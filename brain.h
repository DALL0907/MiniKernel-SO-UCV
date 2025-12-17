#ifndef BRAIN_H
#define BRAIN_H

#include <stdint.h>

// Constantes
#define MEM_SIZE 2000
#define OS_RESERVED 300
#define WORD_DIGITS 8

// Modos de Operacion
#define MODE_USER 0
#define MODE_KERNEL 1

// Códigos de Interrupción (Vector)
#define INT_SYSCALL_INVALID 0
#define INT_INVALID_OP 1 // Codigo interrupcion invalido
#define INT_SYSCALL 2
#define INT_CLOCK 3
#define INT_IO_END 4
#define INT_INV_INSTR 5 // Instruccion invalida
#define INT_INV_ADDR 6  // Direccionamiento invalido (Violación de segmento)
#define INT_UNDERFLOW 7
#define INT_OVERFLOW 8

// Conjunto de Instrucciones (Opcodes)
// Aritméticas
#define OP_SUM 0
#define OP_RES 1
#define OP_MULT 2
#define OP_DIVI 3
// Transferencia de Datos
#define OP_LOAD 4
#define OP_STR 5
#define OP_LOADRX 6
#define OP_STRRX 7
// Comparación y Saltos
#define OP_COMP 8
#define OP_JMPE 9
#define OP_JMPNE 10
#define OP_JMPLT 11
#define OP_JMPLGT 12
// Sistema
#define OP_SVC 13
#define OP_RETRN 14
#define OP_HAB 15
#define OP_DHAB 16
#define OP_TTI 17
#define OP_CHMOD 18
// Registros Base/Límite/Pila
#define OP_LOADRB 19
#define OP_STRRB 20
#define OP_LOADRL 21
#define OP_STRRL 22
#define OP_LOADSP 23
#define OP_STRSP 24
#define OP_PSH 25
#define OP_POP 26
#define OP_J 27 // Salto incondicional
// E/S (DMA)
#define OP_SDMAP 28
#define OP_SDMAC 29
#define OP_SDMAS 30 // Y SDMAIO (Mismo opcode 30/31 en tabla, pero 30 es sector)
#define OP_SDMAIO 31
#define OP_SDMAM 32
#define OP_SDMAON 33

// ==========================================
// Estructuras de Datos
// ==========================================

// Definición de Palabra (8 dígitos decimales se manejan como int en C)
typedef int Word;

// Registro PSW (Program Status Word)
typedef struct
{
    unsigned int CC : 2;         // Código Condición (0-3)
    unsigned int Mode : 1;       // Modo (0=User, 1=Kernel)
    unsigned int Interrupts : 1; // Habilitadas (0/1)
    unsigned int PC : 16;        // Program Counter (suficiente para 2000 posiciones)
} PSW_t;

// Contexto de Registros de CPU [Fuente: 13-19]
typedef struct
{
    Word AC;   // Acumulador
    Word MAR;  // Memory Address Register
    Word MDR;  // Memory Data Register
    Word IR;   // Instruction Register
    Word RB;   // Registro Base
    Word RL;   // Registro Límite
    Word RX;   // Registro Auxiliar/Indice
    Word SP;   // Stack Pointer
    PSW_t PSW; // Estado del sistema
} CPU_Context;

#endif