#include "kernel.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

// Definición de variables globales
PCB process_table[MAX_PROCESSES];
int current_pid = NULL_PID;
int system_ticks = 0;
bool partitions_bitmap[NUM_PARTITIONS];

// --- COLA DE LISTOS (READY QUEUE) ---
int ready_queue[MAX_PROCESSES];
int rq_head = 0;
int rq_aux = 0;
int rq_count = 0;

// Encola un proceso al final de la cola de listos
void enqueue_ready(int pid)
{
    if (rq_count < MAX_PROCESSES)
    {
        ready_queue[rq_aux] = pid;
        rq_aux = (rq_aux + 1) % MAX_PROCESSES;
        rq_count++;
        process_table[pid].state = STATE_READY;
    }
    else
    {
        write_log(1, "KERNEL PANIC: Cola de listos desbordada.\n");
    }
}

// Desencola el primer proceso de la cola de listos
int dequeue_ready()
{
    if (rq_count == 0)
        return NULL_PID;

    int pid = ready_queue[rq_head];
    rq_head = (rq_head + 1) % MAX_PROCESSES;
    rq_count--;
    return pid;
}

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

extern void dispatch(int nuevo_pid);

void schedule()
{
    int outgoing_pid = current_pid;

    // Manejar el proceso actual (si hay uno)
    if (outgoing_pid != NULL_PID)
    {
        if (process_table[outgoing_pid].state == STATE_RUNNING)
        {
            // Si estaba corriendo y se llamó al scheduler, es porque se le acabó el quantum.
            // Lo devolvemos a la cola de listos.
            enqueue_ready(outgoing_pid);
        }
    }

    // Seleccionar el siguiente proceso
    int incoming_pid = dequeue_ready();

    if (incoming_pid == NULL_PID)
    {
        // No hay más procesos en la cola
        if (outgoing_pid != NULL_PID && process_table[outgoing_pid].state == STATE_READY)
        {
            // El único que hay es el mismo. Lo volvemos a sacar.
            incoming_pid = dequeue_ready();
        }
        else
        {
            // La CPU se queda ociosa
            current_pid = NULL_PID;
            return;
        }
    }

    // REGISTRO EN EL LOG
    if (outgoing_pid != incoming_pid && outgoing_pid != NULL_PID)
    {
        write_log(0, "PLANIFICADOR: Quantum agotado. Sale PID %d (%s), Entra PID %d (%s)\n",
                  outgoing_pid, process_table[outgoing_pid].name,
                  incoming_pid, process_table[incoming_pid].name);
    }

    // Reiniciar su contador de quantum y despacharlo
    process_table[incoming_pid].quantum_counter = 0;
    dispatch(incoming_pid);
}

// Atiende las interrupciones desde el punto de vista del Sistema Operativo
void kernel_handle_interrupt(int interrupt_code)
{
    if (current_pid == NULL_PID)
        return;
    // --- MANEJO DE RELOJ (Round Robin) --- //si ocurre es porque se le acabo su tiempo a un proceso
    if (interrupt_code == INT_CLOCK)
    {
        system_ticks++;
        process_table[current_pid].quantum_counter++;

        if (process_table[current_pid].quantum_counter >= QUANTUM_TICKS)
        {
            write_log(0, "KERNEL: PID %d agotó su quantum de %d ticks.\n", current_pid, QUANTUM_TICKS);
            schedule();
        }
    }
    // --- MANEJO DE ERRORES FATALES ---
    else if (interrupt_code == INT_INV_ADDR || interrupt_code == INT_UNDERFLOW ||
             interrupt_code == INT_OVERFLOW || interrupt_code == INT_INV_INSTR)
    {

        write_log(1, "KERNEL: Error fatal (Cod %d) en PID %d. Terminando proceso.\n",
                  interrupt_code, current_pid);

        // 1. Cambiamos el estado a TERMINADO
        process_table[current_pid].state = STATE_TERMINATED;

        // 2. Liberamos su memoria en el mapa de particiones
        if (process_table[current_pid].partition_id != -1)
        {
            partitions_bitmap[process_table[current_pid].partition_id] = false;
        }

        // 3. Llamamos al planificador para que meta al siguiente proceso
        schedule();
    }
    // --- CASO DMA ERROR ---
    else if (interrupt_code == INT_IO_END)
    {
        // Aquí luego agregaremos la lógica de desbloquear procesos que esperaban disco
        int dma_state = dma_get_state();
        if (dma_state != 0)
        {
            write_log(1, "KERNEL: Fallo crítico de DMA en PID %d. Terminando proceso.\n", current_pid);
            process_table[current_pid].state = STATE_TERMINATED;
            if (process_table[current_pid].partition_id != -1)
            {
                partitions_bitmap[process_table[current_pid].partition_id] = false;
            }
            schedule();
        }
    }
}
