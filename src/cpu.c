#include "cpu.h"
#include "bus.h"
#include "memory.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>

CPU_Context context;
// Variables para gestión de interrupciones
static int interrupt_pending = 0;  // Bandera: 0=No, 1=Si
static int interrupt_code_val = 0; // Cuál interrupción es

// Guarda un valor en la Pila del Sistema
// Retorna 0 si éxito, -1 si desbordamiento (Stack Overflow)
int push_stack(int value)
{
    // La pila crece hacia abajo (direcciones menores)
    // El SP apunta a la próxima dirección LIBRE).

    context.SP--;

    // Protección básica para no sobrescribir el Vector de Interrupciones (posiciones 0-20)
    if (context.SP < 20)
    {
        write_log(1, "FATAL: Stack Overflow (SP < 20). Sistema colapsado.\n");
        exit(1);
    }

    // Escribimos directamente en memoria física (el Kernel accede directo)
    // Usamos el ID 0 (CPU)
    if (bus_write(context.SP, value, 0) != 0)
    {
        return -1;
    }
    return 0;
}

// Saca un valor de la pila
int pop_stack(int *value)
{
    // Leemos de donde apunta SP
    if (bus_read(context.SP, value, 0) != 0)
        return -1;
    // Incrementamos SP (la pila se reduce)
    context.SP++;
    return 0;
}

int get_value(int mode, int operand, int *value)
{
    if (mode == 1)
    {
        *value = operand; // valor viene en la instruccion
        return 0;         //
    }

    int logical_addr = operand; // por defecto modo 0

    if (mode == 2)
    {
        logical_addr = operand + context.RX; // Modo 2: Sumamos el índice RX
    }

    if (mode != 0 && mode != 2)
    {
        write_log(1, "ERROR: Modo de direccionamiento inválido (%d)\n", mode);
        return -1;
    }

    int phys_addr = mmu_translate(logical_addr);
    if (phys_addr == -1)
        return -1; // Error de traducción
    Word aux;
    if (bus_read(phys_addr, &aux, 0) != 0)
    {
        write_log(1, "FATAL: Error de lectura en Bus/Memoria (get_value addr=%d, phys=%d)\n", operand, phys_addr);
        return -1; // fallo en bus
    }
    *value = aux;

    return 0; // modo inválido
}

int mmu_translate(int logical_addr)
{
    if (context.PSW.Mode == KERNEL_MODE)
    {
        return logical_addr; // Modo privilegiado accede a todo
    }

    // Modo Usuario: se Reubica
    int physical_addr = logical_addr + context.RB;

    // Verificación de límites (Si está fuera del rango asignado)
    if (physical_addr < context.RB || physical_addr > context.RL)
    {
        write_log(1, "ERROR MMU: Violacion de Segmento. Logica:%d -> Fisica:%d (Limites RB:%d - RL:%d)\n",
                  logical_addr, physical_addr, context.RB, context.RL);
        cpu_interrupt(INT_INV_ADDR); // Interrupción 6
        return -1;
    }
    return physical_addr;
}

void decode(Word instruction, int *opcode, int *mode, int *operand)
{
    int aux = instruction;

    *operand = aux % 100000; // ultimos 5
    aux /= 100000;
    *mode = aux % 10; // siguiente digito
    aux /= 10;
    *opcode = aux; // los 2 restantes
}

void cpu_init()
{
    // inicializar registros en 0
    context.AC = 0;
    context.IR = 0;
    context.MAR = 0;
    context.MDR = 0;
    context.RB = 0;
    context.RL = 0;
    context.RX = 0;
    context.SP = 299; // limite memoria del SO, aqui inicia la PILA

    // inicializar PSW
    context.PSW.CC = 0;
    context.PSW.Mode = USER_MODE;
    context.PSW.Interrupts = 1;
    context.PSW.PC = 0;

    write_log(0, "CPU Inicializada.\n");
}

