#include "dma.h"
#include "brain.h"
#include "bus.h"
#include "cpu.h"
#include "disk.h"
#include "log.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DMA_t dma;
static int dma_initialized = 0;    // Singleton (lo logico es que se trabaje con una sola instancia)
static pthread_t dma_thread;       // Hilo
static int dma_thread_running = 0; // Controla si el hilo está activo
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
* 3. dma_perform_io realiza la operacion de E/S
*/

int dma_init()
{
    if (dma_initialized)
    {
        write_log(0, "DMA: ya inicializado\n"); // Para simular una especie de singleton
        return 0;
    }
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
    dma.BUSY = 0;
    dma_thread_running = 0;
    dma_initialized = 1;
    write_log(0, "DMA: inicializado exitosamente\n");
    return 0;
}

int dma_handler(int opcode, int value, unsigned int mode)
{
    if (!dma_initialized)
    {
        write_log(1, "DMA: no inicializado\n");
        return -1;
    }
    pthread_mutex_lock(&dma.lock);
    // Aquí creo que deberia ir la comprobacion de que el DMA no esté ocupado
    /*if (dma.BUSY)
        {
            write_log(1, "DMA: (Handler) ERROR - DMA ocupado. Espere a que termine la transferencia actual\n");
            pthread_mutex_unlock(&dma.lock);
            return 99;
        }*/
    switch (opcode)
    {
    case OP_SDMAP:
        dma.TRACK = value;
        write_log(0, "DMA: (Handler) Pista establecida en %d\n", value);
        break;
    case OP_SDMAC:
        dma.CYLINDER = value;
        write_log(0, "DMA: (Handler) Cilindro establecido en %d\n", value);
        break;
    case OP_SDMAS:
        dma.SECTOR = value;
        write_log(0, "DMA: (Handler) Sector establecido en %d\n", value);
        break;
    case OP_SDMAIO:
        dma.IO = value;
        write_log(0, "DMA: (Handler) Modo de operación establecido en %d (0 = leer, 1 = escribir)\n", value);
        break;
    case OP_SDMAM:
        dma.ADDRESS = value;
        write_log(0, "DMA: (Handler) Dirección de memoria establecida en %d\n", value);
        break;
    case OP_SDMAON:
        // Iniciar la operacion de E/S
        // Verificar que el DMA no esté ocupado
        if (dma.BUSY)
        {
            write_log(1, "DMA: (Handler) ERROR - DMA ocupado. Espere a que termine la transferencia actual\n");
            pthread_mutex_unlock(&dma.lock);
            return 99;
        }

        // Validar dirección de memoria
        if (dma.ADDRESS < 0 || dma.ADDRESS >= MEM_SIZE)
        {
            write_log(1, "DMA: (Handler) ERROR - Dirección de memoria inválida: %d (rango válido: 0-%d)\n", dma.ADDRESS, MEM_SIZE - 1);
            pthread_mutex_unlock(&dma.lock);
            return -1;
        }
        if (mode == USER_MODE && dma.ADDRESS < OS_RESERVED)
        {
            write_log(1, "DMA: (Handler) ERROR - Intento de acceso a memoria reservada por el sistema.\n");
            pthread_mutex_unlock(&dma.lock);
            return -1;
        }

        // Validar parámetros del disco
        if (dma.TRACK < 0 || dma.TRACK >= DISK_TRACKS || dma.CYLINDER < 0 || dma.CYLINDER >= DISK_CYLINDERS || dma.SECTOR < 0 || dma.SECTOR >= DISK_SECTORS)
        {
            write_log(1, "DMA: (Handler) Error - Parámetros del disco inválidos\n");
            pthread_mutex_unlock(&dma.lock);
            return -1;
        }
        // Preparando el DMA para una nueva operacion
        write_log(0, "DMA: (Handler) Iniciando operación E/S asíncrona...\n");

        dma.BUSY = 1;           // DMA ahora está ocupado
        dma_thread_running = 1; // Hilo estará ejecutándose
        dma.STATE = 1;          // Estado inicial = 1 (error/operación en curso)

        // Liberar mutex antes de crear el hilo (evita bloquear el mutex durante la creación del hilo)
        pthread_mutex_unlock(&dma.lock);

        // Crear el hilo para realizar la operación de E/S
        if (pthread_create(&dma_thread, NULL, dma_perform_io, NULL) != 0)
        {
            write_log(1, "DMA: (Handler) FATAL No se pudo crear hilo de transferencia\n");

            // Recuperar mutex para limpiar estado (ya que falló la creación)
            pthread_mutex_lock(&dma.lock);
            dma.BUSY = 0;           // Liberar DMA
            dma_thread_running = 0; // Hilo no está ejecutándose
            dma.STATE = 1;          // Estado permanece en error
            pthread_mutex_unlock(&dma.lock);

            return -1;
        }
        //write_log(0, "DMA: (Handler) Transferencia iniciada en segundo plano (hilo creado)\n");
        usleep(2);
        return 0; // Éxito - hilo creado correctamente
        break;
    default:
        write_log(1, "DMA: (Handler) ERROR - Código de operación desconocido: %d\n", opcode);
        pthread_mutex_unlock(&dma.lock);
        return -1;
    }
    pthread_mutex_unlock(&dma.lock);
    return 0;
}

