#ifndef DMA_H
#define DMA_H

#include "brain.h"
#include <pthread.h>

typedef struct 
{
    int TRACK;     // pista del disco
    int CYLINDER;  // cilindro del disco
    int SECTOR;    // sector del disco
    int IO;        // 0 = leer (mem -> disk), 1 = escribir (disk -> mem)
    int ADDRESS;   // direccion fisica de la memoria a leer o escribir
    int STATE;     // Resultado de la op E/S -> 0 = exito, 1 = fallo
    int BUSY;      // 0 = libre, 1 = ocupado
    pthread_mutex_t lock;   // semáforo del dma
} DMA_t;

int dma_init();
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
