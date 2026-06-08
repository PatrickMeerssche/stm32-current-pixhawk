#ifndef MEDIR_CORRENTE_H
#define MEDIR_CORRENTE_H

#include <Arduino.h>

// Estrutura para facilitar o transporte dos dados
struct DadosCorrente {
    float correnteDC;  // Valor médio em Amperes
};

// Protótipos das funções
void configurarSensoresCorrente();
void calibrarZeroCorrente(); // <--- Função crítica para corrigir o "offset"
DadosCorrente lerSensoresCorrente();

#endif