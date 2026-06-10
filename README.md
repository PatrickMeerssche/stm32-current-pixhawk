# stm32-current-pixhawk

> **Note:** Branch `serial_script` (current) replaces the older `mavlink` branch approach.
> `serial_script`: STM32 sends plain serial frames (`BATT,voltage,current`) and Pixhawk Lua pushes data via `battery:handle_scripting(...)`.
> `mavlink` (old): STM32 directly sent MAVLink `BATTERY_STATUS` packets over TELEM.

STM32 Bluepill firmware that reads an ACS758 current sensor and sends a simple serial frame to Pixhawk.
Pixhawk runs a Lua script that parses this frame and updates a battery monitor instance.

## Quick summary

- MCU: STM32F103C8T6 (Bluepill)
- Sensor: ACS758-(100/150/etc)B (example: ACS758LCB-100B)
- Transport: UART (USART1) -> Pixhawk serial port at 115200
- STM32 output format: `BATT,<voltage>,<current>` (example: `BATT,5.00,12.34`)
- Pixhawk side: Lua script `stm32_current.lua` using `serial:find_serial(0)` + `battery:handle_scripting(...)`

## Wiring (Bluepill <-> Pixhawk)

- PA0  : ACS758 analog output (sensor output -> PA0)
- PA9  : USART1 TX -> Pixhawk TELEM RX (e.g. TELEM1 RX)
- PA10 : USART1 RX -> Pixhawk TELEM TX (e.g. TELEM1 TX)
- GND  : common ground between Bluepill and Pixhawk
- 5V   : optional power from Pixhawk (VIN/USB 5V) if you want the Bluepill powered by Pixhawk

Important: always connect grounds. If powering the Bluepill from Pixhawk, ensure the
Bluepill regulator can handle the load.

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
- `CURRENT_GAIN_CAL` is a software multiplier used to align firmware readings with
  a trusted meter.

**Current calibration status:**

- **CURRENT_GAIN_CAL:** 2.188f
- **Calibration Method:** Parallel telemetry comparison (369 measurement pairs)
  - Known sensor (BATT1): Pixhawk native battery monitor (instance 0)
  - Test sensor (BATT2): STM32 custom sensor (instance 1)
  - Analysis: Computed ratio of known current ÷ measured current across flight envelope

**To recalibrate:**

1. Fly a test mission with both battery monitors active and logging enabled.
2. Download the BAT.csv telemetry log (contains BATT1 and BATT2 telemetry).
3. Compare instance 0 (reference) vs instance 1 (STM32) current values.
4. Compute `gain = mean(instance_0_current) / mean(instance_1_current)`.
5. Update `CURRENT_GAIN_CAL` in `src/MedirCorrente.cpp` with the new gain value.
6. Rebuild and flash the STM32 firmware.

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
