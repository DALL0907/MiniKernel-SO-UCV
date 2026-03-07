#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

// Incluimos todos los módulos del sistema
#include "brain.h"
#include "cpu.h"
#include "bus.h"
#include "memory.h"
#include "disk.h"
#include "dma.h"
#include "load.h"
#include "log.h"
#include "kernel.h"

#define USER_PROGRAM_START 300
#define SYSTEM_STACK_START 299

// Referencia al contexto global (definido en cpu.c)
extern CPU_Context context;

// --- FUNCIONES DE UTILIDAD ---
void print_registers()
{
    printf("\n[ESTADO CPU] -----------------------------------\n");
    printf(" PC: %08d | IR: %08d | AC: %08d\n",
           context.PSW.PC, context.IR, context.AC);
    printf(" RX: %08d | SP: %08d | Mode: %s\n",
           context.RX, context.SP, (context.PSW.Mode == 0 ? "USER" : "KERNEL"));
    printf(" RB: %08d | RL: %08d | CC: %d\n",
           context.RB, context.RL, context.PSW.CC);
    printf("------------------------------------------------\n");
}

// Inicializa el vector de interrupciones para evitar crashes
void init_kernel()
{
    // Llenamos las direcciones 0-19 apuntando a la dirección 20
    for (int i = 0; i < 20; i++)
    {
        mem_write_physical(i, 20);
    }
    // En la dirección 20 ponemos un RETRN (Opcode 14) de emergencia
    // 14 0 00000 = 14000000
    mem_write_physical(20, 14000000);

    // dejamos vacios las posiciones 21-29 para futuros manejadores
    for (int i = 21; i < 30; i++)
    {
        mem_write_physical(i, 0);
    }

    write_log(0, "KERNEL: Vector de interrupciones inicializado.\n");
}

int system_init()
{
    write_log(0, "=== INICIANDO SISTEMA ===\n");
    // Inicia memoria y mutex
    if (bus_init() != 0)
    {
        write_log(1, "FATAL: No se pudo iniciar el bus. Saliendo...\n");
        return -1;
    }
    // Inicia disco
    if (disk_init() != 0)
    {
        write_log(1, "FATAL: No se pudo iniciar el disco. Saliendo...\n");
        return -1;
    }
    // Inicia el módulo DMA
    if (dma_init() != 0)
    {
        write_log(1, "FATAL: No se pudo iniciar el DMA. Saliendo...\n");
        return -1;
    }
    // Resetea registros
    cpu_init();    // Resetea registros
    init_kernel(); // Prepara memoria del SO
    printf("Sistema inicializado correctamente.\n");
    return 0;
}

static void print_banner()
{
    printf("\n==============================================\n");
    printf("Comandos disponibles:\n");
    printf("  ejecutar <prog1> <prog2> ... <progN>  - Carga y ejecuta una lista de programas\n");
    printf("  ps                                     - Muestra el estado de los procesos\n");
    printf("  apagar                                 - Apaga el sistema y cierra el simulador\n");
    printf("  reiniciar                              - Reinicia el sistema sin cerrar\n");
    printf("==============================================\n\n");
}

// Comando APAGAR: Apaga el sistema de forma ordenada
void cmd_apagar()
{
    printf("Apagando sistema...\n");

    // Liberar recursos en orden específico
    dma_destroy();
    disk_destroy();
    bus_destroy();
    log_close();
}

// Comando REINICIAR: Reinicia el sistema sin cerrar el programa
void cmd_reiniciar()
{
    printf("Reiniciando sistema...\n");

    // Limpiar memoria RAM
    mem_init();

    // Resetear registros del CPU
    cpu_init();

    // Reinicializar estructuras del kernel
    kernel_init_structures();

    // Reinicializar disco virtual
    disk_init();

    // Reinicializar controlador DMA
    dma_init();

    // Reinicializar vector de interrupciones
    init_kernel();

    printf("Sistema reiniciado correctamente.\n");
}

