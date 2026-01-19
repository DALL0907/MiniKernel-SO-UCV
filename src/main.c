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

// Función auxiliar para traducir Opcode a Texto en el Debugger
const char *get_mnemonic(int opcode)
{
    switch (opcode)
    {
    case 0:
        return "SUM";
    case 1:
        return "RES";
    case 2:
        return "MULT";
    case 3:
        return "DIVI";
    case 4:
        return "LOAD";
    case 5:
        return "STR";
    case 6:
        return "LOADRX";
    case 7:
        return "STRRX";
    case 8:
        return "COMP";
    case 9:
        return "JMPE";
    case 10:
        return "JMPNE";
    case 11:
        return "JMPLT";
    case 12:
        return "JMPLGT";
    case 13:
        return "SVC";
    case 14:
        return "RETRN";
    case 15:
        return "HAB";
    case 16:
        return "DHAB";
    case 17:
        return "TTI";
    case 18:
        return "CHMOD";

    // --- FALTABAN ESTOS (Gestión de Registros) ---
    case 19:
        return "LOADRB";
    case 20:
        return "STRRB";
    case 21:
        return "LOADRL";
    case 22:
        return "STRRL";
    case 23:
        return "LOADSP";
    case 24:
        return "STRSP";
        // ---------------------------------------------

    case 25:
        return "PSH";
    case 26:
        return "POP";
    case 27:
        return "J";

    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
        return "DMA_OP";

    default:
        return "UNKNOWN";
    }
}

// --- MAIN LOOP ---
int main()
{
    int cargado = 0;
    loadParams info;
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
        else if (strcmp(comando, "ejecutar") == 0)
        {
            if (!cargado)
            {
                printf("Error: No hay programa cargado.\n");
                continue;
            }
            // === LÓGICA DE AUTO-REINICIO ===
            // Si el programa ya había terminado (PC al final), lo reiniciamos desde el principio.
            // Si estaba en medio (por un debug), lo dejamos continuar.
            if (context.PSW.Mode == USER_MODE && context.PSW.PC >= info.n_words)
            {
                printf(">> Reiniciando programa desde el principio...\n");
                cpu_init(); // Limpiar registros (AC, Flags...)

                // Restaurar contexto
                context.RB = info.load_address;
                context.RL = 1999;
                context.PSW.PC = info.index_start; // Volver al _start
                context.SP = SYSTEM_STACK_START;
                context.PSW.Mode = USER_MODE;
            }
            printf("Ejecutando...\n");

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

                // Caso 2: Fin de archivo (Usuario se quedó sin instrucciones)
                // Solo verificamos esto si estamos en MODO USUARIO.
                // Si estamos en KERNEL (atendiendo la SVC), dejamos que corra.
                if (context.PSW.Mode == USER_MODE)
                {
                    // Si el PC apunta más allá de lo que cargamos, terminamos.
                    if (context.PSW.PC >= info.n_words)
                    {
                        printf(">> Fin del programa: No hay más instrucciones (PC=%d).\n", context.PSW.PC);
                        print_registers();
                        break;
                    }
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
                    // 1. Obtener información PREVIA a la ejecución
                    int pc_actual = context.PSW.PC;
                    int linea_archivo = pc_actual + 1; // Ajuste Base 0 -> Base 1

                    // 2. Intentar "espiar" qué instrucción es
                    int dir_fisica = pc_actual;
                    if (context.PSW.Mode == USER_MODE)
                        dir_fisica += context.RB;

                    Word instruccion_raw;
                    mem_read_physical(dir_fisica, &instruccion_raw);

                    // Decodificar para mostrar el nombre
                    int opcode = instruccion_raw / 1000000;

                    printf("\n>> [DEBUG] Ejecutando LINEA %d (PC=%d) | Instr: %s\n",
                           linea_archivo, pc_actual, get_mnemonic(opcode));

                    // 3. Ejecutar un ciclo de CPU
                    int ret = cpu();
                    print_registers();

                    if (ret != 0)
                    {
                        write_log(0, "=== MODO DEBUG DESACTIVADO ===\n");
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