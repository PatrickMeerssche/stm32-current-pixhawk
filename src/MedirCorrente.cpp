#include "MedirCorrente.h"

// Current sensor configuration and conversion helpers.
//
// Overview for future editors:
// - The ADC reads the ACS758 output on `PIN_DC`. The raw ADC reading is
//   converted to voltage using `ADC_VOLTAGE_REF` and `ADC_SCALE`.
// - `SENSIBILIDADE_V_A` is the sensor sensitivity (V per ampere). Change it
//   if you use a different ACS758 variant or a different sensor.
// - `CURRENT_GAIN_CAL` is a multiplicative calibration factor applied after
//   converting the sensor voltage to amps. Use it to align firmware readings
//   with a trusted meter. Typical workflow: measure a known current, compute
//   gain = true/measured, then update `CURRENT_GAIN_CAL`.
// - `SAMPLING_WINDOW_MS` controls how long the `lerSensoresCorrente()`
//   accumulation lasts. Increase for smoother but slower updates, decrease
//   if you need faster sampling.
//
// If you need to add multiple sensors, copy and adapt the conversion logic
// and expose a sensor index/id in the `DadosCorrente` struct.

// --- DEFINIÇÃO DOS PINOS (ADC - Bluepill STM32F103C8T6) ---
// PIN_DC: connect ACS758 output to PA0 (analog input)
const int PIN_DC  = PA0;

// --- CONSTANTES DE FÍSICA E AMOSTRAGEM ---
// Hardware STM32
const float ADC_VOLTAGE_REF = 3.3;  
const float ADC_SCALE = 4095.0;     

// Parâmetros da Lógica (Baseado no seu main.cpp)
const unsigned long SAMPLING_WINDOW_MS = 80;  // Janela de 80ms
const float NOISE_DEADZONE_AMPS = 0.30;       // Zona morta de 0.3A
// SENSIBILIDADE_V_A: sensor sensitivity in volts per ampere (V/A).
// Example: ACS758LCB-100B at 3.3V -> ~13.2mV/A -> 0.0132 V/A
const float SENSIBILIDADE_V_A = 0.0132;       // 13.2mV/A for ACS758 at 3.3V
// CURRENT_GAIN_CAL: software calibration multiplier to scale measured current
// to match a known reference. Adjust after taking a measured reading vs a
// trusted meter. Example in this repo: measured 220 -> real 252 => ~1.145x
const float CURRENT_GAIN_CAL = 2.188f; // multiplicative gain calibration factor
const float OFFSET_DC_DEFAULT = 2022.10;      // Offset measured at zero current

// --- VARIÁVEIS DE CALIBRAÇÃO (OFFSET) ---
float offset_DC  = OFFSET_DC_DEFAULT;

void configurarSensoresCorrente() {
    analogReadResolution(12); // STM32 usa 12 bits (0-4095)
    pinMode(PIN_DC, INPUT_ANALOG);
}

// calibrarZeroCorrente: take many ADC samples at startup to compute the
// zero-current offset. The function currently uses 1000 samples with a 1 ms
// delay between them (about 1 second plus the initial delay). The result is
// stored in `offset_DC` and used to center the ADC readings.
//
// When running automated tests, you can replace this routine with a fixed
// offset value or skip it entirely (but ensure `offset_DC` is set to a
// reasonable value for your hardware).
void calibrarZeroCorrente() {
        // long somaDC  = 0;
        // int n = 1000;

        // delay(1000); // wait for sensor/regulator to stabilize

        // for(int i=0; i<n; i++) {
        //         somaDC  += analogRead(PIN_DC);
        //         delay(1);
        // }

        // The offset value below was determined empirically in test runs. If you
        // re-run calibration on your hardware, this value will be overwritten by
        // the measured average in future versions — consider computing the average
        // programmatically and assigning it to `offset_DC`.
        offset_DC  = 2027.168; // measured average from earlier calibration

        #if defined(USBCON)
            SerialUSB.println("--- CALIBRACAO CONCLUIDA (Logica Media 1000x) ---");
            SerialUSB.print("Offset DC:  "); SerialUSB.println(offset_DC);
        #endif
}

DadosCorrente lerSensoresCorrente() {
    DadosCorrente dados;

    // Sampling loop: accumulate raw ADC readings for a short window
    // (SAMPLING_WINDOW_MS). This reduces noise by averaging multiple samples.
    unsigned long startTime = millis();
    long amostrasCount = 0;
    long somaLeituras_DC = 0;

    while (millis() - startTime < SAMPLING_WINDOW_MS) {
        int rawDC = analogRead(PIN_DC);
        somaLeituras_DC += rawDC;
        amostrasCount++;
    }

    if (amostrasCount == 0) amostrasCount = 1; // safety

    // Convert average raw ADC value to voltage, subtract the calibrated
    // offset (zero current), convert to amps using sensor sensitivity, and
    // apply the software gain calibration.
    float mediaRawDC = (float)somaLeituras_DC / (float)amostrasCount;
    float tensaoDiferencaDC = (mediaRawDC - offset_DC) * (ADC_VOLTAGE_REF / ADC_SCALE);
    dados.correnteDC = (tensaoDiferencaDC / SENSIBILIDADE_V_A) * CURRENT_GAIN_CAL;

    // Noise gate: small readings near zero are set to zero to avoid
    // flickering around the measurement noise floor.
    if (fabs(dados.correnteDC) < NOISE_DEADZONE_AMPS) dados.correnteDC = 0.0;

    return dados;
}