// Comando MEMESTAT: Muestra el estado de las particiones de memoria RAM
void cmd_memestat()
{
    printf("\n============== ESTADO DE LA MEMORIA RAM ==============\n");
    printf(" Memoria Total  : %d palabras\n", MEM_SIZE);

    // Memoria del SO (Dir 0 a 299)
    float so_percent = ((float)MEM_USER_START / (float)MEM_SIZE) * 100.0f;
    printf(" [Dir %04d a %04d] SISTEMA OPERATIVO - Ocupada - %5.1f%%\n", 0, MEM_USER_START - 1, so_percent);

    // Particiones de Usuario
    float part_percent = ((float)PARTITION_SIZE / (float)MEM_SIZE) * 100.0f;

    for (int i = 0; i < NUM_PARTITIONS; i++)
    {
        int base = MEM_USER_START + (i * PARTITION_SIZE);
        int limit = base + PARTITION_SIZE - 1;

        if (partitions_bitmap[i])
        {
            // Opcional: Buscar qué PID es dueño de esta partición
            int owner_pid = -1;
            for (int j = 0; j < MAX_PROCESSES; j++)
            {
                PCB *p = get_pcb(j);
                if (p != NULL && p->partition_id == i && p->state != STATE_TERMINATED)
                {
                    owner_pid = p->pid;
                    break;
                }
            }
            printf(" [Dir %04d a %04d] PARTICIÓN %d        - OCUPADA (PID %2d) - %5.1f%%\n",
                   base, limit, i, owner_pid, part_percent);
        }
        else
        {
            printf(" [Dir %04d a %04d] PARTICIÓN %d        - LIBRE             - %5.1f%%\n",
                   base, limit, i, part_percent);
        }
    }
    printf("======================================================\n\n");
}

// Comando PS: Muestra el estado de todos los procesos
void cmd_ps()
{
    // Contar procesos activos
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        PCB *pcb = get_pcb(i);
        if (pcb != NULL)
        {
            count++;
        }
    }

    if (count == 0)
    {
        printf("No hay procesos en el sistema.\n");
        return;
    }

    // Mostrar encabezado
    printf("\n%-5s | %-20s | %-12s | %-10s\n",
           "PID", "NOMBRE", "ESTADO", "MEMORIA%");
    printf("------+----------------------+--------------+------------\n");

    // Mostrar cada proceso
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        PCB *pcb = get_pcb(i);

        if (pcb != NULL)
        {
            // Calcular porcentaje de memoria basado en el tamaño real del programa
            float mem_percent = 0.0f;
            // Si el proceso ya está en RAM (no está solo en el disco)
            if (pcb->state == STATE_READY || pcb->state == STATE_RUNNING || pcb->state == STATE_BLOCKED)
            {
                mem_percent = ((float)PARTITION_SIZE / (float)MEM_SIZE) * 100.0f;
            }

            // Obtener estado como string
            const char *estado = state_to_string(pcb->state);

            // Mostrar fila
            printf("%-5d | %-20s | %-12s | %9.1f%%\n",
                   pcb->pid, pcb->name, estado, mem_percent);
        }
    }

    printf("\n");
}

// Función auxiliar: Verifica si hay procesos activos en el sistema
bool hay_procesos_activos()
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        PCB *pcb = get_pcb(i);
        if (pcb != NULL &&
            (pcb->state == STATE_READY || pcb->state == STATE_RUNNING || pcb->state == STATE_BLOCKED))
        {
            return true;
        }
    }
    return false;
}

