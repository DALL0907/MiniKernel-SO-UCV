#include "bus.h"
#include "memory.h"
#include <pthread.h>
#include <stdio.h>

// Mutex para el arbitraje [Fuente: 44, 96]
static pthread_mutex_t bus_lock;

void bus_init()
{
    mem_init(); // El bus enciende la memoria
    pthread_mutex_init(&bus_lock, NULL);
}

int bus_read(int address, Word *data, int client_id)
{
    // 1. Arbitraje: Adquirir el bus
    pthread_mutex_lock(&bus_lock);

    // 2. Realizar la operación física
    int result = mem_read_physical(address, data);

    // 3. Liberar el bus
    pthread_mutex_unlock(&bus_lock);

    return result;
}

int bus_write(int address, Word data, int client_id)
{
    // 1. Arbitraje
    pthread_mutex_lock(&bus_lock);

    // 2. Operación
    int result = mem_write_physical(address, data);

    // 3. Liberar
    pthread_mutex_unlock(&bus_lock);

    return result;
}