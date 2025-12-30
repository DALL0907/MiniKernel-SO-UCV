#include "dma.h"
#include "brain.h"
#include "log.h"
#include <pthread.h>
#include <string.h>

static DMA_t dma;

int dma_init() {
    if (pthread_mutex_init(&dma.lock, NULL) != 0)
    {
        write_log(1, "DMA: Fallo al iniciar el mutex\n");
        return -1;
    }
    memset(&dma, 0, sizeof(dma));
    dma.TRACK = 0;
    dma.CYLINDER = 0;
    dma.SECTOR = 0;
    dma.STATE = 1; // sin exito porque no se ha hecho nada
    dma.IO = 0;
    dma.ADDRESS = 0;
    write_log(0, "DMA: inicializado exitosamente\n");
    return 0;
}

int dma_handler(int opcode, int value) {
  /*
   * 1. DMA debe recibir la operacion dma con su valor
   * 2. el dma_handler debe implementar la logica de la operacion
   *        SDMAP -> guardar la pista en dma.TRACK
   *        SDMAC -> guardar el cilindro en dma.CYLINDER
   *        SDMAS -> guardar el sector en dma.sector
   *        SDMAIO -> guardar 1 o 0 en dma.IO (0 = leer mem 1 = escribir mem)
   *        SDMAM -> guardar value en dma.ADDRESS
   *        SDMAON -> empezar la operacion de E/S
   *        tras cada caso escribir en log para indicar en qué paso se está
   *        ejemplo:
   *        write_log(0, "DMA: ")
   * 3. implementar una funcion que realice la operacion de E/S
   */
    switch (opcode) {
        case OP_SDMAP:
            dma.TRACK = value;
            break;
        case OP_SDMAC:
            dma.CYLINDER = value;
            break;
        case OP_SDMAS:
            dma.SECTOR = value;
            break;
        case OP_SDMAIO:
            dma.IO = value;
            break;
        case OP_SDMAM:
            dma.ADDRESS = value;
            break;
        case OP_SDMAON:
            // Iniciar la operacion de E/S
            break;
  }
    return 0;
}

int dma_get_state() 
{ 
    return dma.STATE; 
}

void dma_destroy() 
{
    pthread_mutex_destroy(&dma.lock);
    write_log(0, "DMA: finalizado exitosamente\n");
}
