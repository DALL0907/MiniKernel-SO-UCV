#include "memory.h"
#include <stdio.h>
#include <string.h>

// La RAM física [Fuente: 33]
static Word RAM[MEM_SIZE];

void mem_init()
{
    // Limpiamos la memoria al iniciar
    memset(RAM, 0, sizeof(RAM));
}

int mem_read_physical(int address, Word *value)
{
    if (address < 0 || address >= MEM_SIZE)
    {
        return -1; // Error fatal de hardware (fuera de límites físicos)
    }
    *value = RAM[address];
    return 0;
}

int mem_write_physical(int address, Word value)
{
    if (address < 0 || address >= MEM_SIZE)
    {
        return -1;
    }
    RAM[address] = value;
    return 0;
}