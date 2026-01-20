#include "brain.h"
#include "cpu.h"
#include "bus.h"
#include "memory.h"
#include "log.h"
#include "dma.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

CPU_Context context;
// Variables para gestión de interrupciones
static int interrupt_pending = 0;  // Bandera: 0=No, 1=Si
static int interrupt_code_val = 0; // Cuál interrupción es
static const int IO_ERROR = 500;   // Código para error de E/S

// Guarda un valor en la Pila del Sistema
// Retorna 0 si éxito, -1 si desbordamiento (Stack Overflow)
int push_stack(int value)
{
    // La pila crece hacia abajo (direcciones menores)
    // El SP apunta a la próxima dirección LIBRE).

    context.SP--;

    // Protección básica para no sobrescribir el Vector de Interrupciones (posiciones 0-20)
    if (context.SP < 30)
    {
        write_log(1, "FATAL: Stack Overflow (SP < 30). Sistema colapsado.\n");
        context.SP++; // Revertir cambio
        return -1;
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
    if (context.SP >= 299)
    { // 300 es el inicio de usuario
        write_log(1, "ERROR: Stack Underflow\n");
        return -1;
    }
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

    if (mode < 0 || mode > 2)
    {
        write_log(1, "ERROR: Modo de direccionamiento inválido (%d)\n", mode);
        return -1;
    }

    int logical_addr = operand; // por defecto modo 0

    if (mode == 2)
    {
        logical_addr = operand + context.RX; // Modo 2: Sumamos el índice RX
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

    // Limpiar banderas de interrupción antiguas ---
    interrupt_pending = 0;
    interrupt_code_val = 0;

    write_log(0, "CPU Inicializada.\n");
}

void cpu_interrupt(int interrupt_code)
{

    // Solo registramos que hay una interrupción pendiente.
    // El ciclo cpu_step la procesará antes de la siguiente instrucción.
    interrupt_pending = 1;
    interrupt_code_val = interrupt_code;
    write_log(1, ">> SOLICITUD INTERRUPCION: Codigo %d detectada.\n", interrupt_code);
}

int handle_interrupt()
{
    write_log(0, "INT: Iniciando secuencia de interrupción %d...\n", interrupt_code_val);

    // --- MATAR PROCESO EN ERRORES FATALES ---

    // 1. Violación de Segmento
    if (interrupt_code_val == INT_INV_ADDR)
    {
        write_log(1, "KERNEL: Violación de Segmento (SIGSEGV). Terminando proceso.\n");
        interrupt_pending = 0;
        return INT_INV_ADDR;
    }

    // 2. Stack Underflow
    if (interrupt_code_val == INT_UNDERFLOW)
    {
        write_log(1, "KERNEL: Stack Underflow. Terminando proceso.\n");
        interrupt_pending = 0;
        return INT_UNDERFLOW;
    }

    // 3. Stack Overflow / División por Cero
    if (interrupt_code_val == INT_OVERFLOW)
    {
        write_log(1, "KERNEL: Overflow Aritmético / Div por Cero. Terminando proceso.\n");
        interrupt_pending = 0;
        return INT_OVERFLOW;
    }

    // 4. Instrucción Ilegal
    if (interrupt_code_val == INT_INV_INSTR)
    {
        write_log(1, "KERNEL: Instrucción Ilegal. Terminando proceso.\n");
        interrupt_pending = 0;
        return INT_INV_INSTR;
    }
    // Caso especial: Interrupción por finalización de E/S
    if (interrupt_code_val == INT_IO_END)
    {
        // Verificar el estado del DMA
        int dma_state = dma_get_state();

        if (dma_state != 0)
        {
            write_log(1, "INT: Operación DMA falló con estado %d\n", dma_state);

            // Si hay error en DMA, terminamos el programa
            // Limpiamos la bandera de interrupción
            interrupt_pending = 0;
            return IO_ERROR;
        }
    }

    // 1. SALVAR CONTEXTO (Guardar registros en la Pila)
    // El orden es arbitrario, pero debe coincidir con el futuro "RETRN" (IRET)
    if (push_stack(context.PSW.PC) != 0)
        return -1; // Guardar dónde íbamos
    if (push_stack(context.AC) != 0)
        return -1; // Guardar Acumulador
    if (push_stack(context.RX) != 0)
        return -1; // Guardar Registro Auxiliar
    if (push_stack(context.RB) != 0)
        return -1; // Guardar Base
    if (push_stack(context.RL) != 0)
        return -1; // Guardar Límite
    if (push_stack(context.PSW.CC) != 0)
        return -1; // Guardar Estado de comparación

    // Guardamos el Modo anterior para poder volver a él
    if (push_stack(context.PSW.Mode) != 0)
        return -1;

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
        return -1;
    }

    // Limpiamos la bandera
    interrupt_pending = 0;
    return 0;
}

// Convierte formato Signo-Magnitud (SM) a Entero de C
int sm_to_int(int sm_val)
{
    int signo = sm_val / 10000000;    // Obtener el primer dígito (0 o 1)
    int magnitud = sm_val % 10000000; // Obtener los 7 restantes

    if (signo == 1)
    {
        return -magnitud;
    }
    return magnitud;
}

// Convierte Entero de C a formato Signo-Magnitud (SM)
int int_to_sm(int int_val)
{
    int signo = 0;

    if (int_val < 0)
    {
        signo = 1;
        int_val = -int_val; // Convertir a positivo para obtener magnitud
    }

    // Verificación de desbordamiento (Solo caben 7 dígitos: 9,999,999)
    if (int_val > 9999999)
    {
        write_log(1, "ALU: Overflow de magnitud (Máx 7 dígitos). Truncando.\n");
        int_val = 9999999;
        context.PSW.CC = 3; // Indicar overflow
    }

    return (signo * 10000000) + int_val;
}

int cpu()
{
    // --- SIMULACIÓN DE RELOJ ---
    usleep(2000); // 2ms por ciclo de instrucción

    // --- CICLO DE INSTRUCCIÓN ---
    // Solo atendemos si hay una pendiente Y las interrupciones están habilitadas
    if (interrupt_pending && context.PSW.Interrupts)
    {
        int int_result = handle_interrupt();
        if (int_result != 0)
        {
            // Si devuelve un código > 0, es una interrupcion FATAL se acaba el programa
            if (int_result > 0)
                return int_result;

            write_log(1, "CPU CRASH: Fallo en manejo de interrupción.\n");
            return -1;
        }
        return 0; // Ciclo consumido por la interrupción (sin fetch)
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
            // 1. Decodificar lo que hay en AC (Formato SM -> Int C)
            int ac_real = sm_to_int(context.AC);
            // 2. Decodificar el operando que vino de memoria (Formato SM -> Int C)
            int val_real = sm_to_int(val);
            // 3. Hacer la suma matemática real
            long long resultado_temp = (long long)ac_real + val_real;
            // 4. Actualizar CC basado en el resultado REAL (negativo real, positivo real)
            if (resultado_temp == 0)
                context.PSW.CC = 0;
            else if (resultado_temp < 0)
                context.PSW.CC = 1; // Negativo
            else
                context.PSW.CC = 2; // Positivo

            // 5. Codificar el resultado de vuelta a Signo-Magnitud para guardarlo en AC
            context.AC = int_to_sm((int)resultado_temp);

            write_log(0, "ALU: SUM %d + %d = %d (Codificado en AC: %d)\n",
                      ac_real, val_real, (int)resultado_temp, context.AC);
        }
        break;
    case OP_RES: // 01
        if (get_value(mode, operand, &val) == 0)
        {
            // 1. Decodificar lo que hay en AC (Formato SM -> Int C)
            int ac_real = sm_to_int(context.AC);
            // 2. Decodificar el operando que vino de memoria (Formato SM -> Int C)
            int val_real = sm_to_int(val);
            // 3. Hacer la resta matemática real
            long long resultado_temp = (long long)ac_real - val_real;
            // 4. Actualizar CC basado en el resultado REAL (negativo real, positivo real)
            if (resultado_temp == 0)
                context.PSW.CC = 0;
            else if (resultado_temp < 0)
                context.PSW.CC = 1; // Negativo
            else
                context.PSW.CC = 2; // Positivo

            // 5. Codificar el resultado de vuelta a Signo-Magnitud para guardarlo en AC
            context.AC = int_to_sm((int)resultado_temp);

            write_log(0, "ALU: RES %d - %d = %d (Codificado en AC: %d)\n",
                      ac_real, val_real, (int)resultado_temp, context.AC);
        }
        break;
    case OP_MULT: // 02
        if (get_value(mode, operand, &val) == 0)
        {
            // 1. Decodificar lo que hay en AC (Formato SM -> Int C)
            int ac_real = sm_to_int(context.AC);
            // 2. Decodificar el operando que vino de memoria (Formato SM -> Int C)
            int val_real = sm_to_int(val);
            // 3. Hacer la multiplicación matemática real
            long long resultado_temp = (long long)ac_real * val_real;
            // 4. Actualizar CC basado en el resultado REAL (negativo real, positivo real)
            if (resultado_temp == 0)
                context.PSW.CC = 0;
            else if (resultado_temp < 0)
                context.PSW.CC = 1; // Negativo
            else
                context.PSW.CC = 2; // Positivo

            // 5. Codificar el resultado de vuelta a Signo-Magnitud para guardarlo en AC
            context.AC = int_to_sm((int)resultado_temp);

            write_log(0, "ALU: MULT %d * %d = %d (Codificado en AC: %d)\n",
                      ac_real, val_real, (int)resultado_temp, context.AC);
        }
        break;
    case OP_DIVI: // 03
        if (get_value(mode, operand, &val) == 0)
        {
            // 1. Decodificar lo que hay en AC (Formato SM -> Int C)
            int ac_real = sm_to_int(context.AC);
            // 2. Decodificar el operando que vino de memoria (Formato SM -> Int C)
            int val_real = sm_to_int(val);
            if (val_real == 0)
            {
                write_log(1, "ERROR ALU: División por CERO detectada.\n");
                cpu_interrupt(INT_OVERFLOW); // Interrupción 8
                return 1;
            }
            // 3. Hacer la división matemática real
            long long resultado_temp = (long long)ac_real / val_real;
            // 4. Actualizar CC basado en el resultado REAL (negativo real, positivo real)
            if (resultado_temp == 0)
                context.PSW.CC = 0;
            else if (resultado_temp < 0)
                context.PSW.CC = 1; // Negativo
            else
                context.PSW.CC = 2; // Positivo

            // 5. Codificar el resultado de vuelta a Signo-Magnitud para guardarlo en AC
            context.AC = int_to_sm((int)resultado_temp);

            write_log(0, "ALU: DIVI %d / %d = %d (Codificado en AC: %d)\n",
                      ac_real, val_real, (int)resultado_temp, context.AC);
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
        final_addr = operand;
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
            // Comparar los valores REALES, no los codificados
            int ac_real = sm_to_int(context.AC);
            int val_real = sm_to_int(val);

            if (ac_real == val_real)
                context.PSW.CC = 0;
            else if (ac_real < val_real)
                context.PSW.CC = 1;
            else
                context.PSW.CC = 2;
        }
        break;
    case OP_JMPE: // 09 - Jump if Equal (CC == 0)
        if (context.PSW.CC == 0)
        {
            context.PSW.PC = operand;
            write_log(0, "JMPE: Condicion cumplida (CC=0). Salto tomado a %d.\n", operand);
        }
        else
        {
            write_log(0, "JMPE: Condicion falsa (CC=%d). Salto NO tomado.\n", context.PSW.CC);
        }
        break;
    case OP_JMPNE: // 10 - Jump if Not Equal (CC != 0)
        if (context.PSW.CC != 0)
        {
            context.PSW.PC = operand;
            write_log(0, "JMPNE: Condicion cumplida. Salto tomado a %d.\n", operand);
        }
        else
        {
            write_log(0, "JMPNE: Condicion falsa (CC=0/Igual). Salto NO tomado.\n");
        }
        break;
    case OP_JMPLT: // 11 - Jump if Less Than (CC == 1)
        if (context.PSW.CC == 1)
        { // 1 significa Menor
            context.PSW.PC = operand;
            write_log(0, "JMPLT: Condicion cumplida (Menor). Salto tomado a %d.\n", operand);
        }
        else
        {
            write_log(0, "JMPLT: Condicion falsa (CC=%d). Salto NO tomado.\n", context.PSW.CC);
        }
        break;
    case OP_JMPLGT: // 12 - Jump if Greater Than (CC == 2)
        if (context.PSW.CC == 2)
        {
            context.PSW.PC = operand;
            write_log(0, "JMPGT: Condicion cumplida (Mayor). Salto tomado a %d.\n", operand);
        }
        else
        {
            write_log(0, "JMPGT: Condicion falsa (CC=%d). Salto NO tomado.\n", context.PSW.CC);
        }
        break;
    case OP_J: // 27 (Salto incondicional)
        context.PSW.PC = operand;
        write_log(0, "J: Salto incondicional a %d\n", operand);
        break;

    // --- SISTEMA Y PILA ---
    case OP_SVC: // 13
        // -----------------IMPORTANTE------------------
        // Verificar que no haya operaciones DMA pendientes antes de SVC
        // ---------------------------------------------
        /*if (dma_is_busy()) {
            write_log(0, "SVC: DMA ocupado. Esperando que termine antes de llamada al sistema...\n");
            // Volver a ejecutar SVC en el siguiente ciclo
            context.PSW.PC--;
            return 0; // Continuar ejecución sin llamar al sistema
        }*/
        write_log(0, "SVC: Solicitud de servicio al sistema.\n");
        // Esto dispara una interrupción de software (Código 2 según brain.h)
        cpu_interrupt(INT_SYSCALL);
        write_log(1, "SVC: Llamada al Sistema (Fin de programa temporal)\n");
        return 0;  // Detener ejecución por ahora
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
    case OP_TTI: // 17 // Simula un evento de reloj
        write_log(0, "TTI: Checkpoint de Timer ejecutado.\n");
        break;
    case OP_CHMOD: // 18 (Change mode)
        if (context.PSW.Mode == USER_MODE)
        {
            write_log(1, "ERROR: Intento de CHMOD en Modo Usuario.\n");
            cpu_interrupt(INT_INVALID_OP); // Protección
        }
        else
        {
            // Si el operando es 0 o 1, cambiamos el modo
            if (get_value(mode, operand, &val) == 0)
            {
                if (val == 0 || val == 1)
                {
                    context.PSW.Mode = val;
                    write_log(0, "CHMOD: Modo cambiado a %d\n", val);
                }
                else
                {
                    write_log(1, "ERROR: Modo invalido para CHMOD (%d)\n", val);
                }
            }
        }
        break;

    // --- REGISTROS BASE/LIMITE/PILA ---
    case OP_LOADRB: // 19
        if (context.PSW.Mode == USER_MODE)
        {
            cpu_interrupt(INT_INVALID_OP); // Prohibido para usuario
        }
        else
        {
            if (get_value(mode, operand, &val) == 0)
            {
                context.RB = val;
                write_log(0, "LOADRB: RB actualizado a %d\n", context.RB);
            }
        }
        break;
    case OP_STRRB: // 20
        // Guardar el valor de RB en memoria
        {
            if (context.PSW.Mode == USER_MODE)
            {
                cpu_interrupt(INT_INVALID_OP);
            }
            else
            {
                if (mode == 1)
                {
                    write_log(1, "ERROR: STRRB Inmediato\n");
                    return 1;
                }
                int log_addr = (mode == 2) ? operand + context.RX : operand;
                // Al ser Kernel, mmu_translate devuelve la dir física directa (no suma RB)
                int aux = mmu_translate(log_addr);
                if (aux != -1)
                {
                    bus_write(aux, context.RB, 0);
                    write_log(0, "STRRB: Guardado RB (%d) en Mem[%d]\n", context.RB, aux);
                }
            }
        }
        break;
    case OP_LOADRL: // 21
        if (context.PSW.Mode == USER_MODE)
        {
            cpu_interrupt(INT_INVALID_OP);
        }
        else
        {
            if (get_value(mode, operand, &val) == 0)
            {
                context.RL = val;
                write_log(0, "LOADRL: RL actualizado a %d\n", context.RL);
            }
        }
        break;
    case OP_STRRL: // 22
        if (context.PSW.Mode == USER_MODE)
        {
            cpu_interrupt(INT_INVALID_OP);
        }
        else
        {
            if (mode == 1)
            {
                write_log(1, "ERROR: STRRL Inmediato\n");
                return 1;
            }
            int log_addr = (mode == 2) ? operand + context.RX : operand;
            int aux = mmu_translate(log_addr);
            if (aux != -1)
            {
                bus_write(aux, context.RL, 0);
                write_log(0, "STRRL: Guardado RL (%d) en Mem[%d]\n", context.RL, aux);
            }
        }
        break;
    case OP_LOADSP: // 23
        // Cambiar dónde está la pila. Solo el Kernel debe hacer esto
        // para inicializar la pila de un nuevo proceso o resetear la del sistema.
        if (context.PSW.Mode == USER_MODE)
        {
            cpu_interrupt(INT_INVALID_OP);
        }
        else
        {
            if (get_value(mode, operand, &val) == 0)
            {
                context.SP = val;
                write_log(0, "LOADSP: SP actualizado a %d\n", context.SP);
            }
        }
        break;
    case OP_STRSP: // 24
        if (context.PSW.Mode == USER_MODE)
        {
            cpu_interrupt(INT_INVALID_OP);
        }
        else
        {
            if (mode == 1)
            {
                write_log(1, "ERROR: STRSP Inmediato\n");
                return 1;
            }
            int log_addr = (mode == 2) ? operand + context.RX : operand;
            int tgt = mmu_translate(log_addr);
            if (tgt != -1)
            {
                bus_write(tgt, context.SP, 0);
                write_log(0, "STRSP: Guardado SP (%d) en Mem[%d]\n", context.SP, tgt);
            }
        }
        break;
    case OP_PSH: // 25
        // Push: Mete un valor en la pila
        if (get_value(mode, operand, &val) == 0)
        {
            if (push_stack(val) == 0)
            {
                write_log(0, "PSH: Guardado %d en Stack (SP=%d)\n", val, context.SP);
            }
            else
            {
                cpu_interrupt(INT_OVERFLOW);
            }
        }
        break;
    case OP_POP: // 26
        // Pop: Saca valor de pila y lo guarda en Memoria (segun operando)
        {
            if (mode == 1)
            {
                write_log(1, "ERROR: POP Inmediato\n");
                return 1;
            }

            int pop_value;
            if (pop_stack(&pop_value) == 0)
            {
                // Ahora guardamos 'pop_value' en la dirección indicada por el operando
                int log_addr = (mode == 2) ? operand + context.RX : operand;
                int tgt = mmu_translate(log_addr);
                if (tgt != -1)
                {
                    bus_write(tgt, pop_value, 0);
                    write_log(0, "POP: Recuperado %d y guardado en Mem[%d]\n", pop_value, tgt);
                }
            }
            else
            {
                cpu_interrupt(INT_UNDERFLOW); // Código 7
            }
        }
        break;

    // --- E/S DMA ---
    // GRUPO 1: Configuración simple (Track, Cyl, Sec, IO)
    case OP_SDMAP:  // 28
    case OP_SDMAC:  // 29
    case OP_SDMAS:  // 30
    case OP_SDMAIO: // 31
        // Usamos get_value para soportar que el valor venga de un registro o inmediato
        if (get_value(mode, operand, &val) == 0)
        {
            dma_handler(opcode, val, context.PSW.Mode);
        }
        break;
    case OP_SDMAM: // 32
        int logical_dma_addr = operand;
        if (mode == 2)
            logical_dma_addr += context.RX; // Soportar indexado si se quiere

        int phys_dma_addr = logical_dma_addr;

        // Si es USUARIO, hay que traducir (Base + Desplazamiento)
        if (context.PSW.Mode == USER_MODE)
        {
            phys_dma_addr += context.RB;
        }

        // Validación extra de MMU antes de enviar al hardware
        if (phys_dma_addr > context.RL)
        {
            write_log(1, "CPU: Violacion de Segmento en SDMAM (Dir %d)\n", phys_dma_addr);
            cpu_interrupt(INT_INV_ADDR);
        }
        else
        {
            // Enviamos la dirección FÍSICA corregida al DMA
            dma_handler(opcode, phys_dma_addr, context.PSW.Mode);
        }
        break;
    case OP_SDMAON: // 33
        if (get_value(mode, operand, &val) != 0)
        {
            return 1; // Error al obtener el valor
        }
        // Aquí se comunicará con el módulo dma.c
        int dma_result = dma_handler(opcode, val, context.PSW.Mode);

        if (dma_result == DMA_BUSY_CODE)
        {
            // DMA está ocupado - reintentar en siguiente ciclo
            write_log(0, "CPU: DMA ocupado. Reintentando en siguiente ciclo...\n");
            context.PSW.PC--; // Decrementar PC para volver a ejecutar esta instrucción
        }
        else if (dma_result != 0)
        {
            write_log(1, "ERROR: Fallo en dma_handler para opcode %d (código: %d)\n", opcode, dma_result);
            return 1;
        }
        usleep(20000); // Simular retardo de activación
        break;
    default:
        write_log(1, "ERROR: Instruccion Ilegal (Opcode %d) en PC=%d\n", opcode, context.PSW.PC - 1);
        cpu_interrupt(INT_INV_INSTR); // Interrupción 5
        return 1;
    }

    return 0; // Continuar ejecución
}
