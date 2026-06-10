-- Script to read STM32 current sensor data via serial and feed ArduPilot's
-- scripting battery monitor.

local SCRIPT_VERSION = "v4"

local port = serial:find_serial(0)
if not port then
    gcs:send_text(0, "STM32 Batt: No Scripting Serial Port found")
    return
end

port:begin(115200)

-- 0-indexed battery instance: 1 = BATT2
local BATT_INSTANCE = 1

local rx_buffer = ""
local parsed_frames = 0
local bad_lines = 0
local bytes_seen = 0
local last_good_ms = 0
local last_diag_ms = 0
local last_batt1_volts = nil

local DIAG_INTERVAL_MS = 2000
local STALE_WARN_MS = 3000
local WAIT_INFO_INTERVAL_MS = 10000
local last_wait_info_ms = 0
local MAX_BYTES_PER_CYCLE = 128
local MAX_LINES_PER_CYCLE = 12

local function batt1_voltage_valid(v)
    return v and v > 5.0 and v < 60.0
end

local function read_batt1_voltage()
    local v = tonumber(battery:voltage(0))
    if batt1_voltage_valid(v) then
        return v
    end

    local vr = tonumber(battery:voltage_resting_estimate(0))
    if batt1_voltage_valid(vr) then
        return vr
    end

    return nil
end

local function batt1_ready()
    local v = read_batt1_voltage()
    if v then
        last_batt1_volts = v
        return true
    end

    return last_batt1_volts ~= nil
end

local function mirrored_voltage()
    local v = read_batt1_voltage()
    if v then
        last_batt1_volts = v
    end

    return last_batt1_volts
end

local function process_line(line)
    -- Parse CSV frame from STM32: BATT,<voltage>,<current>
    -- The voltage field is validated for frame sanity but not used as BATT2
    -- output voltage; BATT2 voltage is always mirrored from BATT1 instead.
    local volts_str, amps_str = line:match("^%s*BATT%s*,%s*([%d%.%-]+)%s*,%s*([%d%.%-]+)%s*$")
    if not volts_str or not amps_str then
        bad_lines = bad_lines + 1
        return
    end

    local volts = tonumber(volts_str)
    local amps = tonumber(amps_str)
    if not volts or not amps then
        bad_lines = bad_lines + 1
        return
    end

    -- Reject obviously invalid telemetry frames to avoid poisoning battery state.
    if volts < 0 or volts > 60 or amps < -200 or amps > 200 then
        bad_lines = bad_lines + 1
        return
    end

    -- Wait for a real battery on BATT1 (useful when Pixhawk is USB-powered)
    -- before publishing scripted BATT2 values.
    if not batt1_ready() then
        return
    end

    -- Mirror BATT1 voltage to BATT2 so BATT2 health/failsafes are based on the
    -- real pack voltage while current comes from STM32.
    local out_volts = mirrored_voltage()
    if not out_volts then
        return
    end

    local state = BattMonitorScript_State()
    state:healthy(true)
    state:voltage(out_volts)
    state:current_amps(amps)
    state:cell_count(1)

    if not battery:handle_scripting(BATT_INSTANCE, state) then
        gcs:send_text(4, "STM32 Batt: handle_scripting failed")
        return
    end

    parsed_frames = parsed_frames + 1
    last_good_ms = tonumber(millis()) or 0
end

function update()
    local now = tonumber(millis()) or 0
    local read_count = 0
    for _ = 1, MAX_BYTES_PER_CYCLE do
        local b = port:read()
        if b == -1 then
            break
        end

        read_count = read_count + 1
        bytes_seen = bytes_seen + 1
        rx_buffer = rx_buffer .. string.char(b)
    end

    if read_count > 0 then
        local lines_processed = 0
        while lines_processed < MAX_LINES_PER_CYCLE do
            local newline_pos = rx_buffer:find("\n", 1, true)
            if not newline_pos then
                break
            end

            local line = rx_buffer:sub(1, newline_pos - 1):gsub("\r$", "")
            rx_buffer = rx_buffer:sub(newline_pos + 1)
            process_line(line)
            lines_processed = lines_processed + 1
        end

        if #rx_buffer > 128 then
            rx_buffer = rx_buffer:sub(-64)
        end
    end

    if now - last_diag_ms >= DIAG_INTERVAL_MS then
        last_diag_ms = now
        local age = (last_good_ms == 0) and -1 or (now - last_good_ms)

        if not batt1_ready() then
            if now - last_wait_info_ms >= WAIT_INFO_INTERVAL_MS then
                last_wait_info_ms = now
                gcs:send_text(6, "STM32 Batt: waiting for BATT1")
            end
            return update, 100
        end

        if age < 0 then
            gcs:send_text(6, string.format("STM32 Batt: no valid frame yet (bytes=%d bad=%d)", bytes_seen, bad_lines))
        elseif age > STALE_WARN_MS then
            gcs:send_text(4, string.format("STM32 Batt: stale %dms (frames=%d bytes=%d)", age, parsed_frames, bytes_seen))
        else
            gcs:send_text(7, string.format("STM32 Batt: ok frames=%d bad=%d", parsed_frames, bad_lines))
        end
    end

    return update, 100
end

gcs:send_text(6, string.format("STM32 Battery Script Active %s (inst=%d)", SCRIPT_VERSION, BATT_INSTANCE))
return update()