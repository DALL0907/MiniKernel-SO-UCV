#ifndef DMA_H
#define DMA_H

#include "brain.h"

typedef struct {
  unsigned int TRACK;     // pista del disco
  unsigned int CYLINDER;  // cilindro del disco
  unsigned int SECTOR;    // sector del disco
  unsigned int IO;        // 0 = leer (disk -> mem), 1 = escribir (mem -> disk)
  unsigned int ADDRESS;   // direccion fisica de la memoria
  unsigned int STATE : 1; // Resultado de la op E/S -> 0 = exito 1 = fallo
} DMA_t;

void dma_init();
void dma_destroy();

// Recibe el código y el operando de la instruccion
int dma_handler(int opcode, int operand);

// Devuelve el estado del DMA
int dma_get_state();

#endif // DMA_H
