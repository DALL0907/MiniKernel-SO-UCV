#include "brain.h"
#include "cpu.h"
#include "bus.h"
#include "kernel.h"
#include "log.h"
#include <string.h>
#include <stdio.h>
#include "dma.h"

// --- DEFINICIÓN DE VARIABLES GLOBALES ---
PCB process_table[MAX_PROCESSES];
FileTableEntry file_table[MAX_FILE_TABLE];
int file_table_count = 0;
int current_pid = NULL_PID;
int system_ticks = 0;
bool partitions_bitmap[NUM_PARTITIONS];

// --- COLA DE LISTOS (READY QUEUE) ---
int ready_queue[MAX_PROCESSES];
int rq_head = 0;
int rq_aux = 0;
int rq_count = 0;

extern void dispatch(int nuevo_pid);
int kernel_pop_stack(int pid, int *value);

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

// Inicializa todas las tablas y estructuras del kernel
void kernel_init_structures()
{
    // Inicializar tabla de procesos
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        process_table[i].pid = -1;
        process_table[i].state = STATE_TERMINATED;
        process_table[i].partition_id = -1;
    }

    // Inicializar tabla de archivos
    for (int i = 0; i < MAX_FILE_TABLE; i++)
    {
        file_table[i].program_name[0] = '\0';
        file_table[i].pid = -1;
        file_table[i].partition_id = -1;
        file_table[i].state = FILE_STATE_DISK;
        file_table[i].track = -1;
        file_table[i].cylinder = -1;
        file_table[i].sector_initial = -1;
        file_table[i].size_words = 0;
        file_table[i].n_start = 0;
    }
    file_table_count = 0;

    // Inicializar particiones como libres
    for (int i = 0; i < NUM_PARTITIONS; i++)
    {
        partitions_bitmap[i] = false;
    }

    current_pid = NULL_PID;
    system_ticks = 0;

    write_log(0, "KERNEL: Estructuras inicializadas (Procesos, Archivos, Memoria).\n");
}

// ============================================================
// === FUNCIONES DE GESTIÓN DE TABLA DE ARCHIVOS ===
// ============================================================

/**
 * Busca un programa en la tabla de archivos por nombre
 *
 * Se usa principalmente para:
 *   - El comando CARGAR: verifica que no exista ya y esa broma
 *   - El comando EJECUTAR: obtiene los datos del programa en disco
 *
 * Retorna: índice en file_table, o -1 si no existe
 */
int file_table_search_by_name(const char *program_name)
{
    for (int i = 0; i < file_table_count; i++)
    {
        if (strcmp(file_table[i].program_name, program_name) == 0)
        {
            write_log(0, "FILE TABLE: Programa '%s' encontrado en índice %d.\n", program_name, i);
            return i;
        }
    }
    write_log(0, "FILE TABLE: Programa '%s' NO encontrado.\n", program_name);
    return -1;
}

/**
 * Busca un programa en la tabla de archivos por PID
 *
 * Se usa principalmente para:
 *   - Cuando un proceso TERMINA (kernel_handle_interrupt)
 *   - Cuando el SCHEDULER cambia de proceso
 *   - Para actualizar el estado del archivo en la tabla
 *
 * Retorna: índice en file_table, o -1 si no existe
 */
int file_table_find_by_pid(int pid)
{
    for (int i = 0; i < file_table_count; i++)
    {
        if (file_table[i].pid == pid)
        {
            write_log(0, "FILE TABLE: Archivo con PID %d encontrado en índice %d.\n", pid, i);
            return i;
        }
    }
    write_log(0, "FILE TABLE: NO hay archivo con PID %d.\n", pid);
    return -1;
}

/**
 * Obtiene un puntero a una entrada de la tabla
 * Valida que el índice
 *
 * Retorna: puntero a FileTableEntry, o NULL si índice inválido
 */
FileTableEntry *get_file_table_entry(int index)
{
    if (index < 0 || index >= file_table_count)
    {
        write_log(1, "FILE TABLE ERROR: Índice %d fuera de rango [0, %d).\n", index, file_table_count);
        return NULL;
    }
    return &file_table[index];
}

/**
 * Agrega un nuevo programa a la tabla de archivos.
 * Se llama desde load_program después de escribir en disco.
 *
 * Parámetros:
 *   - program_name: nombre del programa (ej: "prog1.txt")
 *   - track, cylinder, sector: ubicación en disco
 *   - size: cantidad de palabras
 *   - n_start: índice _start leído del archivo
 *
 * Retorna: índice en file_table, o -1 si error
 */
