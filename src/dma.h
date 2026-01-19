#ifndef DMA_H
#define DMA_H

#include "brain.h"
#include <pthread.h>

// Inicia el modulo dma
int dma_init();

// Elimina el modulo dma
void dma_destroy();

// Recibe el código, el valor y el modo de ejecución (Kernel/Usuario) a usar con la instruccion
int dma_handler(int opcode, int value, unsigned int mode);

// Indica si el dma está trabajando
int dma_is_busy();

// Devuelve el estado del DMA
int dma_get_state();

// Funcion que ejecutará el hilo que quiera usar el dma para su op de E/S 
void *dma_perform_io(void *arg);

#endif // DMA_H