// Comando EJECUTAR: Carga y ejecuta una lista de programas
void cmd_ejecutar(const char *program_list_arg)
{
    // Paso 1: Validar entrada
    const char *p = program_list_arg;

    // Saltar espacios iniciales
    while (*p == ' ')
        p++;

    if (*p == '\0')
    {
        printf("Uso: ejecutar <prog1> <prog2> ... <progN>\n");
        return;
    }

    // Paso 2: Parsear y cargar cada programa
    char program_names[256];
    strncpy(program_names, p, sizeof(program_names) - 1);
    program_names[sizeof(program_names) - 1] = '\0';

    // Tokenizar la lista de programas
    char *token = strtok(program_names, " \t");
    int programs_loaded = 0;
    static int proxima_pista_libre = 0;

    while (token != NULL)
    {
        char program_name[256];
        strncpy(program_name, token, sizeof(program_name) - 1);
        program_name[sizeof(program_name) - 1] = '\0';

        printf("\n--- Cargando programa: %s ---\n", program_name);

        // Paso 3: Verificar si existe en File_Table
        int file_index = file_table_search_by_name(program_name);
        int pid = -1;

        if (file_index == -1)
        {
            // Paso 4: No existe, cargar desde PC real a Disco
            printf("Programa no encontrado en disco. Cargando desde archivo...\n");

            // Construir ruta del archivo (en carpeta src/)
            char filepath[300];
            snprintf(filepath, sizeof(filepath), "%s", program_name);

            // Cargar a disco (track=0, cylinder=0, sector=0 por defecto)
            pid = load_program_to_disk(filepath, program_name, proxima_pista_libre, 0, 0);

            if (pid != -1)
            {
                proxima_pista_libre++; // Avanzamos a la siguiente pista para el próximo programa
            }

            printf("Programa cargado a disco con PID %d.\n", pid);

            // Actualizar file_index después de cargar
            file_index = file_table_search_by_name(program_name);
        }
        else
        {
            // Paso 5: Ya existe en disco, crear nuevo PCB
            FileTableEntry *entry = get_file_table_entry(file_index);

            if (entry == NULL)
            {
                printf("Error: No se pudo obtener información del programa '%s'.\n", program_name);
                token = strtok(NULL, " \t");
                continue;
            }

            pid = create_process(program_name, entry->track, entry->cylinder,
                                 entry->sector_initial, entry->size_words);

            if (pid == -1)
            {
                printf("Error: No se pudo crear el proceso para '%s'.\n", program_name);
                token = strtok(NULL, " \t");
                continue;
            }

            printf("Proceso creado con PID %d.\n", pid);
        }

        // Paso 6: Buscar partición libre
        int partition_id = find_free_partition();

        if (partition_id == -1)
        {
            printf("Error: No hay memoria RAM disponible para '%s'.\n", program_name);
            token = strtok(NULL, " \t");
            continue;
        }

        printf("Partición %d asignada.\n", partition_id);

        // Paso 7: Cargar a RAM
        if (load_program_to_ram(pid, partition_id, file_index) != 0)
        {
            printf("Error: No se pudo cargar '%s' a memoria RAM.\n", program_name);
            token = strtok(NULL, " \t");
            continue;
        }

        printf("Programa '%s' cargado en RAM.\n", program_name);

        // Paso 8: Dispatch
        printf("Proceso %d (%s) listo para ejecutar.\n", pid, program_name);

        programs_loaded++;

        // Siguiente programa
        token = strtok(NULL, " \t");
    }

    if (programs_loaded == 0)
    {
        printf("No se pudo cargar ningún programa.\n");
        return;
    }

    printf("\n--- Ejecutando %d programa(s) ---\n", programs_loaded);
    // Dejamos que el planificador elija al primer proceso de la cola
    schedule();

    // Paso 9: Bucle de ejecución CPU
    while (1)
    {
        int ret = cpu();

        // Caso 1: Error fatal reportado por CPU
        if (ret != 0)
        {
            printf(">> CPU Detenida (Codigo: %d)\n", ret);
            print_registers();
            break;
        }

        // Caso 2: Verificar si hay procesos activos
        if (!hay_procesos_activos())
        {
            printf(">> No hay más procesos activos.\n");
            break;
        }
    }

    printf("\nEjecución completada.\n");
}

// --- MAIN LOOP ---
int main()
{
    log_init();
    if (system_init() != 0)
    {
        write_log(1, "FATAL: No se pudo iniciar el sistema. Saliendo...\n");
        return -1;
    }

    // Inicializar estructuras del kernel
    kernel_init_structures();

    print_banner();

    while (true)
    {
        char comando[256];
        printf("Shell> ");
        if (fgets(comando, sizeof(comando), stdin) == NULL)
            break;

        comando[strcspn(comando, "\n")] = '\0'; // Elimina salto de línea

        // --- COMANDO: APAGAR ---
        if (strcmp(comando, "apagar") == 0)
        {
            cmd_apagar();
            break;
        }
        // --- COMANDO: REINICIAR ---
        else if (strcmp(comando, "reiniciar") == 0)
        {
            cmd_reiniciar();
        }
        // --- COMANDO: PS ---
        else if (strcmp(comando, "ps") == 0)
        {
            cmd_ps();
        }
        else if (strcmp(comando, "memestat") == 0)
        {
            cmd_memestat();
        }
        // --- COMANDO: EJECUTAR ---
        else if (strncmp(comando, "ejecutar ", 9) == 0)
        {
            cmd_ejecutar(comando + 9);
        }
        // --- LÍNEA VACÍA ---
        else if (strlen(comando) == 0)
        {
            // Ignorar líneas vacías sin mostrar error
            continue;
        }
        // --- COMANDO NO RECONOCIDO ---
        else
        {
            printf("Comando no reconocido.\n");
        }
    }
    return 0;
}