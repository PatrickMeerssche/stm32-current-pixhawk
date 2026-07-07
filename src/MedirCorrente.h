#ifndef MEDIR_CORRENTE_H
#define MEDIR_CORRENTE_H

#include <Arduino.h>

// Estrutura para facilitar o transporte dos dados
struct DadosCorrente {
    float correnteDC;  // Valor médio em Amperes
    float mediaRawDC;
    float tensaoDiferencaDC;
};

// Protótipos das funções
void configurarSensoresCorrente();
DadosCorrente lerSensoresCorrente();

#endif