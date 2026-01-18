#ifndef CPU_H
#define CPU_H

// Traduce las direcciones logicas del programa a fisicas para compatibilidad con la ram
int mmu_translate(int logical_addr);

// Etapa decode del ciclo de instruccion del cpu
void decode(Word instruction, int *opcode, int *mode, int *operand);

// Guarda un valor en la Pila del Sistema
// Retorna 0 si éxito, -1 si desbordamiento (Stack Overflow)
int push_stack(int value);

// Saca un valor de la pila
int pop_stack(int *value);

// Obtiene el valor codificado en la instrucción según el modo de direccionamiento
int get_value(int mode, int operand, int *value);

// Convierte formato Signo-Magnitud (SM) a Entero de C
int sm_to_int(int sm_val);

// Convierte Entero de C a formato Signo-Magnitud (SM)
int int_to_sm(int int_val);

// inicializa el procesador como queremos
void cpu_init();

// notificar interrupciones del cpu
void cpu_interrupt(int interrupt_code);

// manejar interrupciones del cpu
int handle_interrupt();

// acciones normales del cpu, las 34 instruc
int cpu();

#endif