int file_table_add_entry(const char *program_name, int track, int cylinder, int sector, int size, int n_start)
{
    // Validar que la tabla no esté llena
    if (file_table_count >= MAX_FILE_TABLE)
    {
        write_log(1, "FILE TABLE ERROR: Tabla llena. No se puede agregar '%s'.\n", program_name);
        return -1;
    }

    // Verificar que no exista un programa con el mismo nombre
    if (file_table_search_by_name(program_name) != -1)
    {
        write_log(1, "FILE TABLE ERROR: Programa '%s' ya existe.\n", program_name);
        return -1;
    }

    // Agregar la nueva entrada
    FileTableEntry *entry = &file_table[file_table_count];

    strncpy(entry->program_name, program_name, 49);
    entry->program_name[49] = '\0'; // Garantizar null-termination

    entry->track = track;
    entry->cylinder = cylinder;
    entry->sector_initial = sector;
    entry->size_words = size;
    entry->n_start = n_start;
    entry->pid = -1;                // Sin PID aún (en disco)
    entry->partition_id = -1;       // Sin partición aún (en disco)
    entry->state = FILE_STATE_DISK; // Estado inicial: en disco

    write_log(0, "FILE TABLE: Entrada %d agregada: '%s' [Track=%d, Cyl=%d, Sec=%d, Size=%d, n_start=%d]\n",
              file_table_count, program_name, track, cylinder, sector, size, n_start);

    file_table_count++;
    return file_table_count - 1;
}

// Crea un PCB básico para un nuevo proceso (Estado NEW en disco)

int create_process(const char *name, int track, int cylinder, int sector, int size)
{
    int free_slot = -1;

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

    PCB *new_proc = &process_table[free_slot];

    new_proc->pid = free_slot;
    strncpy(new_proc->name, name, 49);
    new_proc->state = STATE_NEW;

    new_proc->disk_track = track;
    new_proc->disk_cylinder = cylinder;
    new_proc->disk_sector = sector;
    new_proc->prog_size = size;

    new_proc->partition_id = -1;

    memset(&new_proc->context, 0, sizeof(CPU_Context));

    write_log(0, "KERNEL: Proceso creado PID=%d, (%s) en estado NEW.\n", free_slot, name);
    return free_slot;
}

// Obtiene el PCB de un proceso
PCB *get_pcb(int pid)
{
    if (pid < 0 || pid >= MAX_PROCESSES)
        return NULL;
    if (process_table[pid].pid == -1)
        return NULL;
    return &process_table[pid];
}

/**
 * Encuentra una partición de RAM libre
 * Retorna: ID de partición (0-4), o -1 si todas están ocupadas
 */
