#include "load.h"
#include "bus.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int load_program(const char *filename, int base_address, loadParams *info)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        log_event(1, "No se pudo abrir el archivo: %s", filename);
        return -1;
    }

    char line[256];
    char aux_word[100];
    int offset = 0; // desplazamiento desde la base
    info->load_address = base_address;
    info->n_words = 0;
    info->index_start = 0;

    log_event(1, "Cargando programa %s en direccion fisica %d...\n", filename, base_address);

    while (fgets(line, sizeof(line), file))
    {
        // Ignorar líneas vacías y comentarios
        if (sscanf(line, "%s", aux_word) != 1)
        {
            continue;
        }
        // Verificar si es un comentario completo (empieza con //) e ignorarlo
        if (strncmp(aux_word, "//", 2) == 0)
        {
            continue;
        }
        if (strcmp(aux_word, "_start") == 0)
        {
            sscanf(line, "%*s %d", &info->index_start);
            log_event(0, "Directiva: Inicio en linea %d\n", info->index_start);
            continue;
        }
        if (strcmp(aux_word, ".NumeroPalabras") == 0)
        {
            sscanf(line, "%*s %d", &info->n_words);
            log_event(0, "Directiva: Numero de palabras %d\n", info->n_words);
            continue;
        }
        if (strcmp(aux_word, ".NombreProg") == 0)
        {
            sscanf(line, "%*s %d", &info->load_address);
            log_event(0, "Directiva: Direccion de carga %d\n", info->load_address);
            continue;
        }
        else if (isdigit(aux_word[0]))
        {
            Word word;
            sscanf(line, "%d", &word);

            int address = base_address + offset;
            if (bus_write(address, word, 2) == 0)
                offset++; // el 2 es el ID del cargador, sirve para el log
            else
            {
                log_event(1, "Error al escribir en memoria en la direccion %d", address);
                fclose(file);
                return -1;
            }
        }
    }
    fclose(file);
    log_info("Programa cargado exitosamente: %s", filename);
    return 0;
}