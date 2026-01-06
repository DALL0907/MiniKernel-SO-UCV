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

static DMA_t dma;
static int dma_initialized =
    0; // Singleton (lo logico es que se trabaje con una sola instancia)
static pthread_t dma_thread;       // Hilo
static int dma_thread_running = 0; // Controla si el hilo está activo

int dma_init() {
  if (dma_initialized) {
    write_log(
        0, "DMA: ya inicializado\n"); // Para simular una especie de singleton
    return 0;
  }
  if (pthread_mutex_init(&dma.lock, NULL) != 0) {
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
   * 3. dma_perform_io realiza la operacion de E/S
   */
  if (!dma_initialized) {
    write_log(1, "DMA: no inicializado\n");
    return -1;
  }
  pthread_mutex_lock(&dma.lock);

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
    // Verificar que el DMA no esté ocupado
    if (dma.BUSY) {
      write_log(1, "DMA: ERROR - DMA ocupado. Espere a que termine la "
                   "transferencia actual\n");
      pthread_mutex_unlock(&dma.lock);
      return -1;
    }

    // --------------------------------------------------
    // VALIDACIÓN EXHAUSTIVA DE PARÁMETROS
    // --------------------------------------------------

    // Validar dirección de memoria
    if (dma.ADDRESS < 0 || dma.ADDRESS >= MEM_SIZE) {
      write_log(1,
                "DMA: ERROR - Dirección de memoria inválida: %d "
                "(rango válido: 0-%d)\n",
                dma.ADDRESS, MEM_SIZE - 1);
      pthread_mutex_unlock(&dma.lock);
      return -1;
    }

    // Validar parámetros del disco
    if (dma.TRACK < 0 || dma.TRACK >= DISK_TRACKS) {
      write_log(1, "DMA: ERROR - Track inválido: %d (rango válido: 0-%d)\n",
                dma.TRACK, DISK_TRACKS - 1);
      pthread_mutex_unlock(&dma.lock);
      return -1;
    }

    if (dma.CYLINDER < 0 || dma.CYLINDER >= DISK_CYLINDERS) {
      write_log(1, "DMA: ERROR - Cylinder inválido: %d (rango válido: 0-%d)\n",
                dma.CYLINDER, DISK_CYLINDERS - 1);
      pthread_mutex_unlock(&dma.lock);
      return -1;
    }

    if (dma.SECTOR < 0 || dma.SECTOR >= DISK_SECTORS) {
      write_log(1, "DMA: ERROR - Sector inválido: %d (rango válido: 0-%d)\n",
                dma.SECTOR, DISK_SECTORS - 1);
      pthread_mutex_unlock(&dma.lock);
      return -1;
    }

    // --------------------------------------------------
    // CONFIGURACIÓN INICIAL PARA NUEVA TRANSFERENCIA
    // --------------------------------------------------
    write_log(0, "DMA: Iniciando operación E/S asíncrona...\n");

    // Estado inicial según requisitos
    dma.BUSY = 1;           // DMA ahora está ocupado
    dma_thread_running = 1; // Hilo estará ejecutándose
    dma.STATE = 1;          // Estado inicial = 1 (error/operación en curso)
                            // Cambiará a 0 solo si toda la operación es exitosa

    // Liberar mutex ANTES de crear el hilo
    // (evita bloquear el mutex durante la creación del hilo)
    pthread_mutex_unlock(&dma.lock);

    // --------------------------------------------------
    // CREAR HILO PARA TRANSFERENCIA ASÍNCRONA
    // --------------------------------------------------
    // pthread_create requiere:
    // 1. Puntero a pthread_t (identificador del hilo)
    // 2. Atributos (NULL = valores por defecto)
    // 3. Función a ejecutar (dma_perform_io)
    // 4. Argumento para la función (NULL en este caso)
    if (pthread_create(&dma_thread, NULL, dma_perform_io, NULL) != 0) {
      write_log(
          1, "DMA: ERROR CRÍTICO - No se pudo crear hilo de transferencia\n");

      // Recuperar mutex para limpiar estado (ya que falló la creación)
      pthread_mutex_lock(&dma.lock);
      dma.BUSY = 0;           // Liberar DMA
      dma_thread_running = 0; // Hilo no está ejecutándose
      dma.STATE = 1;          // Estado permanece en error
      pthread_mutex_unlock(&dma.lock);

      return -1;
    }

    write_log(0,
              "DMA: Transferencia iniciada en segundo plano (hilo creado)\n");
    write_log(0,
              "DMA: CPU puede continuar ejecución mientras DMA trabaja...\n");
    return 0; // Éxito - hilo creado correctamente
    break;
  default:
    write_log(1, "DMA: ERROR - Código de operación desconocido: %d\n", opcode);
    pthread_mutex_unlock(&dma.lock);
    return -1;
  }
  pthread_mutex_unlock(&dma.lock);
  return 0;
}

