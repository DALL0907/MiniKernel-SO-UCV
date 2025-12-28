#include "cpu.h"
#include "bus.h"
#include "memory.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>

CPU_Context context;

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
    context.SP = 0;

    // inicializar PSW
    context.PSW.CC = 0;
    context.PSW.Mode = USER_MODE;
    context.PSW.Interrupts = 1;
    context.PSW.PC = 0;

    write_log(0, "CPU Inicializada.\n");
}

void cpu_interrupt(int interrupt_code)
{
    write_log(1, "Manejando interrupción en CPU.\n");
    // Aquí se implementaría la lógica para manejar interrupciones y salvar contexto
    // TAREA 11
    if (interrupt_code == INT_INV_ADDR || interrupt_code == INT_INV_INSTR)
    {
        exit(1); // Detener por error fatal momentáneamente
    }
}

void cpu()
{
    // Etapa Fetch
    context.MAR = context.PSW.PC;               // Cargar PC en MAR
    int phys_addr = mmu_translate(context.MAR); // traducir direccion
    if (phys_addr == -1)
        return; // Error de traducción

    if (bus_read(phys_addr, &context.MDR, 0) != 0)
    {
        write_log(1, "FATAL: Error de lectura en Bus (PC=%d)\n", context.PSW.PC);
        return 1;
    }
    context.IR = context.MDR; // Cargar instrucción en IR
    context.PSW.PC++;         // Incrementar PC

    // decode
    int opcode, mode, operand;
    decode(context.IR, &opcode, &mode, &operand);

    // execute

    switch (opcode)
    {
    // --- ARITMÉTICAS ---
    case OP_SUM: // 00
        write_log(0, "Ejecutando SUM\n");
        break;
    case OP_RES: // 01
        write_log(0, "Ejecutando RES\n");
        break;
    case OP_MULT: // 02
        write_log(0, "Ejecutando MULT\n");
        break;
    case OP_DIVI: // 03
        write_log(0, "Ejecutando DIVI\n");
        break;

    // --- TRANSFERENCIA DE DATOS ---
    case OP_LOAD: // 04
        write_log(0, "Ejecutando LOAD\n");
        break;
    case OP_STR: // 05
        write_log(0, "Ejecutando STR\n");
        break;
    case OP_LOADRX: // 06
        write_log(0, "Ejecutando LOADRX\n");
        break;
    case OP_STRRX: // 07
        write_log(0, "Ejecutando STRRX\n");
        break;

    // --- COMPARACIÓN Y SALTOS ---
    case OP_COMP: // 08
        write_log(0, "Ejecutando COMP\n");
        break;
    case OP_JMPE: // 09
        write_log(0, "Ejecutando JMPE\n");
        break;
    case OP_JMPNE: // 10
        write_log(0, "Ejecutando JMPNE\n");
        break;
    case OP_JMPLT: // 11
        write_log(0, "Ejecutando JMPLT\n");
        break;
    case OP_JMPLGT: // 12
        write_log(0, "Ejecutando JMPLGT\n");
        break;
    case OP_J: // 27 (Salto incondicional)
        write_log(0, "Ejecutando J (Salto)\n");
        break;

    // --- SISTEMA Y PILA ---
    case OP_SVC: // 13
        write_log(1, "SVC: Llamada al Sistema (Fin de programa temporal)\n");
        return 1;  // Detener ejecución por ahora
    case OP_RETRN: // 14
        write_log(0, "Ejecutando RETRN\n");
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
        cpu_interrupt(INT_INV_INSTR); // Interrupción 5 [cite: 70]
        return 1;
    }

    return 0; // Continuar ejecución
}
