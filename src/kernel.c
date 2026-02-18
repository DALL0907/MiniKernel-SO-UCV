#include "kernel.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

// Definición de variables globales
PCB process_table[MAX_PROCESSES];
int current_pid = NULL_PID;
int system_ticks = 0;
bool partitions_bitmap[NUM_PARTITIONS];

// Inicializa todas las tablas en 0/Vacío
void kernel_init_structures()
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        process_table[i].pid = -1;                 // -1 indica que no tiene aun
        process_table[i].state = STATE_TERMINATED; // por defeto TERMINATED para sencillez
        process_table[i].partition_id = -1;        // No asignada a ninguna partición
    }

    // Inicializar particiones como libres
    for (int i = 0; i < NUM_PARTITIONS; i++)
    {
        partitions_bitmap[i] = false;
    }

    current_pid = NULL_PID;
    system_ticks = 0;

    write_log(0, "KERNEL: Estructuras de datos inicializadas (Tabla de Procesos y Mapa de Memoria).\n");
}

// Busca un slot libre en la tabla y crea un PCB básico (Estado NEW)
// Retorna el PID asignado o -1 si la tabla está llena.
int create_process(const char *name, int track, int cylinder, int sector, int size)
{
    int free_slot = -1;

    // 1. Buscar slot en la tabla de procesos (Máximo 20)
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_table[i].pid == -1 || process_table[i].state == STATE_TERMINATED)
        {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1)
    {
        write_log(1, "KERNEL ERROR: Tabla de procesos llena. No se puede crear '%s'.\n", name);
        return -1;
    }

    // 2. Inicializar PCB
    PCB *new_proc = &process_table[free_slot];

    new_proc->pid = free_slot; // Usamos el índice como PID
    strncpy(new_proc->name, name, 49);
    new_proc->state = STATE_NEW; // Inicialmente NEW (no cargado en RAM)

    // Datos de disco (para cargarlo más tarde)
    new_proc->disk_track = track;
    new_proc->disk_cylinder = cylinder;
    new_proc->disk_sector = sector;
    new_proc->prog_size = size;

    // Datos de memoria (Aún no asignados)
    new_proc->base_address = -1;
    new_proc->limit_address = -1;
    new_proc->partition_id = -1;

    // Contexto (Se llenará cuando pase a READY)
    memset(&new_proc->context, 0, sizeof(CPU_Context));

    write_log(0, "KERNEL: Proceso creado PID=%d, (%s) en estado NEW.\n", free_slot, name);
    return free_slot;
}

// Retorna puntero al PCB o NULL si error
PCB *get_pcb(int pid)
{
    if (pid < 0 || pid >= MAX_PROCESSES)
        return NULL;
    if (process_table[pid].pid == -1)
        return NULL;
    return &process_table[pid];
}

// Busca una partición de memoria libre
// Retorna el ID de partición (0-4) o -1 si no hay memoria.
int find_free_partition()
{
    for (int i = 0; i < NUM_PARTITIONS; i++)
    {
        if (!partitions_bitmap[i])
        {
            return i;
        }
    }
    return -1; // Memoria llena
}

const char *state_to_string(ProcessState s)
{
    switch (s)
    {
    case STATE_NEW:
        return "NEW";
    case STATE_READY:
        return "READY";
    case STATE_RUNNING:
        return "RUNNING";
    case STATE_BLOCKED:
        return "BLOCKED";
    case STATE_TERMINATED:
        return "TERMINATED";
    default:
        return "UNKNOWN";
    }
}