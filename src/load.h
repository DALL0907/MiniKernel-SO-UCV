#ifndef LOAD_H
#define LOAD_H

#include "brain.h"

typedef struct
{
    int index_start;  // posicion o indice del _start
    int n_words;      // palabras que se leeran
    int load_address; // direccion desde donde se cargo
} loadParams;

/*
 * Carga un archivo .txt en la memoria del sistema.
 * filename: Ruta del archivo.
 * base_address: Dirección de memoria donde comenzar a escribir.
 * info: Puntero para devolver los metadatos del programa.
 * Retorna: 0 si éxito, -1 si error.
 */

int load_program(const char *filename, int base_address, loadParams *info);

#endif // LOADER_H