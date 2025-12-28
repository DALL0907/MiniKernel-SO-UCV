#ifndef MEMORY_H
#define MEMORY_H

#include "brain.h"

// Inicializa la RAM a 0
void mem_init();

// Acceso Físico (Usado solo por el BUS)
// Retorna 0 si éxito, -1 si error de hardware (índice fuera de 0-1999)
int mem_read_physical(int address, Word *value);
int mem_write_physical(int address, Word value);

#endif