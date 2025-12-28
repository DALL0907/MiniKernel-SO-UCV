#ifndef CPU_H
#define CPU_H

int mmu_translate();

void decode();

// inicializa el procesador como queremos
void cpu_init();

// manejar interrupciones del cpu
void cpu_interrupt();

// acciones normales del cpu, las 34 instruc
void cpu();

#endif