#include "MedirCorrente.h"

// Current sensor conversion for ACS758 on Bluepill.
// Flow: ADC raw -> voltage delta from offset -> amps -> calibrated gain.

// PA0 receives ACS758 analog output.
const int PIN_DC  = PA0;

// STM32 ADC conversion constants.
const float ADC_VOLTAGE_REF = 3.3;  
const float ADC_SCALE = 4095.0;     

// Sampling and sensor parameters.
const unsigned long SAMPLING_WINDOW_MS = 80;  // Keep aligned with main loop timing.
const float NOISE_DEADZONE_AMPS = 0.05;       // Clamp only tiny near-zero noise to zero.
// ACS758 sensitivity in V/A (example: 13.2mV/A at 3.3V).
const float SENSIBILIDADE_V_A = 0.0132;
// Calibration multiplier (reference_current / measured_current).
const float CURRENT_GAIN_CAL = -14.75f; // Fine-trimmed from latest point: Fluke 0.93A vs sensor 1.16A.
const float OFFSET_DC_DEFAULT = 730.16f;      // Fixed zero-current ADC offset from latest confirmed 0.00A samples.

// Runtime offset used by conversion.
float offset_DC  = OFFSET_DC_DEFAULT;

void configurarSensoresCorrente() {
    analogReadResolution(12); // STM32 ADC range: 0..4095
    pinMode(PIN_DC, INPUT_ANALOG);
}

DadosCorrente lerSensoresCorrente() {
    DadosCorrente dados{};

    // Average samples over a short window to reduce ADC noise.
    unsigned long startTime = millis();
    long amostrasCount = 0;
    long somaLeituras_DC = 0;

    while (millis() - startTime < SAMPLING_WINDOW_MS) {
        int rawDC = analogRead(PIN_DC);
        somaLeituras_DC += rawDC;
        amostrasCount++;
    }

    if (amostrasCount == 0) amostrasCount = 1; // Safety guard.

    // Convert ADC average to current in amps.
    dados.mediaRawDC = (float)somaLeituras_DC / (float)amostrasCount;
    dados.tensaoDiferencaDC = (dados.mediaRawDC - offset_DC) * (ADC_VOLTAGE_REF / ADC_SCALE);
    dados.correnteDC = (dados.tensaoDiferencaDC / SENSIBILIDADE_V_A) * CURRENT_GAIN_CAL;

    // Suppress flicker around zero.
    if (fabs(dados.correnteDC) < NOISE_DEADZONE_AMPS) dados.correnteDC = 0.0;

    return dados;
}