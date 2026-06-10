#/********************************************************************************
 * Pinout & Port Mapping
 *
 * Bluepill STM32F103C8T6 pins used by this firmware:
 * - PA0   : Analog input for current sensor (PIN_DC in MedirCorrente.h)
 * - PC13  : Onboard LED (active-low) reserved for status indication
 * - PA9   : USART1 TX (Serial1 TX) -> connect to Pixhawk TELEM RX (e.g. TELEM1 RX)
 * - PA10  : USART1 RX (Serial1 RX) -> connect to Pixhawk TELEM TX (e.g. TELEM1 TX)
 * - GND   : Common ground (connect Bluepill GND to Pixhawk GND)
 * - 5V    : VIN/USB 5V input (power source from Pixhawk when used)
 *
 * Notes:
 * - Firmware now sends plain CSV serial lines over `Serial1` at 115200 baud.
 * - The Pixhawk Lua script reads the line format `BATT,<voltage>,<current>` and
 *   forwards the values into the scripting battery monitor API.
 ********************************************************************************/

#include <Arduino.h>

#include "MedirCorrente.h"

#define MySerial Serial

// DEBUG: when set to 1 the firmware publishes a fixed test current value
// (777 centi-amps = 7.77 A) regardless of the sensor. This is useful for
// verifying the serial pipeline without the sensor connected or while
// debugging wiring.
// Set `DEBUG = 0` to publish actual sensor values.
static const uint8_t DEBUG = 0;


static HardwareSerial &TelemetrySerial = Serial1;

static const unsigned long LOG_INTERVAL_MS = 80;
static const unsigned long STARTUP_ANALYSIS_MS = 3000;
static const unsigned long SERIAL_TX_INTERVAL_MS = 100;

static void enviarBatteryStatus(const DadosCorrente &dados) {
  float currentAmps = DEBUG ? 7.77f : dados.correnteDC;
  // Voltage field is a placeholder. The Pixhawk Lua script mirrors BATT1 voltage
  // into BATT2, so this value is intentionally ignored on the receiving end.
  float voltageVolts = 5.0f;

  TelemetrySerial.print("BATT,");
  TelemetrySerial.print(voltageVolts, 2);
  TelemetrySerial.print(",");
  TelemetrySerial.print(currentAmps, 2);
  TelemetrySerial.print("\n");
}

// coletarMediaInicial8s: collect an initial average current over
// `STARTUP_ANALYSIS_MS` and print a startup summary to the USB serial
// console. Validation and rejection policy is handled on Pixhawk/Lua.
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
}

void setup() {
  pinMode(PC13, OUTPUT);
  digitalWrite(PC13, HIGH);

  MySerial.begin(115200);
  while (!MySerial && millis() < 4000) { ; }
  delay(1000);

  TelemetrySerial.begin(115200);

  MySerial.println(">>> Datalogger de Corrente DC <<<");
  MySerial.println(">>> Saida serial CSV via UART <<<");

  // Configure ADC and perform a zero-current calibration step. If you want
  // to skip calibration (for automated setups) replace or remove the call
  // to `calibrarZeroCorrente()` and set `offset_DC` in `MedirCorrente.cpp`.
  configurarSensoresCorrente();
  calibrarZeroCorrente();

  coletarMediaInicial8s();

  MySerial.println("SISTEMA PRONTO.");
}

void loop() {
  static unsigned long lastLogMs = 0;
  unsigned long now = millis();

  if (now - lastLogMs >= SERIAL_TX_INTERVAL_MS) {
    lastLogMs = now;

    // Read sensor and send a plain serial line for the Pixhawk Lua script to parse.
    DadosCorrente dadosAmper = lerSensoresCorrente();
    enviarBatteryStatus(dadosAmper);
  }
}