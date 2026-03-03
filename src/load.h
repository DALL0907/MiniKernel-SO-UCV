#ifndef LOAD_H
#define LOAD_H

#include "brain.h"

typedef struct
{
    int index_start;  // posición o índice del _start
    int n_words;      // palabras que se leyeron
    int load_address; // dirección donde se cargo en RAM (USADO SOLO EN RAM)
} loadParams;

/**
 * CARGAR DE PC REAL -> DISCO VIRTUAL
 * 
 * Lee un archivo .txt desde la PC real y lo carga completamente en disco
 * El programa queda en estado NEW en disco sin ocupar RAM
 * 
 * Se llama desde: comando CARGAR
 * 
 * Parámetros:
 *   filename: Ruta del archivo en PC real (ej: "Casos de prueba/prog1.txt")
 *   program_name: Nombre a asignar en tabla de archivos (ej: "prog1.txt")
 *   track, cylinder, sector: Ubicación inicial en disco virtual
 * 
 * Retorna: PID del proceso creado si éxito, -1 si error
 */
int load_program_to_disk(const char *filename, const char *program_name,
                          int track, int cylinder, int sector);

/**
 * CARGAR DE DISCO VIRTUAL -> RAM
 * 
 * Lee un programa desde el disco virtual y lo carga EN RAM dentro de una partición
 * Inicializa contexto del proceso (PC, SP, RB, RL, etc.)
 * El programa queda en estado READY, listo para ejecutar
 * 
 * Se llama desde: comando EJECUTAR (cuando se cambia de NEW a READY)
 * 
 * Parámetros:
 *   pid: PID del proceso a cargar en RAM
 *   partition_id: ID de la partición asignada (0-4)
 *   file_table_index: Índice en la tabla de archivos
 * 
 * Retorna: 0 si éxito, -1 si error
 */
int load_program_to_ram(int pid, int partition_id, int file_table_index);

#endif // LOAD_H