void *dma_perform_io(void *arg)
{
    char buffer[SECTOR_BYTES]; // Buffer para datos del disco (9 bytes = 8 dígitos + '\0')
    int result;                // Variable para resultados de operaciones de disco
    
    write_log(0, "DMA: Hilo de transferencia iniciado\n");

    // Simular Latencia de Disco
    usleep(20000);

    // 1. Adquirir exclusión mutua
    pthread_mutex_lock(&dma.lock);

    // 2. Validar parámetros
    if (dma.TRACK < 0 || dma.TRACK >= DISK_TRACKS ||
        dma.CYLINDER < 0 || dma.CYLINDER >= DISK_CYLINDERS ||
        dma.SECTOR < 0 || dma.SECTOR >= DISK_SECTORS)
    {
        write_log(1, "DMA: ERROR - Parámetros de disco inválidos.\n");

        dma.STATE = 1; // 1 = error
        dma.BUSY = 0;  // Liberar el DMA para nuevas operaciones

        pthread_mutex_unlock(&dma.lock);
        dma_thread_running = 0;
        return NULL;
    }

    write_log(0, "DMA: Operación E/S con parámetros - PISTA=%d, CILINDRO=%d, SECTOR=%d, IO=%d, ADDRESS=%d\n",
              dma.TRACK, dma.CYLINDER, dma.SECTOR, dma.IO, dma.ADDRESS);

    // CASO A: memoria -> disco (IO = 0)
    if (dma.IO == 0)
    {
        Word w; // Variable para almacenar la palabra de memoria (8 dígitos)

        // A.1 Leer la memoria
        // client_id = 1 indica que es el DMA quien hace la petición al bus
        if (bus_read(dma.ADDRESS, &w, 1) != 0)
        {
            write_log(1, "DMA: ERROR - Fallo al leer de memoria en dirección %d\n", dma.ADDRESS);

            dma.STATE = 1;
            dma.BUSY = 0;

            pthread_mutex_unlock(&dma.lock);
            dma_thread_running = 0;
            return NULL;
        }

        write_log(0, "DMA: Palabra de memoria leída. Valor: %d\n", w);

        // A.2 Formatear para disco
        // Convertir el número entero a cadena de caracteres
        snprintf(buffer, SECTOR_BYTES, "%0*d", WORD_DIGITS, w);
        buffer[SECTOR_BYTES - 1] = '\0'; // Asegurar terminación nula

        write_log(0, "DMA: Formateado para disco. Cadena: \"%s\"\n", buffer);

        // A.3 Escribir en disco
        result = disk_write_sector(dma.TRACK, dma.CYLINDER, dma.SECTOR, buffer);
        if (result != 0)
        {
            write_log(1, "DMA: ERROR - Fallo al escribir en disco. PISTA=%d, CILINDRO=%d, SECTOR=%d\n",
                      dma.TRACK, dma.CYLINDER, dma.SECTOR);

            dma.STATE = 1;
            dma.BUSY = 0; // Liberar el DMA

            pthread_mutex_unlock(&dma.lock);
            dma_thread_running = 0;
            return NULL;
        }

        dma.STATE = 0; // éxito

        write_log(0, "DMA: ÉXITO - Transferencia Memoria->Disco completada. "
                     "Word=%d escrito en sector \"%s\"\n",
                  w, buffer);
    }

    // CASO B: disco -> memoria (IO = 1)
    else
    {
        // Inicializar buffer
        memset(buffer, 0, sizeof(buffer));

        // B.1 Leer disco
        result = disk_read_sector(dma.TRACK, dma.CYLINDER, dma.SECTOR, buffer);
        if (result != 0)
        {
            write_log(1, "DMA: ERROR - Fallo al leer del disco. PISTA=%d, CILINDRO=%d, SECTOR=%d\n",
                      dma.TRACK, dma.CYLINDER, dma.SECTOR);

            dma.STATE = 1;
            dma.BUSY = 0;

            pthread_mutex_unlock(&dma.lock);
            dma_thread_running = 0;
            return NULL;
        }

        // Asegurar que se devuelvan los 9 caracteres (8 dígitos + '\0')
        buffer[SECTOR_BYTES - 1] = '\0';

        write_log(0, "DMA: Leído sector del disco. Contenido: \"%s\"\n", buffer);

        // B.2 Convertir la cadena almacenada en el disco a un entero
        int val = atoi(buffer);

        write_log(0, "DMA: Convertido a entero. Valor: %d\n", val);

        // B.3 Escribir en memoria
        if (bus_write(dma.ADDRESS, (Word)val, 1) != 0)
        {
            write_log(1, "DMA: ERROR - Fallo al escribir en memoria en dirección %d\n", dma.ADDRESS);

            dma.STATE = 1;
            dma.BUSY = 0;

            pthread_mutex_unlock(&dma.lock);
            dma_thread_running = 0;
            return NULL;
        }

        dma.STATE = 0; // éxito en la operación E/S

        write_log(0, "DMA: ÉXITO - Transferencia Disco->Memoria completada. "
                     "Sector \"%s\" escrito en dirección %d como valor %d\n",
                  buffer, dma.ADDRESS, val);
    }

    // 3. Completar operación y limpiar estado
    dma.BUSY = 0; // DMA ahora está libre para nuevas operaciones

    // Liberar el mutex antes de enviar la interrupción (evita bloquear el mutex mientras se notifica a la CPU)
    pthread_mutex_unlock(&dma.lock);

    // 4. Simular retardo de transferencia
    //sleep(1);

    // 5. Notificar a la CPU
    write_log(0, "DMA: Operación finalizada. Estado: %d (0=éxito, 1=error).\n", dma.STATE);

    cpu_interrupt(INT_IO_END);

    // 6. Finalizar hilo
    dma_thread_running = 0;

    write_log(0, "DMA: Hilo de transferencia finalizado correctamente\n");
    return NULL;
}