void cpu_interrupt(int interrupt_code)
{

    // Solo registramos que hay una interrupción pendiente.
    // El ciclo cpu_step la procesará antes de la siguiente instrucción.
    interrupt_pending = 1;
    interrupt_code_val = interrupt_code;
    if (interrupt_code == INT_INV_ADDR || interrupt_code == INT_INV_INSTR)
    {
        exit(1); // Detener por error fatal momentáneamente
    }
    write_log(1, ">> SOLICITUD INTERRUPCION: Codigo %d detectada.\n", interrupt_code);
}

void handle_interrupt()
{
    write_log(0, "INT: Iniciando secuencia de interrupción %d...\n", interrupt_code_val);

    // 1. SALVAR CONTEXTO (Guardar registros en la Pila)
    // El orden es arbitrario, pero debe coincidir con el futuro "RETRN" (IRET)
    push_stack(context.PSW.PC); // Guardar dónde íbamos
    push_stack(context.AC);     // Guardar Acumulador
    push_stack(context.RX);     // Guardar Registro Auxiliar
    push_stack(context.RB);     // Guardar Base
    push_stack(context.RL);     // Guardar Límite
    push_stack(context.PSW.CC); // Guardar Estado de comparación

    // Guardamos el Modo anterior para poder volver a él
    push_stack(context.PSW.Mode);

    // 2. CAMBIAR A MODO KERNEL
    context.PSW.Mode = KERNEL_MODE; // Ahora somos omnipotentes
    context.PSW.Interrupts = 0;     // Deshabilitar int anidadas (para no interrumpir al manejador)

    // 3. SALTO AL MANEJADOR (Vector de Interrupciones)
    // Leemos la dirección de memoria física donde está el código para esta interrupción.
    // Asumimos que el Vector está en las direcciones 0, 1, 2... de la RAM física.
    Word handler_address;
    if (bus_read(interrupt_code_val, &handler_address, 0) == 0)
    {
        context.PSW.PC = handler_address;
        write_log(0, "INT: Contexto salvado. Saltando a manejador en dir %d\n", handler_address);
    }
    else
    {
        write_log(1, "INT FATAL: No se pudo leer el Vector de Interrupciones.\n");
        exit(1);
    }

    // Limpiamos la bandera
    interrupt_pending = 0;
}

