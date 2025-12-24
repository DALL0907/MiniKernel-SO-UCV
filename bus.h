#ifndef BUS_H
#define BUS_H

#include "brain.h"

// Inicializa el semáforo/mutex del bus
void bus_init();

// La CPU o DMA solicitan acceso a memoria a través del bus.
// El bus se encarga del bloqueo (arbitraje) y llama a mem_phys.
// client_id es solo para debug/log (0=CPU, 1=DMA)
int bus_read(int address, Word *data, int client_id);
int bus_write(int address, Word data, int client_id);

#endif