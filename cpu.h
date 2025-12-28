#ifndef CPU_H
#define CPU_H

int mmu_translate(int logical_addr);

void decode(Word instruction, int *opcode, int *mode, int *operand);

// inicializa el procesador como queremos
void cpu_init();

// manejar interrupciones del cpu
void cpu_interrupt();

// acciones normales del cpu, las 34 instruc
void cpu();

#endif