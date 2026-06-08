#/********************************************************************************
 * Pinout & Port Mapping
 *
 * Bluepill STM32F103C8T6 pins used by this firmware:
 * - PA0   : Analog input for current sensor (PIN_DC in MedirCorrente.h)
 * - PC13  : Onboard LED (active-low) used for error/lock blinking
 * - PA9   : USART1 TX (Serial1 TX) -> connect to Pixhawk TELEM RX (e.g. TELEM1 RX)
 * - PA10  : USART1 RX (Serial1 RX) -> connect to Pixhawk TELEM TX (e.g. TELEM1 TX)
 * - GND   : Common ground (connect Bluepill GND to Pixhawk GND)
 * - 5V    : VIN/USB 5V input (power source from Pixhawk when used)
 *
 * Notes:
 * - MAVLink telemetry is sent over `Serial1` (USART1) at 115200 baud. On Pixhawk use
 *   a TELEM port (TELEM1/TELEM2) configured to 115200 / Mavlink2 / option 0.
 * - Firmware sends MAVLink `BATTERY_STATUS` frames with a distinct
 *   system/component and battery id so it appears as an external
 *   payload battery and does not overwrite the vehicle's primary battery.
 ********************************************************************************/

#include <Arduino.h>
#include <mavlink/common/mavlink.h>

#include "MedirCorrente.h"

#define MySerial Serial

// MAVLink identity (tune these to avoid collisions with the autopilot)
// - `MAV_SYSTEM_ID`: unique system id for this external telemetry source.
//   Pick a value that is different from your vehicle (avoid `1` if the
//   autopilot uses it). Example: 42.
// - `MAV_COMPONENT_ID`: component id for this board (any unused small value).
// - `MAV_BATTERY_ID_EXTERNAL`: battery id used in BATTERY_STATUS frames.
//   Use a different battery id than the autopilot's main battery so the
//   telemetry appears as an external source in Mission Planner / QGroundControl.
// To change identity: edit these defines and rebuild. Keep them consistent
// if you power multiple telemetry sources on the same bus.
#define MAV_SYSTEM_ID 42
#define MAV_COMPONENT_ID 158
#define MAV_BATTERY_ID_EXTERNAL 1

// DEBUG: when set to 1 the firmware publishes a fixed test current value
// (777 centi-amps = 7.77 A) regardless of the sensor. This is useful for
// verifying that MAVLink frames are visible on the autopilot without the
// sensor connected or while debugging wiring.
// Set `DEBUG = 0` to publish actual sensor values.
static const uint8_t DEBUG = 0;


static HardwareSerial &TelemetrySerial = Serial1;

static unsigned long logStartMs = 0;
static const unsigned long LOG_INTERVAL_MS = 80;
static const unsigned long STARTUP_ANALYSIS_MS = 3000;
static const unsigned long MAVLINK_TX_INTERVAL_MS = 1000;

static void travarComPiscaErro() {
  while (true) {
    digitalWrite(PC13, LOW);
    delay(200);
    digitalWrite(PC13, HIGH);
    delay(200);
  }
}

// Pack and send a MAVLink BATTERY_STATUS message.
// - `dados.correnteDC` is in amperes (float). The MAVLink BATTERY_STATUS expects
//   `current_battery` in centi-amps (A * 100). We cast and clamp as int16_t.
// enviarBatteryStatus: assemble and send a MAVLink BATTERY_STATUS frame.
// - `dados.correnteDC` is read in amperes (A).
// - MAVLink `current_battery` field is in centi-amps (A * 100) and stored
//   as an int16_t. Values outside the int16 range will wrap — keep currents
//   within sensible bounds or clamp if needed.
// - To add voltage measurements populate the `voltages` / `voltages_ext`
//   arrays and adjust the pack call accordingly.
// - This function writes the raw MAVLink packet to `TelemetrySerial`.
static void enviarBatteryStatus(const DadosCorrente &dados, float tempoDecorridoS) {
  (void)tempoDecorridoS;

  mavlink_message_t msg;
  uint16_t voltages[10];
  uint16_t voltagesExt[4];
  for (unsigned int i = 0; i < 10; ++i) {
    voltages[i] = UINT16_MAX;
  }
  for (unsigned int i = 0; i < 4; ++i) {
    voltagesExt[i] = 0;
  }

  // Convert A -> centi-A for MAVLink. Keep DEBUG override for quick tests.
  int16_t currentBatteryCentiAmps = DEBUG ? 777 : (int16_t)(dados.correnteDC * 100.0f);

  mavlink_msg_battery_status_pack(
    MAV_SYSTEM_ID,
    MAV_COMPONENT_ID,
    &msg,
    MAV_BATTERY_ID_EXTERNAL,
    MAV_BATTERY_FUNCTION_PAYLOAD,
    MAV_BATTERY_TYPE_UNKNOWN,
    INT16_MAX,
    voltages,
    currentBatteryCentiAmps,
    -1,
    -1,
    -1,
    -1,
    MAV_BATTERY_CHARGE_STATE_UNDEFINED,
    voltagesExt,
    0,
    0
  );

  // Serialize and send over the configured telemetry UART (`TelemetrySerial`).
  uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
  uint16_t length = mavlink_msg_to_send_buffer(buffer, &msg);
  TelemetrySerial.write(buffer, length);
}

