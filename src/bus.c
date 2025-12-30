#include "bus.h"
#include "memory.h"
#include "log.h"
#include <pthread.h>
#include <stdio.h>

// Mutex para el arbitraje
static pthread_mutex_t bus_lock;

int bus_init() 
{
    mem_init(); // El bus enciende la memoria
    // Verificar errores que pueda arrojar esta funcion de abajo
    if (pthread_mutex_init(&bus_lock, NULL) != 0) 
    {
        write_log(1, "BUS: fallo al iniciar el mutex\n");
        return -1;
    }
    write_log(0, "BUS: Inicializado exitosamente\n");
    return 0;
}

int bus_read(int address, Word *data, int client_id) 
{
    // 1. Arbitraje: Adquirir el bus
    pthread_mutex_lock(&bus_lock);

    // 2. Realizar la operación física
    int result = mem_read_physical(address, data);

    // 3. Liberar el bus
    pthread_mutex_unlock(&bus_lock);

    /* El bus no escribe en el log para evitar mensajes duplicados
      Se delega la responsabilidad de escribir el log al cliente (CPU, DMA,
      loader) que tiene mejor contexto (PC, operación E/S, client_id, etc.)*/

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
    // Igual que en bus_read: no logueamos aquí para evitar duplicados
    return result;
}

void bus_destroy() 
{
    // Verificar errores que pueda arrojar esta funcion de abajo
    pthread_mutex_destroy(&bus_lock);
    write_log(0, "BUS: finalizado exitosamente\n");
}
