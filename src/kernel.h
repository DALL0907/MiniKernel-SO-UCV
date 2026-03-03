#ifndef KERNEL_H
#define KERNEL_H

#include "brain.h"
#include <stdbool.h>

// --- CONSTANTES ---
#define MAX_PROCESSES 20
#define QUANTUM_TICKS 2
#define NULL_PID -1

#define MEM_USER_START 300
#define NUM_PARTITIONS 5
#define PARTITION_SIZE 340

// --- TABLA DE ARCHIVOS (FILE TABLE) ---
#define MAX_FILE_TABLE MAX_PROCESSES

typedef enum
{
    FILE_STATE_DISK,        // Sólo en disco virtual, esperando ser cargado a RAM
    FILE_STATE_READY,       // Cargado en RAM, listo para ejecutar (partición asignada)
    FILE_STATE_RUNNING,     // Proceso en ejecución actual en CPU
    FILE_STATE_TERMINATED   // Proceso terminó, partición se liberó
} FileState;

typedef struct
{
    char program_name[50];      // Nombre del programa ("prog1.txt", "prog2", etc.)
    int track;                  // Ubicación en disco
    int cylinder;
    int sector_initial;
    int size_words;             // Tamaño en palabras
    int pid;                    // PID asignado (-1 si sólo en disco)
    int partition_id;           // ID partición RAM (-1 si sólo en disco)
    FileState state;            // Estado: DISK, READY, RUNNING, TERMINATED
    int n_start;                // Índice _start del programa
} FileTableEntry;

// --- ESTADOS DEL PROCESO ---
typedef enum
{
    STATE_NEW,       // Recién creado, aún no en RAM
    STATE_READY,     // En RAM, listo para CPU
    STATE_RUNNING,   // Usando la CPU actualmente
    STATE_BLOCKED,   // Dormido o esperando E/S
    STATE_TERMINATED // Finalizó ejecución
} ProcessState;

// --- BLOQUE DE CONTROL DE PROCESO (PCB) ---
typedef struct
{
    int pid;            // ID del proceso (0 a 19)
    char name[50];      // Nombre del programa
    ProcessState state; // Estado actual

    CPU_Context context; // Registros (PC, AC, SP, etc.)

    // Gestión de Memoria
    int partition_id;   // Qué partición ocupa (-1 si ninguna)

    // Planificación
    int quantum_counter; // Ticks consumidos en el turno actual
    int wake_time;       // Tick en el que debe despertar

    // Gestión de Archivo (Simulación disco)
    int disk_track;
    int disk_sector;
    int disk_cylinder;
    int prog_size;      // Tamaño en palabras

} PCB;

// --- VARIABLES GLOBALES DEL KERNEL ---
extern PCB process_table[MAX_PROCESSES];
extern FileTableEntry file_table[MAX_FILE_TABLE];
extern int file_table_count;
extern int current_pid;
extern int system_ticks;
extern bool partitions_bitmap[NUM_PARTITIONS];

// --- FUNCIONES ---

// Inicialización
void kernel_init_structures();

// Gestión de Procesos
int create_process(const char *name, int track, int cylinder, int sector, int size);
PCB *get_pcb(int pid);

// Gestión de Tabla de Archivos
int file_table_search_by_name(const char *program_name);  // Busca por nombre, retorna índice o -1
int file_table_find_by_pid(int pid);                      // Busca por PID, retorna índice o -1
FileTableEntry* get_file_table_entry(int index);          // Obtiene puntero a entrada válida
int file_table_add_entry(const char *program_name, int track, int cylinder, int sector, int size, int n_start);

// Utilidades
const char *state_to_string(ProcessState s);
int find_free_partition();

// Manejo de interrupciones
void kernel_handle_interrupt(int interrupt_code);

#endif // KERNEL_H