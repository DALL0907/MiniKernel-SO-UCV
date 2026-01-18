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

#define USER_PROGRAM_START 300
#define SYSTEM_STACK_START 299

// Referencia al contexto global (definido en cpu.c)
extern CPU_Context context;

// --- FUNCIONES DE UTILIDAD (Que faltaban) ---

void print_registers()
{
    /*
    printf("\n[ESTADO CPU] -----------------------------------\n");
    printf(" PC: %04d | IR: %08d | AC: %d\n", context.PSW.PC, context.IR, context.AC);
    printf(" RX: %d    | SP: %d       | Mode: %s\n", context.RX, context.SP, (context.PSW.Mode == 0 ? "USER" : "KERNEL"));
    printf(" RB: %d    | RL: %d       | CC: %d\n", context.RB, context.RL, context.PSW.CC);
    printf("------------------------------------------------\n");
    */
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

void system_init()
{
    write_log(0, "=== INICIANDO SISTEMA ===\n");
    bus_init();    // Inicia memoria y mutex
    disk_init();   // Inicia disco
    dma_init();    // Inicia hilos DMA
    cpu_init();    // Resetea registros
    init_kernel(); // Prepara memoria del SO
    printf("Sistema inicializado correctamente.\n");
}

static void print_banner()
{
    printf("\nShell\n");
    printf("Comandos:\n");
    printf("  cargar <archivo>  - Carga un programa en memoria\n");
    printf("  ejecutar          - Ejecuta el programa cargado (run)\n");
    printf("  debug             - Modo paso a paso con estado\n");
    printf("  salir             - Termina el simulador\n\n");
}

// --- MAIN LOOP ---

int main()
{
    int cargado = 0;
    log_init();
    system_init();
    print_banner();

    while (true)
    {
        char comando[256];
        printf("Shell> ");
        if (fgets(comando, sizeof(comando), stdin) == NULL)
            break;

        comando[strcspn(comando, "\n")] = '\0'; // Elimina salto de línea

        // --- COMANDO: SALIR ---
        if (strcmp(comando, "salir") == 0)
        {
            printf("Apagando sistema...\n");
            // Limpieza de recursos (IMPORTANTE)
            dma_destroy();
            disk_destroy();
            bus_destroy();
            log_close();
            break;
        }
        // --- COMANDO: CARGAR ---
        else if (strncmp(comando, "cargar ", 7) == 0)
        {
            char filename[248];
            const char *p = comando + 7;
            while (*p == ' ')
                p++;

            if (*p == '\0')
            {
                printf("Uso: cargar <archivo.txt>\n");
                continue;
            }

            if (sscanf(p, "%247[^\n]", filename) == 1)
            {
                // Trim espacios finales
                size_t len = strlen(filename);
                while (len > 0 && isspace((unsigned char)filename[len - 1]))
                {
                    filename[--len] = '\0';
                }

                loadParams info;
                printf("Cargando '%s' en dir fisica %d...\n", filename, USER_PROGRAM_START);

                // LLAMADA REAL AL CARGADOR
                if (load_program(filename, USER_PROGRAM_START, &info) == 0)
                {
                    printf("Programa cargado exitosamente.\n");

                    // CONFIGURACIÓN DE CONTEXTO (Vital para que corra)
                    cpu_init(); // Resetear registros base
                    context.RB = info.load_address;
                    context.RL = 1999;
                    context.PSW.PC = info.index_start; // Punto de entrada relativo
                    context.SP = SYSTEM_STACK_START;   // Pila del sistema
                    context.PSW.Mode = USER_MODE;

                    printf("Proceso listo: PC=%d, RB=%d, RL=%d\n", context.PSW.PC, context.RB, context.RL);
                    cargado = 1;
                }
                else
                {
                    printf("Error: No se pudo cargar el programa.\n");
                    cargado = 0;
                }
            }
        }
        // --- COMANDO: EJECUTAR ---
        else if (strcmp(comando, "ejecutar") == 0 || strcmp(comando, "run") == 0)
        {
            if (!cargado)
            {
                printf("Error: No hay programa cargado.\n");
                continue;
            }
            printf("Ejecutando...\n");
            while (1)
            {
                int ret = cpu();

                if (ret != 0)
                {
                    printf(">> CPU Detenida (Codigo: %d)\n", ret);
                    print_registers();
                    // Opcional: cargado = 0; si se da opcion a recargar
                    break;
                }
            }
        }
        // --- COMANDO: DEBUG ---
        else if (strcmp(comando, "debug") == 0)
        {
            if (!cargado)
            {
                printf("Error: No hay programa cargado.\n");
                continue;
            }
            write_log(1, "=== MODO DEBUG ACTIVADO ===\n");
            //printf("--- MODO DEBUG ACTIVADO ---\n");
            printf("Comandos: 'step' (realizar paso), 'regs' (ver registros), 'salir'\n");
            print_registers();

            while (1)
            {
                printf("Debug> ");
                if (fgets(comando, sizeof(comando), stdin) == NULL)
                    break;
                comando[strcspn(comando, "\n")] = 0;

                if (strcmp(comando, "step") == 0)
                {
                    int ret = cpu();
                    print_registers();

                    if (ret != 0)
                    {
                        printf(">> Programa finalizado (Codigo: %d)\n", ret);
                        break; // Salir del debug
                    }
                }
                else if (strcmp(comando, "regs") == 0)
                {
                    print_registers();
                }
                else if (strcmp(comando, "salir") == 0)
                {
                    write_log(0, "=== MODO DEBUG DESACTIVADO ===\n");
                    printf(">> Saliendo del Debugger.\n");
                    break;
                }
                else
                {
                    printf("Comando desconocido en debug.\n");
                }
            }
        }
        else if (strlen(comando) > 0)
        {
            printf("Comando no reconocido.\n");
        }
    }
    return 0;
}