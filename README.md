
# stm32-current-pixhawk

> **Note:** This method only logs on TELEMETRY logs, not .bin (internally in pixhawk).
> __
> ---> This is better | I also implemented LUA scripting so it logs to both Telemetry and .bin files (see [serial_script](https://github.com/PatrickMeerssche/stm32-current-pixhawk/tree/serial_script)).

STM32 Bluepill firmware that reads an ACS758 current sensor and publishes it to a Pixhawk
autopilot as MAVLink `BATTERY_STATUS` telemetry.

## Quick summary

- MCU: STM32F103C8T6 (Bluepill)
- Sensor: ACS758-(100/150/etc)B (example: ACS758LCB-100B)
- Transport: UART (USART1) -> connect to Pixhawk TELEM port (115200, MAVLink2)

## Wiring (Bluepill <-> Pixhawk)

- PA0  : ACS758 analog output (sensor output -> PA0)
- PA9  : USART1 TX -> Pixhawk TELEM RX (e.g. TELEM1 RX)
- PA10 : USART1 RX -> Pixhawk TELEM TX (e.g. TELEM1 TX)
- GND  : common ground between Bluepill and Pixhawk
- 5V   : optional power from Pixhawk (VIN/USB 5V) if you want the Bluepill powered by Pixhawk

Important: always connect grounds. If powering the Bluepill from Pixhawk, ensure the
Bluepill regulator can handle the load.

## Pixhawk (ArduPilot) TELEM settings

Configure the TELEM port you used (replace `n` with the TELEM index):

- `SERIALn_BAUD` = `57200`
- `SERIALn_PROTOCOL` = `2`  (MAVLink2)
- `SERIALn_OPTIONS` = `0`

These ensure the Pixhawk accepts MAVLink v2 frames from the STM32.

## Firmware notes

- Configure identity and debug options in `src/main.cpp`:
  - `MAV_SYSTEM_ID`, `MAV_COMPONENT_ID`, `MAV_BATTERY_ID_EXTERNAL`
  - `DEBUG`: set to `0` to publish real sensor values, `1` to publish a fixed test value.

## Sensor calibration

- `SENSIBILIDADE_V_A` in `src/MedirCorrente.cpp` is the sensor sensitivity in V/A.
  Example: ACS758LCB-100B at 3.3V → ~13.2 mV/A → `0.0132` V/A.
- `CURRENT_GAIN_CAL` is a software multiplier used to align firmware readings with
  a trusted meter. To calibrate:
  1. Apply a known current and record the firmware reading.
  2. Compute `gain = true_current / measured_current`.
  3. Update `CURRENT_GAIN_CAL` in `src/MedirCorrente.cpp` and rebuild.

## Files to edit

- `src/main.cpp` — MAVLink identity, `DEBUG` flag, message packing, and telemetry logic.
- `src/MedirCorrente.cpp` — ADC pin, `SENSIBILIDADE_V_A`, `CURRENT_GAIN_CAL`, and sampling parameters.

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