// enviarHeartbeat: simple heartbeat so the autopilot/QGroundControl can
// discover this MAVLink source. Keep a regular heartbeat cadence (this
// firmware sends heartbeats twice per second in `loop()`). If the autopilot
// requires a different type/autopilot id, change the `mavlink_msg_heartbeat_pack`
// parameters here.
static void enviarHeartbeat() {
  mavlink_message_t msg;
  mavlink_msg_heartbeat_pack(
    MAV_SYSTEM_ID,
    MAV_COMPONENT_ID,
    &msg,
    MAV_TYPE_GENERIC,
    MAV_AUTOPILOT_GENERIC,
    0,
    0,
    MAV_STATE_ACTIVE
  );

  uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
  uint16_t length = mavlink_msg_to_send_buffer(buffer, &msg);
  TelemetrySerial.write(buffer, length);
}

static void enviarHeartbeatInicial() {
  enviarHeartbeat();
  delay(50);
  enviarHeartbeat();
}

// drenarBufferSerial1: simple RX drain.
// The autopilot may send bytes to the telemetry port; if unused, these bytes
// can fill the UART FIFO and cause issues. This helper empties any pending
// RX bytes. If you need to parse incoming MAVLink messages from the autopilot,
// replace this with a proper MAVLink input handler.
static void drenarBufferSerial1() {
  while (TelemetrySerial.available() > 0) {
    (void)TelemetrySerial.read();
  }
}

// enviarStatusText: send a short text message to the autopilot for debugging.
// The `statustext` message may be split in chunks by the MAVLink library; this
// basic wrapper sends short messages only. For long text support implement
// chunking using `id`/`chunk_seq` fields.
static void enviarStatusText(const char *txt) {
  mavlink_message_t msg;
  mavlink_msg_statustext_pack(
    MAV_SYSTEM_ID,
    MAV_COMPONENT_ID,
    &msg,
    MAV_SEVERITY_INFO,
    txt,
    0,
    0
  );

  uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
  uint16_t length = mavlink_msg_to_send_buffer(buffer, &msg);
  TelemetrySerial.write(buffer, length);
}

// coletarMediaInicial8s: collect an initial average current over
// `STARTUP_ANALYSIS_MS` to detect gross errors (for example negative
// values if the sensor wiring is reversed) and to print a quick startup
// summary to the USB serial console. This helps when calibrating or
// verifying wiring during bench tests.
static void coletarMediaInicial8s() {
  float soma = 0.0f;
  unsigned long sampleCount = 0;
  unsigned long inicio = millis();

  MySerial.println(">>> Coleta inicial (3s) <<<");
  MySerial.flush();

  while (millis() - inicio < STARTUP_ANALYSIS_MS) {
    DadosCorrente leitura = lerSensoresCorrente();
    soma += leitura.correnteDC;
    sampleCount++;
    delay(LOG_INTERVAL_MS);
  }

  if (sampleCount == 0) {
    sampleCount = 1;
  }

  float mediaInicial = soma / (float)sampleCount;

  MySerial.print("Mean: ");
  MySerial.println(mediaInicial, 2);
  MySerial.flush();

  if (mediaInicial < -2.0f) {
    MySerial.println("ERRO: Current < -2A. LOCKED.");
    MySerial.flush();
    travarComPiscaErro();
  }
}

void setup() {
  pinMode(PC13, OUTPUT);
  digitalWrite(PC13, HIGH);

  MySerial.begin(115200);
  while (!MySerial && millis() < 4000) { ; }
  delay(1000);

  TelemetrySerial.begin(115200);
  enviarHeartbeatInicial();

  MySerial.println(">>> Datalogger de Corrente DC <<<");
  MySerial.println(">>> Saida MAVLink via UART <<<");

  // Configure ADC and perform a zero-current calibration step. If you want
  // to skip calibration (for automated setups) replace or remove the call
  // to `calibrarZeroCorrente()` and set `offset_DC` in `MedirCorrente.cpp`.
  configurarSensoresCorrente();
  calibrarZeroCorrente();

  logStartMs = millis();
  coletarMediaInicial8s();

  MySerial.println("SISTEMA PRONTO.");
}

void loop() {
  static unsigned long lastLogMs = 0;
  static unsigned long lastHeartbeatMs = 0;
  unsigned long now = millis();

  // Keep the RX buffer drained by default. Replace with MAVLink RX handling
  // if you plan to receive configuration commands from the autopilot.
  drenarBufferSerial1();

  if (now - lastLogMs >= MAVLINK_TX_INTERVAL_MS) {
    lastLogMs = now;

    // Read sensor, compute elapsed time (unused currently) and send MAVLink
    // BATTERY_STATUS containing the measured current as centi-amps.
    DadosCorrente dadosAmper = lerSensoresCorrente();
    float tempoDecorridoS = (millis() - logStartMs) / 1000.0f;

    enviarBatteryStatus(dadosAmper, tempoDecorridoS);
  }

  /* heartbeat every 1s so Pixhawk/QGC will detect this MAVLink source */
  if (now - lastHeartbeatMs >= 500) {
    lastHeartbeatMs = now;
    enviarHeartbeat();
  }
}