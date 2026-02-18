#ifndef KERNEL_H
#define KERNEL_H

#include "brain.h"
#include <stdbool.h>

// --- CONSTANTES ---
#define MAX_PROCESSES 20 // Máximo 20 procesos
#define QUANTUM_TICKS 2  // Requisito PDF: Round Robin q=2
#define NULL_PID -1

// Definición de particiones de memoria (Espacio Usuario: 300 - 1999)
// Estrategia: 5 particiones fijas de 340 palabras cada una.
// 1700 palabras totales / 5 particiones = 340 palabras/proceso.
// Si hay más de 5 procesos, se quedan en cola NEW esperando hueco.
#define MEM_USER_START 300
#define NUM_PARTITIONS 5
#define PARTITION_SIZE 340

// --- ESTADOS DEL PROCESO ---
typedef enum
{
    STATE_NEW,       // Recién creado, aun no en RAM
    STATE_READY,     // En RAM, listo para CPU
    STATE_RUNNING,   // Usando la CPU actualmente
    STATE_BLOCKED,   // Dormido o esperando E/S
    STATE_TERMINATED // Finalizó ejecución
} ProcessState;

// --- BLOQUE DE CONTROL DE PROCESO (PCB) ---
typedef struct
{
    int pid;            // ID del proceso (0 a 19)
    char name[50];      // Nombre del programa (ej: "prog1")
    ProcessState state; // Estado actual

    CPU_Context context; // Copia de los registros (PC, AC, SP, etc.)
    // context.RB = Inicio de partición
    // context.RL = Fin de partición (Absoluto)

    // Gestión de Memoria (Particionamiento Estático)
    int partition_id; // Qué partición ocupa (-1 si ninguna)

    // Planificación
    int quantum_counter; // Ticks consumidos en el turno actual
    int wake_time;       // Tick del reloj en el que debe despertar (para Syscall Dormir)

    // Gestión de Archivo (Simulación disco)
    int disk_track;
    int disk_sector;
    int disk_cylinder;
    int prog_size; // Tamaño en palabras

} PCB;

// --- VARIABLES GLOBALES DEL KERNEL ---
extern PCB process_table[MAX_PROCESSES];       // Tabla de procesos
extern int current_pid;                        // Quién tiene la CPU (-1 si nadie)
extern int system_ticks;                       // Reloj global del sistema
extern bool partitions_bitmap[NUM_PARTITIONS]; // Mapa de bits: 0=Libre, 1=Ocupada

// --- FUNCIONES ---

// Inicialización
void kernel_init_structures();

// Gestión de Procesos
int create_process(const char *name, int track, int sector, int size);

PCB *get_pcb(int pid);

// Utilidades
const char *state_to_string(ProcessState s);

int find_free_partition();

#endif