int dma_get_state()
{
    if (!dma_initialized)
    {
        write_log(1, "DMA: intento de leer estado sin inicializacion\n");
        return 1;
    }

    pthread_mutex_lock(&dma.lock);
    int state = dma.STATE;
    pthread_mutex_unlock(&dma.lock);

    return state;
}

int dma_is_busy()
{
    if (!dma_initialized)
    {
        return 0;
    }
    pthread_mutex_lock(&dma.lock);
    int busy = dma.BUSY;
    pthread_mutex_unlock(&dma.lock);

    return busy;
}

void dma_destroy()
{
    if (!dma_initialized)
        return;

    // Si hay un hilo corriendo, esperar a que termine
    if (dma_thread_running)
    {
        write_log(0, "DMA: Esperando a que termine el hilo de transferencia...\n");

        void *thread_result;
        if (pthread_join(dma_thread, &thread_result) != 0)
        {
            write_log(1, "DMA: Error en pthread_join\n");
        }
        else
        {
            write_log(0, "DMA: Hilo terminado correctamente\n");
        }
    }

    pthread_mutex_destroy(&dma.lock);

    // Marcar como no inicializado
    dma_initialized = 0;
    dma_thread_running = 0;

    write_log(0, "DMA: finalizado exitosamente\n");
}
