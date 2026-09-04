-- Script to read STM32 current sensor data via serial and feed ArduPilot's
-- scripting battery monitor.

local SCRIPT_VERSION = "v5"

local port = serial:find_serial(0)
if not port then
    gcs:send_text(0, "STM32 Batt: No Scripting Serial Port found")
    return
end

port:begin(115200)

local BATT_INSTANCE = 1
local rx_buffer = ""
local parsed_frames = 0
local bad_lines = 0
local bytes_seen = 0
local loop_count = 0
local last_good_ms = 0
local last_diag_ms = 0
local last_batt1_volts = nil
local using_frame_voltage = false

local DIAG_INTERVAL_MS = 2000
local STALE_WARN_MS = 3000
local WAIT_INFO_INTERVAL_MS = 10000
local last_wait_info_ms = 0
local MAX_BYTES_PER_CYCLE = 128
local MAX_LINES_PER_CYCLE = 12
local mirrored_voltage

local function publish_unhealthy()
    local ok_state, state = pcall(BattMonitorScript_State)
    if not ok_state or not state then return end
    state:healthy(false)
    local out_volts = mirrored_voltage()
    if out_volts then state:voltage(out_volts) end
    state:current_amps(0)
    state:cell_count(1)
    pcall(battery.handle_scripting, battery, BATT_INSTANCE, state)
end

local function batt1_voltage_valid(v)
    return v and v > 5.0 and v < 60.0
end

local function safe_battery_call(method_name, instance)
    local fn = battery[method_name]
    if not fn then return nil end
    local ok, value = pcall(fn, battery, instance)
    if not ok then return nil end
    return tonumber(value)
end

local function read_batt1_voltage()
    local v = safe_battery_call("voltage", 0)
    if batt1_voltage_valid(v) then return v end
    local vr = safe_battery_call("voltage_resting_estimate", 0)
    if batt1_voltage_valid(vr) then return vr end
    return nil
end

mirrored_voltage = function()
    local v = read_batt1_voltage()
    if v then last_batt1_volts = v end
    return last_batt1_volts
end

-- Debug/logging control: set to true to enable regular diagnostic prints.
local DEBUG = false
-- Track the last reported bad frame count so we only notify on new bad frames
local last_reported_bad_lines = 0

local function send_text_debug(level, msg, force)
    if force then
        gcs:send_text(level, msg)
        return
    end
    if DEBUG then gcs:send_text(level, msg) end
end
local function process_line(line)
    local volts_str, amps_str = line:match("BATT%s*,%s*([%d%.%-]+)%s*,%s*([%d%.%-]+)")
    
    if not volts_str or not amps_str then
        bad_lines = bad_lines + 1
        send_text_debug(4, string.format("STM32 Batt: bad frame #%d (parse fail): '%s'", bad_lines, line), true)
        return
    end

    local volts = tonumber(volts_str)
    local amps = tonumber(amps_str)
    if not volts or not amps then
        bad_lines = bad_lines + 1
        send_text_debug(4, string.format("STM32 Batt: bad frame #%d (number fail): '%s'", bad_lines, line), true)
        return
    end

    if volts < 0 or volts > 60 or amps < -200 or amps > 200 then
        bad_lines = bad_lines + 1
        send_text_debug(4, string.format("STM32 Batt: bad frame #%d (range fail): '%s'", bad_lines, line), true)
        return
    end

    local out_volts = mirrored_voltage()
    if not out_volts then
        out_volts = volts
        if not using_frame_voltage then
            using_frame_voltage = true
            send_text_debug(4, "STM32 Batt: using frame voltage fallback", false)
        end
    elseif using_frame_voltage then
        using_frame_voltage = false
        send_text_debug(6, "STM32 Batt: restored BATT1 voltage mirror", false)
    end

    local ok_state, state = pcall(BattMonitorScript_State)
    if not ok_state or not state then return end

    state:healthy(true)
    state:voltage(out_volts)
    state:current_amps(amps)
    state:cell_count(1)

    local ok_handle, handled = pcall(battery.handle_scripting, battery, BATT_INSTANCE, state)
    if not ok_handle or handled == false then return end

    parsed_frames = parsed_frames + 1
    last_good_ms = millis():tofloat()
end

local function update_impl()
    local now = millis():tofloat()
    loop_count = loop_count + 1
    local read_count = 0
    
    for _ = 1, MAX_BYTES_PER_CYCLE do
        local b = port:read()
        if b == -1 then break end
        read_count = read_count + 1
        bytes_seen = bytes_seen + 1
        rx_buffer = rx_buffer .. string.char(b)
    end

    if read_count > 0 then
        local lines_processed = 0
        while lines_processed < MAX_LINES_PER_CYCLE do
            local newline_pos = rx_buffer:find("\n", 1, true)
            if not newline_pos then break end
            local line = rx_buffer:sub(1, newline_pos - 1):gsub("[\r\n]", "")
            rx_buffer = rx_buffer:sub(newline_pos + 1)
            process_line(line)
            lines_processed = lines_processed + 1
        end
        if #rx_buffer > 128 then rx_buffer = rx_buffer:sub(-64) end
    end

    if now - last_diag_ms >= DIAG_INTERVAL_MS then
        last_diag_ms = now
        local age = -1
        if last_good_ms > 0 then
            age = now - last_good_ms
            if age < 0 then age = 0 end 
        end
        if age < 0 then
            publish_unhealthy()
            -- always notify when we have no valid frame yet
            send_text_debug(6, string.format("STM32 Batt: no valid frame yet (bytes=%d bad=%d)", bytes_seen, bad_lines), true)
        elseif age > STALE_WARN_MS then
            publish_unhealthy()
            -- always notify when stale
            send_text_debug(4, string.format("STM32 Batt: stale %dms (frames=%d bytes=%d)", math.floor(age), parsed_frames, bytes_seen), true)
        else
            -- normal OK diagnostics only when DEBUG enabled
            send_text_debug(7, string.format("STM32 Batt: ok frames=%d bad=%d", parsed_frames, bad_lines), false)
        end

        -- If we've seen new bad frames since last report, notify (forced)
        if bad_lines > last_reported_bad_lines then
            last_reported_bad_lines = bad_lines
            send_text_debug(4, string.format("STM32 Batt: bad frame(s) detected total=%d", bad_lines), true)
        end
    end

    if now - last_wait_info_ms >= WAIT_INFO_INTERVAL_MS then
        last_wait_info_ms = now
        if last_good_ms == 0 then
            send_text_debug(6, string.format("STM32 Batt: alive loops=%d bytes=%d", loop_count, bytes_seen), false)
        end
    end

    return update, 100
end

function update()
    local ok, next_fn, delay_ms = pcall(update_impl)
    if not ok then
        gcs:send_text(0, "STM32 Batt runtime error")
        return update, 500
    end
    return next_fn, delay_ms
end

gcs:send_text(6, string.format("STM32 Battery Script Active %s (inst=%d)", SCRIPT_VERSION, BATT_INSTANCE))
return update()