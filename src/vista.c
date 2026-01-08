#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "load.h"
#include "brain.h"

static void print_banner()
{
    printf("\nShell\n");
    printf("Comandos:\n");
    printf("  cargar <archivo>  - Carga un programa en memoria\n");
    printf("  ejecutar          - Ejecuta el programa cargado\n");
    printf("  debug             - Modo paso a paso con estado\n");
    printf("  salir             - Termina el simulador\n\n");
}

void vista_init(){
    int cargado = 0;
    print_banner();
    while (true)
    {
        char comando[256];
        printf("> ");
        fgets(comando, sizeof(comando), stdin);

        // Elimina el salto de línea
        comando[strcspn(comando, "\n")] = '\0';

        if (strcmp(comando, "salir") == 0)
        {
            break;
        }
        else if (strncmp(comando, "cargar ", 7) == 0){
            char filename[248];
            const char *p = comando + 7; // después de "cargar "
            while (*p == ' ') p++;
            if (*p == '\0'){
                printf("Uso: cargar <archivo.txt>\n");
                continue;
            }
            // Leer todo el resto (incluye espacios)
            if (sscanf(p, "%247[^\n]", filename) == 1) {
                // Recortar espacios finales
                size_t len = strlen(filename);
                while (len > 0 && isspace((unsigned char)filename[len-1])) {
                    filename[--len] = '\0';
                }
                printf("[OK] Archivo preparado: '%s'\n", filename);
                // Nota: NO llamamos a load_program aquí
                cargado = 1;
            } else {
                printf("Uso: cargar <archivo.txt>\n");
            }
        }
        else if (strcmp(comando, "ejecutar") == 0 && cargado == 1){
            //Ejecutar
            continue;
        }
        else if (strcmp(comando, "debug") == 0 && cargado == 1){
        
            //debug
            continue;
        }
        else{
            printf("Comando no reconocido. Intente de nuevo.\n");
            print_banner();
        }
    }
}