int cpu()
{
    // Solo atendemos si hay una pendiente Y las interrupciones están habilitadas
    if (interrupt_pending && context.PSW.Interrupts)
    {
        handle_interrupt();
        return 0; // el ciclo  solo maneja la interrupción
    }
    // Etapa Fetch
    context.MAR = context.PSW.PC;               // Cargar PC en MAR
    int phys_addr = mmu_translate(context.MAR); // traducir direccion
    if (phys_addr == -1)
        return 1; // Error de traducción: mmu_translate ya registró la violación de segmento

    if (bus_read(phys_addr, &context.MDR, 0) != 0)
    {
        // Hubo fallo al leer en la dirección física
        write_log(1, "FATAL: Error de lectura en Bus/Memoria (PC=%d, phys=%d)\n", context.PSW.PC, phys_addr);

        cpu_interrupt(INT_INV_ADDR);

        return 1;
    }
    context.IR = context.MDR; // Cargar instrucción en IR
    context.PSW.PC++;         // Incrementar PC

    // decode
    int opcode, mode, operand, val;
    decode(context.IR, &opcode, &mode, &operand);

    // execute

    switch (opcode)
    {
    // --- ARITMÉTICAS ---
    case OP_SUM: // 00
        if (get_value(mode, operand, &val) == 0)
        {
            context.AC += val;
            write_log(0, "Ejecutando SUM, Ahora AC=%d\n", context.AC);
        }
        break;
    case OP_RES: // 01
        if (get_value(mode, operand, &val) == 0)
        {
            context.AC -= val;
            write_log(0, "Ejecutando RES, ahora AC=%d\n", context.AC);
        }
        break;
    case OP_MULT: // 02
        if (get_value(mode, operand, &val) == 0)
        {
            context.AC *= val;
            write_log(0, "Ejecutando MULT, ahora AC=%d\n", context.AC);
        }
        break;
    case OP_DIVI: // 03
        if (get_value(mode, operand, &val) == 0)
        {
            if (val == 0)
            {
                write_log(1, "ERROR: División por cero en DIVI\n");
                cpu_interrupt(INT_INVALID_OP); // Interrupción 7
                return 1;
            }
            context.AC /= val;
            write_log(0, "Ejecutando DIVI, ahora AC=%d\n", context.AC);
        }
        break;

    // --- TRANSFERENCIA DE DATOS ---
    case OP_LOAD: // 04
        if (get_value(mode, operand, &val) == 0)
        {
            context.AC = val;
            write_log(0, "Ejecutando LOAD, AC cargado con %d\n", val);
        }
        break;
    case OP_STR: // 05
        if (mode == 1)
        {
            write_log(1, "ERROR: Modo inmediato inválido para STR\n");
            return 1;
        }
        // se calcula la direccion final segun el modo de direccionamiento
        int final_addr = operand;
        if (mode == 2)
        {
            final_addr = operand + context.RX; // Sumar índice
        }
        int target_addr = mmu_translate(final_addr);
        if (target_addr != -1)
        {
            bus_write(target_addr, context.AC, 0);
            write_log(0, "Ejecutando STR, valor %d escrito en dirección %d\n", context.AC, target_addr);
        }
        break;
    case OP_LOADRX: // 06
        if (get_value(mode, operand, &val) == 0)
        {
            context.RX = val;
            write_log(0, "Ejecutando LOADRX, RX cargado con %d\n", val);
        }
        break;
    case OP_STRRX: // 07
        if (mode == 1)
        {
            write_log(1, "ERROR: Modo inmediato inválido para STRRX\n");
            return 1;
        }
        int final_addr = operand;
        if (mode == 2)
        {
            final_addr = operand + context.RX;
        }
        int target_addr_rx = mmu_translate(final_addr);
        if (target_addr_rx != -1)
        {
            bus_write(target_addr_rx, context.RX, 0);
            write_log(0, "Ejecutando STRRX, valor %d escrito en dirección %d\n", context.RX, target_addr_rx);
        }

        break;

    // --- COMPARACIÓN Y SALTOS ---
    case OP_COMP: // 08
        if (get_value(mode, operand, &val) == 0)
        {
            if (context.AC == val)
            {
                context.PSW.CC = 0; // Igual
                write_log(0, "COMP : AC == val == %d\n, CC=0", val);
            }
            else if (context.AC < val)
            {
                context.PSW.CC = 1; // Menor
                write_log(0, "COMP : AC < val == %d\n, CC=1", val);
            }
            else
            {
                context.PSW.CC = 2; // Mayor
                write_log(0, "COMP : AC > val == %d\n, CC=2", val);
            }
        }
        write_log(0, "Ejecutando COMP\n");
        break;
    case OP_JMPE: // 09 - Jump if Equal (CC == 0)
        if (context.PSW.CC == 0)
        {
            context.PSW.PC = operand; // Saltamos (PC = Dirección Lógica destino)
            write_log(0, "JMPE: Salto tomado a %d\n", operand);
        }
        break;
    case OP_JMPNE: // 10 - Jump if Not Equal (CC != 0)
        if (context.PSW.CC != 0)
        {
            context.PSW.PC = operand;
            write_log(0, "JMPNE: Salto tomado a %d\n", operand);
        }
        break;
    case OP_JMPLT: // 11 - Jump if Less Than (CC == 1)
        if (context.PSW.CC == 1)
        {
            context.PSW.PC = operand;
            write_log(0, "JMPLT: Salto tomado a %d\n", operand);
        }
        break;
    case OP_JMPLGT: // 12 - Jump if Greater Than (CC == 2)
        if (context.PSW.CC == 2)
        {
            context.PSW.PC = operand;
            write_log(0, "JMPLGT: Salto tomado a %d\n", operand);
        }
        break;
    case OP_J: // 27 (Salto incondicional)
        context.PSW.PC = operand;
        write_log(0, "J: Salto incondicional a %d\n", operand);
        break;

    // --- SISTEMA Y PILA ---
    case OP_SVC: // 13
        write_log(0, "SVC: Solicitud de servicio al sistema.\n");
        // Esto dispara una interrupción de software (Código 2 según brain.h)
        cpu_interrupt(INT_SYSCALL);
        break;
        write_log(1, "SVC: Llamada al Sistema (Fin de programa temporal)\n");
        return 1;  // Detener ejecución por ahora
    case OP_RETRN: // 14 //Retorno de interrupción
        if (context.PSW.Mode == USER_MODE)
        {
            write_log(1, "ERROR: Intento de RETRN en Modo Usuario.\n");
            cpu_interrupt(INT_INVALID_OP); // Protección
        }
        else
        {
            // Recuperar contexto en ORDEN INVERSO al guardado en handle_interrupt
            // Orden guardado: PC, AC, RX, RB, RL, CC, Mode
            // Orden recuperación: Mode, CC, RL, RB, RX, AC, PC
            int temp;
            pop_stack(&temp);
            context.PSW.Mode = temp;
            pop_stack(&temp);
            context.PSW.CC = temp;
            pop_stack(&temp);
            context.RL = temp;
            pop_stack(&temp);
            context.RB = temp;
            pop_stack(&temp);
            context.RX = temp;
            pop_stack(&temp);
            context.AC = temp;
            pop_stack(&temp);
            context.PSW.PC = temp;

            context.PSW.Interrupts = 1; // Volver a habilitar interrupciones
            write_log(0, "RETRN: Contexto restaurado. Volviendo a PC=%d\n", context.PSW.PC);
        }
        break;
    case OP_HAB: // 15
        write_log(0, "Ejecutando HAB (Habilitar Int)\n");
        context.PSW.Interrupts = 1;
        break;
    case OP_DHAB: // 16
        write_log(0, "Ejecutando DHAB (Deshabilitar Int)\n");
        context.PSW.Interrupts = 0;
        break;
    case OP_TTI: // 17
        write_log(0, "Ejecutando TTI (Timer)\n");
        break;
    case OP_CHMOD: // 18
        write_log(0, "Ejecutando CHMOD\n");
        break;

    // --- REGISTROS BASE/LIMITE/PILA ---
    case OP_LOADRB: // 19
        write_log(0, "Ejecutando LOADRB\n");
        break;
    case OP_STRRB: // 20
        write_log(0, "Ejecutando STRRB\n");
        break;
    case OP_LOADRL: // 21
        write_log(0, "Ejecutando LOADRL\n");
        break;
    case OP_STRRL: // 22
        write_log(0, "Ejecutando STRRL\n");
        break;
    case OP_LOADSP: // 23
        write_log(0, "Ejecutando LOADSP\n");
        break;
    case OP_STRSP: // 24
        write_log(0, "Ejecutando STRSP\n");
        break;
    case OP_PSH: // 25
        write_log(0, "Ejecutando PSH (Push)\n");
        break;
    case OP_POP: // 26
        write_log(0, "Ejecutando POP\n");
        break;

    // --- E/S DMA ---
    case OP_SDMAP:  // 28
    case OP_SDMAC:  // 29
    case OP_SDMAS:  // 30
    case OP_SDMAIO: // 31
    case OP_SDMAM:  // 32
    case OP_SDMAON: // 33
        write_log(0, "Ejecutando Instruccion DMA (%d)\n", opcode);
        // Aquí se comunicará con el módulo dma.c
        break;

    default:
        write_log(1, "ERROR: Instruccion Ilegal (Opcode %d) en PC=%d\n", opcode, context.PSW.PC - 1);
        cpu_interrupt(INT_INV_INSTR); // Interrupción 5
        return 1;
    }

    return 0; // Continuar ejecución
}
