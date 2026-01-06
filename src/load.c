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
    int offset = 0; // desplazamiento desde la base

    // Inicializar info
    info->load_address = base_address;
    info->n_words = 0;
    info->index_start = 0;

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
            sscanf(line, "%*s %d", &info->index_start);
            write_log(0, "LOADER: Punto de entrada (_start) relativo: %d\n", info->index_start);
            continue;
        }
        if (strcmp(aux_word, ".NumeroPalabras") == 0)
        {
            // Solo logueamos, confiaremos en lo que leamos
            int declaradas;
            sscanf(line, "%*s %d", &declaradas);
            write_log(0, "LOADER: Palabras declaradas en header: %d\n", declaradas);
            continue;
        }
        if (strcmp(aux_word, ".NombreProg") == 0)
        {
            // leer el nombre como string.
            char prog_name[50];
            sscanf(line, "%*s %s", prog_name);
            write_log(0, "LOADER: Nombre del programa: %s\n", prog_name);
            continue;
        }

        // 4. PROCESAR INSTRUCCIONES / DATOS
        if (isdigit(aux_word[0]) || (aux_word[0] == '-' && isdigit(aux_word[1])))
        {
            Word word;
            // Leemos el entero (sscanf maneja negativos automáticamente con %d)
            sscanf(line, "%d", &word);

            int address = base_address + offset;

            // Escribir en memoria física (ID 2 = Loader)
            if (bus_write(address, word, 2) == 0)
            {
                // write_log(0, "LOADER: Mem[%d] = %d\n", address, word); // Descomentar para full debug
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

    // Actualizamos el número real de palabras cargadas
    info->n_words = offset;

    fclose(file);
    write_log(1, "LOADER: Carga finalizada. %d palabras escritas.\n", info->n_words);
    return 0;
}