// ============================================
// FUNCIÓN: dma_perform_io(void* arg)
// ============================================
// Descripción: Función principal que realiza la operación de E/S.
//              Esta función es el "entry point" del hilo pthread.
//              Cumple con los requisitos: establece dma.STATE = 0 si éxito, 1 si error.
// Parámetros: void* arg (no utilizado en esta implementación, requerido por pthread)
// Retorno: void* (NULL en todos los casos, requerido por pthread)
// ============================================
void* dma_perform_io(void* arg) {
    char buffer[SECTOR_BYTES];  // Buffer para datos del disco (9 bytes = 8 dígitos + '\0')
    int res;                     // Variable para resultados de operaciones de disco
    
    write_log(0, "DMA: Hilo de transferencia iniciado\n");
    
    // ============================================
    // PASO 1: ADQUIRIR EXCLUSIÓN MUTUA
    // ============================================
    pthread_mutex_lock(&dma.lock);
    
    // ============================================
    // PASO 2: VALIDACIÓN INICIAL DE PARÁMETROS
    // ============================================
    if (dma.TRACK < 0 || dma.TRACK >= DISK_TRACKS ||
        dma.CYLINDER < 0 || dma.CYLINDER >= DISK_CYLINDERS ||
        dma.SECTOR < 0 || dma.SECTOR >= DISK_SECTORS) {
        
        write_log(1, "DMA: ERROR - Parámetros de disco inválidos. "
                    "Track=%d (max %d), Cylinder=%d (max %d), Sector=%d (max %d)\n",
                    dma.TRACK, DISK_TRACKS-1, dma.CYLINDER, DISK_CYLINDERS-1, 
                    dma.SECTOR, DISK_SECTORS-1);
        
        // REQUISITO: Establecer estado a 1 (error)
        dma.STATE = 1;    // 1 = error
        dma.BUSY = 0;     // Liberar el DMA para nuevas operaciones
        
        pthread_mutex_unlock(&dma.lock);
        dma_thread_running = 0;
        return NULL;
    }

    write_log(0, "DMA: Iniciando operación E/S. "
                "TRACK=%d, CYLINDER=%d, SECTOR=%d, IO=%d, ADDRESS=%d\n",
                dma.TRACK, dma.CYLINDER, dma.SECTOR, dma.IO, dma.ADDRESS);

    // ============================================
    // CASO A: MEMORIA -> DISCO (IO = 0)
    // ============================================
    if (dma.IO == 0) {
        Word w;  // Variable para almacenar la palabra de memoria (8 dígitos)
        
        // --------------------------------------------------
        // SUBPASO A.1: LEER DE MEMORIA
        // --------------------------------------------------
        // client_id = 1 indica que es el DMA quien hace la petición al bus
        if (bus_read(dma.ADDRESS, &w, 1) != 0) {
            write_log(1, "DMA: ERROR - Fallo al leer de memoria en dirección %d\n", dma.ADDRESS);
            
            // REQUISITO: Establecer estado a 1 (error)
            dma.STATE = 1;    // 1 = error
            dma.BUSY = 0;     // Liberar el DMA
            
            pthread_mutex_unlock(&dma.lock);
            dma_thread_running = 0;
            return NULL;
        }
        
        write_log(0, "DMA: Leída palabra de memoria. Valor: %d\n", w);
        
        // --------------------------------------------------
        // SUBPASO A.2: FORMATEAR PARA DISCO
        // --------------------------------------------------
        // Convertir el número entero a cadena de 8 dígitos con ceros a la izquierda
        // Ejemplo: w=42 -> "00000042"
        snprintf(buffer, SECTOR_BYTES, "%0*d", WORD_DIGITS, w);
        buffer[SECTOR_BYTES - 1] = '\0';  // Asegurar terminación nula
        
        write_log(0, "DMA: Formateado para disco. Cadena: \"%s\"\n", buffer);
        
        // --------------------------------------------------
        // SUBPASO A.3: ESCRIBIR EN DISCO
        // --------------------------------------------------
        res = disk_write_sector(dma.TRACK, dma.CYLINDER, dma.SECTOR, buffer);
        if (res != 0) {
            write_log(1, "DMA: ERROR - Fallo al escribir en disco. "
                        "Track=%d, Cylinder=%d, Sector=%d\n",
                        dma.TRACK, dma.CYLINDER, dma.SECTOR);
            
            // REQUISITO: Establecer estado a 1 (error)
            dma.STATE = 1;    // 1 = error
            dma.BUSY = 0;     // Liberar el DMA
            
            pthread_mutex_unlock(&dma.lock);
            dma_thread_running = 0;
            return NULL;
        }
        
        // ============================================
        // ÉXITO COMPLETO: MEMORIA -> DISCO
        // ============================================
        // REQUISITO: Establecer estado a 0 (éxito)
        dma.STATE = 0;    // 0 = éxito
        
        write_log(0, "DMA: ÉXITO - Transferencia Memoria->Disco completada. "
                    "Word=%d escrito en sector \"%s\"\n", w, buffer);
    }
    // ============================================
    // CASO B: DISCO -> MEMORIA (IO = 1)
    // ============================================
    else {
        // Inicializar buffer
        memset(buffer, 0, sizeof(buffer));
        
        // --------------------------------------------------
        // SUBPASO B.1: LEER DEL DISCO
        // --------------------------------------------------
        res = disk_read_sector(dma.TRACK, dma.CYLINDER, dma.SECTOR, buffer);
        if (res != 0) {
            write_log(1, "DMA: ERROR - Fallo al leer del disco. "
                        "Track=%d, Cylinder=%d, Sector=%d\n",
                        dma.TRACK, dma.CYLINDER, dma.SECTOR);
            
            // REQUISITO: Establecer estado a 1 (error)
            dma.STATE = 1;    // 1 = error
            dma.BUSY = 0;     // Liberar el DMA
            
            pthread_mutex_unlock(&dma.lock);
            dma_thread_running = 0;
            return NULL;
        }
        
        // Asegurar terminación nula (el disco siempre devuelve 9 bytes)
        buffer[SECTOR_BYTES - 1] = '\0';
        
        write_log(0, "DMA: Leído sector del disco. Contenido: \"%s\"\n", buffer);
        
        // --------------------------------------------------
        // SUBPASO B.2: CONVERTIR A ENTERO
        // --------------------------------------------------
        // atoi() convierte cadena a entero, maneja ceros a la izquierda
        // Ejemplo: "00000042" -> 42
        int val = atoi(buffer);
        
        write_log(0, "DMA: Convertido a entero. Valor: %d\n", val);
        
        // --------------------------------------------------
        // SUBPASO B.3: ESCRIBIR EN MEMORIA
        // --------------------------------------------------
        if (bus_write(dma.ADDRESS, (Word)val, 1) != 0) {
            write_log(1, "DMA: ERROR - Fallo al escribir en memoria en dirección %d\n", dma.ADDRESS);
            
            // REQUISITO: Establecer estado a 1 (error)
            dma.STATE = 1;    // 1 = error
            dma.BUSY = 0;     // Liberar el DMA
            
            pthread_mutex_unlock(&dma.lock);
            dma_thread_running = 0;
            return NULL;
        }
        
        // ============================================
        // ÉXITO COMPLETO: DISCO -> MEMORIA
        // ============================================
        // REQUISITO: Establecer estado a 0 (éxito)
        dma.STATE = 0;    // 0 = éxito
        
        write_log(0, "DMA: ÉXITO - Transferencia Disco->Memoria completada. "
                    "Sector \"%s\" escrito en dirección %d como valor %d\n", 
                    buffer, dma.ADDRESS, val);
    }
    
    // ============================================
    // PASO 3: OPERACIÓN COMPLETADA - LIMPIAR ESTADO
    // ============================================
    dma.BUSY = 0;  // DMA ahora está libre para nuevas operaciones
    
    // Liberar el mutex ANTES de enviar la interrupción
    // (evita bloquear el mutex mientras se notifica a la CPU)
    pthread_mutex_unlock(&dma.lock);
    
    // ============================================
    // PASO 4: SIMULAR TIEMPO REAL DE TRANSFERENCIA
    // ============================================
    // En un sistema real, la transferencia tomaría tiempo real
    // sleep(1) simula ese tiempo (1 segundo para pruebas)
    // Puede ajustarse o eliminarse según necesidades
    sleep(1);
    
    // ============================================
    // PASO 5: NOTIFICAR A LA CPU
    // ============================================
    // REQUISITO: "Luego, interrumpe al procesador."
    write_log(0, "DMA: Operación finalizada. Estado: %d (0=éxito, 1=error). "
                "Enviando interrupción INT_IO_END a CPU\n", dma.STATE);
    
    cpu_interrupt(INT_IO_END);  // Código de interrupción 4 según brain.h
    
    // ============================================
    // PASO 6: FINALIZAR HILO
    // ============================================
    dma_thread_running = 0;  // Hilo ya no está ejecutándose
    
    write_log(0, "DMA: Hilo de transferencia finalizado correctamente\n");
    return NULL;
}

int dma_get_state() {
  if (!dma_initialized) {
    write_log(1, "DMA: intento de leer estado sin inicializacion\n");
    return 1;
  }

  pthread_mutex_lock(&dma.lock);
  int state = dma.STATE;
  pthread_mutex_unlock(&dma.lock);

  return state;
}

int dma_is_busy() {
  if (!dma_initialized) {
    return 0;
  }

  pthread_mutex_lock(&dma.lock);
  int busy = dma.BUSY;
  pthread_mutex_unlock(&dma.lock);

  return busy;
}

void dma_destroy() {
  if (!dma_initialized)
    return;

  // Si hay un hilo corriendo, esperar a que termine
  if (dma_thread_running) {
    write_log(0, "DMA: Esperando a que termine el hilo de transferencia...\n");

    void *thread_result;
    if (pthread_join(dma_thread, &thread_result) != 0) {
      write_log(1, "DMA: Error en pthread_join\n");
    } else {
      write_log(0, "DMA: Hilo terminado correctamente\n");
    }
  }

  pthread_mutex_destroy(&dma.lock);

  // Marcar como no inicializado
  dma_initialized = 0;
  dma_thread_running = 0;

  write_log(0, "DMA: finalizado exitosamente\n");
}
