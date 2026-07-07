# stm32-current-pixhawk

> **Note:** Branch `serial_script` (current) replaces the older `mavlink` branch approach.
> `serial_script`: STM32 sends plain serial frames (`BATT,voltage,current`) and Pixhawk Lua pushes data via `battery:handle_scripting(...)`.
> `mavlink` (old): STM32 directly sent MAVLink `BATTERY_STATUS` packets over TELEM.

STM32 Bluepill firmware that reads an ACS758 current sensor and sends a simple serial frame to Pixhawk.
Pixhawk runs a Lua script that parses this frame and updates a battery monitor instance.

## Quick summary

- MCU: STM32F103C8T6 (Bluepill)
- Sensor: ACS758-100B - any other model can be used, such as, 50B, 150B etc... just change the sensitivity in the code. (See Sensor calibration section below)
- Transport: UART (USART1) -> Pixhawk serial port at 115200
- STM32 output format: `BATT,<voltage>,<current>` (example: `BATT,5.00,12.34`)
- Pixhawk side: Lua script `stm32_current.lua` using `serial:find_serial(0)` + `battery:handle_scripting(...)`

## Wiring (Bluepill <-> Pixhawk)

- PA0  : ACS758 analog output (OU1 ACS758 output -> PA0)
- PA9  : USART1 TX -> Pixhawk TELEM RX (e.g. TELEM1 RX)
- PA10 : USART1 RX -> Pixhawk TELEM TX (e.g. TELEM1 TX)
- GND  : common ground between Bluepill, ACS758 and Pixhawk
- 3.3V : power for ACS758 sensor (from Bluepill 3.3V output)
- 5V   : power from Pixhawk (VIN/USB 5V)

## Pixhawk (ArduPilot) serial + scripting setup

Configure one serial port for scripting (replace `n` with the serial index/port you used):

- `SERIALn_BAUD` = `115k`
- `SERIALn_PROTOCOL` = `Scripting`
- `SERIALn_OPTIONS` = `0`
- `BRD_SERn_RTSCTS` = `0`

Configure Battery 2 as a scripted monitor:

- `BATT2_MONITOR` = `29` (Scripting)
- `BATT2_LOW_VOLT` = set to match your pack chemistry (e.g. `14.0` for 4S LiPo)

Enable scripting:

- `SCR_ENABLE` = `1`

Deploy the script:

- Copy `stm32_current.lua` to the Pixhawk SD card folder `APM/scripts/`
- Reboot Pixhawk (or restart scripting)

If data still does not arrive in Lua, verify the selected serial port has both
`SERIALn_PROTOCOL = Scripting` and `BRD_SERn_RTSCTS = 0` for that same `n`.

## Firmware notes

- `src/main.cpp` sends serial text instead of MAVLink.
- `DEBUG` in `src/main.cpp`:
  - `0`: publish real sensor current
  - `1`: publish fixed test current (`7.77 A`)
- The voltage field in the STM32 serial frame is a placeholder (`5.00 V`). The
  Lua script discards it and mirrors BATT1 voltage into BATT2 instead, so
  BATT2 voltage/health tracks the real pack.
- Validation of bad values (negative current, out-of-range voltage) is handled
  in the Lua script.

## Known limitations

- Some GCS versions (Mission Planner / QGroundControl) may display BATT2 voltage
  as `262.14 V` in the graphical UI. This is a display-side scaling artefact with
  the scripted battery monitor type. The internal ArduPilot value is correct and
  failsafes work correctly based on the real mirrored voltage.
- BATT2 will not publish updates while BATT1 is absent (e.g. USB-only power).
  Once the main battery is connected the script resumes automatically.

## Sensor calibration

- `SENSIBILIDADE_V_A` in `src/MedirCorrente.cpp` is the sensor sensitivity in V/A.
  Example: ACS758LCB-100B at 3.3V → ~13.2 mV/A → `0.0132` V/A.
- `OFFSET_DC_DEFAULT` is the fixed ADC offset for `0.00 A`.
- `CURRENT_GAIN_CAL` scales the measured current to match a trusted reference
  (Fluke clamp meter, bench shunt, or a known-good power module).

**Current calibration status (this branch):**

- **CURRENT_GAIN_CAL:** `-11.23f`
- **OFFSET_DC_DEFAULT:** `731.72f`

### 1) Calibrate zero current (`0.00 A`) using USB debug

Use this first. Gain calibration is only valid after offset is correct.

- Set `DEBUG = 0` in `src/main.cpp` to use the real sensor path.
- Set `USB_DEBUG = 1` in `src/main.cpp` to print diagnostics over USB.
- Ensure the drone/load path is truly idle (`0.00 A` real current).
- Build + flash, then open USB serial monitor at `115200`.
- Record a stable window of lines like `raw=731.71 deltaV=-0.27772 currentA=-19.24`.
- Compute the average of `raw` over 20 to 100 samples.
- Set `OFFSET_DC_DEFAULT` in `src/MedirCorrente.cpp` to that average raw value.
- Rebuild + flash and verify no-load output is near `currentA=0.00`.

### 2) Calibrate gain using a ground-truth current

After zero-offset is correct:

- Keep `DEBUG = 0`.
- Apply one or more steady known loads and record pairs: `I_true` (Fluke) and `I_sensor` (STM32/Pixhawk).
- Use points where `|I_sensor|` is above deadzone (for example > `0.2 A`).
- Compute per-point correction: `k_i = I_true / I_sensor`.
- Compute `k` as mean (or median) of all `k_i`.
- Update gain with `CURRENT_GAIN_CAL_new = CURRENT_GAIN_CAL_old * k`.
- Rebuild + flash and verify with another known load.

Notes:

- If sensor sign is reversed (positive true current but negative sensor current),
  `k` will be negative. This flips sign correctly when applied.
- If small currents still show `0.00`, reduce `NOISE_DEADZONE_AMPS` slightly.

## Files to edit

- `src/main.cpp` — serial output format, `DEBUG` behavior, and telemetry interval.
- `src/MedirCorrente.cpp` — ADC pin, `SENSIBILIDADE_V_A`, `CURRENT_GAIN_CAL`, and sampling parameters.
- `stm32_current.lua` — serial parsing and battery scripting integration on Pixhawk.

## Build & flash (PlatformIO)

From the repository root:

```bash
# build
platformio run

# upload (ensure board and upload settings in platformio.ini are correct)
platformio run --target upload
```

If you encounter `LIBUSB_ERROR_ACCESS` on Linux, add the appropriate udev rules or
run the upload with the necessary permissions.

## License

This repository is published under the MIT License (see `LICENSE`).
