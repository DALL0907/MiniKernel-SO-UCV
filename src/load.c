#include "load.h"
#include "bus.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int load_program(const char *filename, int base_address, loadParams *info)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        write_log(1, "LOADER: No se pudo abrir el archivo: %s\n", filename);
        return -1;
    }

    char line[256];
    char aux_word[100];
    int offset = 0;          // Palabras realmente leídas/cargadas
    int declared_words = -1; // -1 indica que no se ha encontrado la etiqueta aun

    // Inicializar info
    info->load_address = base_address;
    info->n_words = 0;
    info->index_start = 0; // Por defecto arranca en la primera (0)

    write_log(1, "LOADER: Cargando %s en dir fisica %d...\n", filename, base_address);

    while (fgets(line, sizeof(line), file))
    {
        // 1. lectura de primera palabra
        if (sscanf(line, "%s", aux_word) != 1)
            continue; // Línea vacía

        // 2. Ignorar comentarios completos
        if (strncmp(aux_word, "//", 2) == 0)
            continue;

        // 3. PROCESAR DIRECTIVAS
        if (strcmp(aux_word, "_start") == 0)
        {
            int lineaStart;
            sscanf(line, "%*s %d", &lineaStart);

            // CORRECCIÓN 1: Ajuste de Base 1 a Base 0
            // Si el archivo dice 1, el PC debe ser 0.
            if (lineaStart > 0)
            {
                info->index_start = lineaStart - 1;
            }
            else
            {
                info->index_start = 0; // Protección por si ponen 0
            }

            write_log(0, "LOADER: _start %d detectado -> PC inicial ajustado a %d\n", lineaStart, info->index_start);
            continue;
        }
        if (strcmp(aux_word, ".NumeroPalabras") == 0)
        {
            sscanf(line, "%*s %d", &declared_words);
            write_log(0, "LOADER: Palabras declaradas en header: %d\n", declared_words);
            continue;
        }
        if (strcmp(aux_word, ".NombreProg") == 0)
        {
            char prog_name[50];
            sscanf(line, "%*s %s", prog_name);
            write_log(0, "LOADER: Nombre del programa: %s\n", prog_name);
            continue;
        }

        // 4. PROCESAR INSTRUCCIONES / DATOS
        if (isdigit(aux_word[0]) || (aux_word[0] == '-' && isdigit(aux_word[1])))
        {
            Word word;
            sscanf(line, "%d", &word);

            int address = base_address + offset;

            // Escribir en memoria física (ID 2 = Loader)
            if (bus_write(address, word, 2) == 0)
            {
                offset++;
            }
            else
            {
                write_log(1, "LOADER ERROR: Fallo de escritura en dir %d\n", address);
                fclose(file);
                return -1;
            }
        }
    }

    fclose(file);

    // CORRECCIÓN 2: Validación Estricta de Cantidad
    if (declared_words != -1) // Solo si la etiqueta existía
    {
        if (offset != declared_words)
        {
            write_log(1, "LOADER ERROR: Inconsistencia de tamaño. Declaradas: %d, Leídas: %d.\n",
                      declared_words, offset);
            return -1; // Abortamos la carga
        }
    }

    // Actualizamos el número real de palabras cargadas
    info->n_words = offset;

    write_log(1, "LOADER: Carga finalizada exitosamente. %d palabras escritas.\n", info->n_words);
    return 0;
}