int find_free_partition()
{
    for (int i = 0; i < NUM_PARTITIONS; i++)
    {
        if (!partitions_bitmap[i])
        {
            partitions_bitmap[i] = true; // Marcar como OCUPADA
            write_log(0, "KERNEL: Partición %d está libre.\n", i);
            return i;
        }
    }
    write_log(1, "KERNEL ERROR: Todas las particiones están ocupadas.\n");
    return -1;
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

void schedule()
{
    int outgoing_pid = current_pid;

    if (outgoing_pid != NULL_PID)
    {
        if (process_table[outgoing_pid].state == STATE_RUNNING)
        {
            enqueue_ready(outgoing_pid);
        }
    }

    int incoming_pid = dequeue_ready();

    if (incoming_pid == NULL_PID)
    {
        if (outgoing_pid != NULL_PID && process_table[outgoing_pid].state == STATE_READY)
        {
            incoming_pid = dequeue_ready();
        }
        else
        {
            current_pid = NULL_PID;
            return;
        }
    }

    if (outgoing_pid != incoming_pid && outgoing_pid != NULL_PID)
    {
        write_log(0, "PLANIFICADOR: Quantum agotado. Sale PID %d (%s), Entra PID %d (%s)\n",
                  outgoing_pid, process_table[outgoing_pid].name,
                  incoming_pid, process_table[incoming_pid].name);
    }

    process_table[incoming_pid].quantum_counter = 0;
    dispatch(incoming_pid);
}

void kernel_handle_interrupt(int interrupt_code)
{
    if (current_pid == NULL_PID)
        return;

    if (interrupt_code == INT_CLOCK)
    {
        system_ticks++;

        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            if (process_table[i].pid != -1 && process_table[i].state == STATE_BLOCKED)
            {
                if (process_table[i].wake_time > 0 && system_ticks >= process_table[i].wake_time)
                {
                    write_log(0, "KERNEL: Proceso %d despertó. Pasa a LISTO.\n", i);
                    process_table[i].wake_time = 0;
                    enqueue_ready(i);
                }
            }
        }

        if (current_pid != NULL_PID)
        {
            process_table[current_pid].quantum_counter++;

            if (process_table[current_pid].quantum_counter >= QUANTUM_TICKS)
            {
                write_log(0, "KERNEL: PID %d agotó su quantum.\n", current_pid);
                schedule();
            }
        }
        else
        {
            if (rq_count > 0)
            {
                schedule();
            }
        }
    }
    else if (interrupt_code == INT_INV_ADDR || interrupt_code == INT_UNDERFLOW ||
             interrupt_code == INT_OVERFLOW || interrupt_code == INT_INV_INSTR)
    {
        write_log(1, "KERNEL: Error fatal (Cod %d) en PID %d. Terminando.\n",
                  interrupt_code, current_pid);

        process_table[current_pid].state = STATE_TERMINATED;

        if (process_table[current_pid].partition_id != -1)
        {
            partitions_bitmap[process_table[current_pid].partition_id] = false;
        }

        schedule();
    }
    else if (interrupt_code == INT_IO_END)
    {
        int dma_state = dma_get_state();
        if (dma_state != 0)
        {
            write_log(1, "KERNEL: Error DMA en PID %d. Terminando.\n", current_pid);
            process_table[current_pid].state = STATE_TERMINATED;
            if (process_table[current_pid].partition_id != -1)
            {
                partitions_bitmap[process_table[current_pid].partition_id] = false;
            }
            schedule();
        }
    }
    else if (interrupt_code == INT_SYSCALL)
    {
        int syscall_code = sm_to_int(process_table[current_pid].context.AC);
        int param_raw = 0;
        int param_real = 0;

        switch (syscall_code)
        {
        case 1:
            if (kernel_pop_stack(current_pid, &param_raw) == 0)
            {
                param_real = sm_to_int(param_raw);
                write_log(0, "SYSCALL 1: Proceso %d termina con estado %d.\n", current_pid, param_real);
                process_table[current_pid].state = STATE_TERMINATED;
                if (process_table[current_pid].partition_id != -1)
                {
                    partitions_bitmap[process_table[current_pid].partition_id] = false;
                }
                schedule();
            }
            break;

        case 2:
            if (kernel_pop_stack(current_pid, &param_raw) == 0)
            {
                param_real = sm_to_int(param_raw);
                printf("\n[PROCESO %d]> %d\n", current_pid, param_real);
                write_log(0, "SYSCALL 2: Proceso %d imprime %d.\n", current_pid, param_real);
            }
            break;

        case 3:
            printf("\n[ENTRADA %d]> Ingrese un entero: ", current_pid);
            int input_val;
            if (scanf("%d", &input_val) == 1)
            {
                int input_sm = int_to_sm(input_val);
                process_table[current_pid].context.AC = input_sm;
                write_log(0, "SYSCALL 3: Proceso %d leyó %d.\n", current_pid, input_val);
            }
            else
            {
                write_log(1, "KERNEL ERROR: Entrada inválida.\n");
                while (getchar() != '\n')
                    ;
                process_table[current_pid].context.AC = int_to_sm(0);
            }
            break;

        case 4:
            if (kernel_pop_stack(current_pid, &param_raw) == 0)
            {
                param_real = sm_to_int(param_raw);
                if (param_real > 0)
                {
                    write_log(0, "SYSCALL 4: Proceso %d duerme %d tics.\n", current_pid, param_real);
                    process_table[current_pid].state = STATE_BLOCKED;
                    process_table[current_pid].wake_time = system_ticks + param_real;
                    schedule();
                }
            }
            break;

        default:
            write_log(1, "KERNEL ERROR: Syscall desconocida (%d) del PID %d. Violación de seguridad.\n", syscall_code, current_pid);
            // Parche: Asesinar al proceso rebelde
            process_table[current_pid].state = STATE_TERMINATED;
            if (process_table[current_pid].partition_id != -1)
            {
                partitions_bitmap[process_table[current_pid].partition_id] = false;
            }
            schedule(); // Cambiar de proceso inmediatamente
            break;
        }
    }
}

int kernel_pop_stack(int pid, int *value)
{
    int sp = process_table[pid].context.SP;

    if (bus_read(sp, value, 0) != 0)
    {
        return -1;
    }

    process_table[pid].context.SP++;

    return 0;
}