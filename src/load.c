#include "load.h"
#include "disk.h"
#include "kernel.h"
#include "bus.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_WORDS_BUFFER 1000

/**
 * Lee un archivo .txt y lo almacena en un buffer
 * 
 * Retorna:
 *   - Numero de palabras leidas (>= 0) si exito
 *   - -1 si error
 */
static int read_program_file(const char *filename, Word *words_buffer,
                             int *out_n_start, char *out_prog_name)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        write_log(1, "LOADER: No se pudo abrir el archivo: %s\n", filename);
        return -1;
    }

    char line[256];
    char aux_word[100];
    int word_count = 0;
    int n_start = 0;
    int declared_words = -1;
    char prog_name[50] = {0};

    write_log(1, "LOADER: Leyendo archivo %s desde PC real...\n", filename);

    while (fgets(line, sizeof(line), file))
    {
        // 1. Leer primera palabra de la linea
        if (sscanf(line, "%s", aux_word) != 1)
            continue;

        // 2. Ignorar comentarios
        if (strncmp(aux_word, "//", 2) == 0)
            continue;

        // 3. PROCESAR DIRECTIVAS
        if (strcmp(aux_word, "_start") == 0)
        {
            int lineaStart;
            sscanf(line, "%*s %d", &lineaStart);

            // Ajuste de Base 1 a Base 0
            if (lineaStart > 0)
                n_start = lineaStart - 1;
            else
                n_start = 0;

            write_log(0, "LOADER: Directiva _start %d -> n_start = %d\n", lineaStart, n_start);
            continue;
        }

        if (strcmp(aux_word, ".NumeroPalabras") == 0)
        {
            sscanf(line, "%*s %d", &declared_words);
            write_log(0, "LOADER: Palabras declaradas: %d\n", declared_words);
            continue;
        }

        if (strcmp(aux_word, ".NombreProg") == 0)
        {
            sscanf(line, "%*s %s", prog_name);
            write_log(0, "LOADER: Nombre del programa: %s\n", prog_name);
            continue;
        }

        // 4. PROCESAR INSTRUCCIONES / DATOS
        if (isdigit(aux_word[0]) || (aux_word[0] == '-' && isdigit(aux_word[1])))
        {
            // Validar que no superemos el buffer
            if (word_count >= MAX_WORDS_BUFFER)
            {
                write_log(1, "LOADER ERROR: Buffer de palabras desbordado (max %d).\n", MAX_WORDS_BUFFER);
                fclose(file);
                return -1;
            }

            Word word;
            sscanf(line, "%d", &word);
            words_buffer[word_count] = word;
            word_count++;
        }
    }

    fclose(file);

    // Validacion estricta de cantidad
    if (declared_words != -1 && word_count != declared_words)
    {
        write_log(1, "LOADER ERROR: Inconsistencia. Declaradas: %d, Leidas: %d.\n",
                  declared_words, word_count);
        return -1;
    }

    *out_n_start = n_start;
    strncpy(out_prog_name, prog_name, 49);
    out_prog_name[49] = '\0';

    write_log(0, "LOADER: Archivo parseado exitosamente. Total: %d palabras.\n", word_count);
    return word_count;
}

/**
 * Se escribe el programa en el disco
 *
 * Retorna: 0 si exito, -1 si error
 */
static int write_program_to_disk(const Word *words_buffer, int word_count,
                                  int track, int cylinder, int sector_start)
{
    write_log(1, "LOADER: Escribiendo %d palabras en disco (Track=%d, Cyl=%d, Sec=%d)...\n",
              word_count, track, cylinder, sector_start);

    // Buffer temporal para un sector (9 bytes maximo segun SECTOR_BYTES)
    char sector_buffer[SECTOR_BYTES];

    int current_track = track;
    int current_cylinder = cylinder;
    int current_sector = sector_start;
    int words_written = 0;

    // Escribir cada palabra
    for (int i = 0; i < word_count; i++)
    {
        // Convertir palabra a string (maximo 8 digitos segun WORD_DIGITS)
        char word_str[20];
        snprintf(word_str, sizeof(word_str), "%08d", words_buffer[i]);

        // Cada palabra se guarda en un sector separado (simplificado)
        memset(sector_buffer, 0, SECTOR_BYTES);
        strncpy(sector_buffer, word_str, SECTOR_BYTES - 1);

        // Escribir en disco
        if (disk_write_sector(current_track, current_cylinder, current_sector, sector_buffer) != 0)
        {
            write_log(1, "LOADER ERROR: Fallo al escribir sector (%d,%d,%d).\n",
                      current_track, current_cylinder, current_sector);
            return -1;
        }

        words_written++;
        write_log(0, "LOADER: Palabra %d escrita en sector (%d,%d,%d).\n",
                  i, current_track, current_cylinder, current_sector);

        // Avanzar al siguiente sector
        current_sector++;

        // Si se alcanzo el limite de sectores en un cilindro, avanzar
        if (current_sector >= DISK_SECTORS)
        {
            current_sector = 0;
            current_cylinder++;

            // Si se alcanzo el limite de cilindros, avanzar a siguiente pista
            if (current_cylinder >= DISK_CYLINDERS)
            {
                current_cylinder = 0;
                current_track++;

                // Si se alcanzo el limite de pistas, ERROR
                if (current_track >= DISK_TRACKS)
                {
                    write_log(1, "LOADER ERROR: Disco lleno. No hay espacio para todas las palabras.\n");
                    return -1;
                }
            }
        }
    }

    write_log(0, "LOADER: %d palabras escritas en disco exitosamente.\n", words_written);
    return 0;
}

/**
 * Lee palabras del disco virtual y las almacena en el buffer temporal.
 *
 * Retorna: 0 si exito, -1 si error
 */
static int read_program_from_disk(Word *words_buffer, int word_count,
                                   int track, int cylinder, int sector_start)
{
    write_log(1, "LOADER: Leyendo %d palabras desde disco (Track=%d, Cyl=%d, Sec=%d)...\n",
              word_count, track, cylinder, sector_start);

    char sector_buffer[SECTOR_BYTES];

    int current_track = track;
    int current_cylinder = cylinder;
    int current_sector = sector_start;
    int words_read = 0;

    // Leer cada palabra del disco
    for (int i = 0; i < word_count; i++)
    {
        // Leer sector
        if (disk_read_sector(current_track, current_cylinder, current_sector, sector_buffer) != 0)
        {
            write_log(1, "LOADER ERROR: Fallo al leer sector (%d,%d,%d).\n",
                      current_track, current_cylinder, current_sector);
            return -1;
        }

        // Convertir string a palabra
        Word word = atoi(sector_buffer);
        words_buffer[i] = word;
        words_read++;

        write_log(0, "LOADER: Palabra %d leida desde sector (%d,%d,%d): %d\n",
                  i, current_track, current_cylinder, current_sector, word);

        // Avanzar al siguiente sector
        current_sector++;

        if (current_sector >= DISK_SECTORS)
        {
            current_sector = 0;
            current_cylinder++;

            if (current_cylinder >= DISK_CYLINDERS)
            {
                current_cylinder = 0;
                current_track++;

                if (current_track >= DISK_TRACKS)
                {
                    write_log(1, "LOADER ERROR: Limite de disco alcanzado durante lectura.\n");
                    return -1;
                }
            }
        }
    }

    write_log(0, "LOADER: %d palabras leidas desde disco exitosamente.\n", words_read);
    return 0;
}

/**
 * CARGAR DE PC REAL -> DISCO VIRTUAL
 * 
 * Lee el codigo fuente y lo carga en disco
 * El programa queda en estado NEW en disco, sin ocupar RAM
 * Se llama desde con el comando CARGAR
 * 
 * Flujo:
 *   1. Lee archivo .txt desde la pc (parsea directivas)
 *   2. Almacena palabras en buffer temporal
 *   3. Escribe todo en disco virtual usando disk_write_sector
 *   4. Crea PCB en tabla de procesos (Estado NEW)
 *   5. Agrega entrada en tabla de archivos (Estado DISK)
 * 
 * Parametros:
 *   filename: Ruta del archivo en PC real (ej: "Casos de prueba/prog1.txt")
 *   program_name: Nombre a asignar en tabla de archivos (ej: "prog1.txt")
 *   track, cylinder, sector: Ubicacion inicial en disco virtual
 * 
 * Retorna: PID del proceso creado si exito, -1 si error
 */
int load_program_to_disk(const char *filename, const char *program_name,
                          int track, int cylinder, int sector)
{
    write_log(1, "LOADER: ===== INICIANDO CARGA PC REAL -> DISCO =====\n");
    write_log(1, "LOADER: Programa: %s, Archivo: %s\n", program_name, filename);

    // PASO 1: Leer archivo desde PC real
    Word *words_buffer = (Word *)malloc(MAX_WORDS_BUFFER * sizeof(Word));
    if (!words_buffer)
    {
        write_log(1, "LOADER ERROR: Fallo al asignar buffer de palabras.\n");
        return -1;
    }

    int n_start = 0;
    char prog_name[50] = {0};
    int word_count = read_program_file(filename, words_buffer, &n_start, prog_name);

    if (word_count < 0)
    {
        write_log(1, "LOADER ERROR: Fallo al leer archivo.\n");
        free(words_buffer);
        return -1;
    }

    write_log(0, "LOADER: Archivo leido. %d palabras en buffer temporal.\n", word_count);

    // PASO 2: Escribir en disco virtual
    if (write_program_to_disk(words_buffer, word_count, track, cylinder, sector) != 0)
    {
        write_log(1, "LOADER ERROR: Fallo al escribir en disco.\n");
        free(words_buffer);
        return -1;
    }

    write_log(0, "LOADER: Programa escrito en disco exitosamente.\n");

    // PASO 3: Crear PCB en tabla de procesos (Estado NEW)
    int pid = create_process(program_name, track, cylinder, sector, word_count);
    if (pid < 0)
    {
        write_log(1, "LOADER ERROR: Fallo al crear PCB.\n");
        free(words_buffer);
        return -1;
    }

    write_log(0, "LOADER: PCB creado. PID=%d\n", pid);

    // PASO 4: Agregar entrada en tabla de archivos (Estado DISK)
    int ft_index = file_table_add_entry(program_name, track, cylinder, sector, word_count, n_start);
    if (ft_index < 0)
    {
        write_log(1, "LOADER ERROR: Fallo al agregar entrada en tabla de archivos.\n");
        free(words_buffer);
        return -1;
    }

    // Asociar el PID con la entrada de la tabla de archivos
    file_table[ft_index].pid = pid;

    write_log(0, "LOADER: Entrada en tabla de archivos creada (indice %d).\n", ft_index);

    // Liberar buffer temporal
    free(words_buffer);

    write_log(1, "LOADER: ===== CARGA PC->DISCO COMPLETADA =====\n");
    write_log(1, "LOADER: PID=%d, Programa=%s, Palabras=%d, n_start=%d\n",
              pid, program_name, word_count, n_start);

    return pid;
}

/**
 * CARGAR DE DISCO VIRTUAL -> RAM
 * 
 * Lee un programa desde el disco y lo carga en ram dentro de una particion
 * Inicializa contexto del proceso (PC, SP, RB, RL, etc)
 * El programa queda en estado READY, listo para ejecutar
 * Se llama con el comando EJECUTAR (cuando se cambia de NEW a READY)
 * 
 * Flujo:
 *   1. Lee programa desde disco a buffer temporal
 *   2. Escribe buffer en particion de RAM asignada
 *   3. Inicializa contexto del proceso (PC en PSW, SP, RB, RL, etc)
 *   4. Actualiza estado PCB a READY
 *   5. Actualiza estado en tabla de archivos a READY
 * 
 * Parametros:
 *   pid: PID del proceso a cargar en RAM
 *   partition_id: ID de la particion asignada (0-4)
 *   file_table_index: Indice en la tabla de archivos
 * 
 * Retorna: 0 si exito, -1 si error
 */
int load_program_to_ram(int pid, int partition_id, int file_table_index)
{
    write_log(1, "LOADER: ===== INICIANDO CARGA DISCO -> RAM =====\n");
    write_log(1, "LOADER: PID=%d, Particion=%d, FT_Index=%d\n", pid, partition_id, file_table_index);

    // Obtener entrada de tabla de archivos
    FileTableEntry *entry = get_file_table_entry(file_table_index);
    if (!entry)
    {
        write_log(1, "LOADER ERROR: Entrada invalida en tabla de archivos.\n");
        return -1;
    }

    // Obtener PCB del proceso
    PCB *pcb = get_pcb(pid);
    if (!pcb)
    {
        write_log(1, "LOADER ERROR: PCB invalido para PID %d.\n", pid);
        return -1;
    }

    write_log(0, "LOADER: Cargando '%s' (PID=%d) a RAM (Particion %d).\n",
              entry->program_name, pid, partition_id);

    // PASO 1: Leer programa desde disco a buffer temporal
    Word *words_buffer = (Word *)malloc(entry->size_words * sizeof(Word));
    if (!words_buffer)
    {
        write_log(1, "LOADER ERROR: Fallo al asignar buffer para lectura desde disco.\n");
        return -1;
    }

    if (read_program_from_disk(words_buffer, entry->size_words,
                               entry->track, entry->cylinder, entry->sector_initial) != 0)
    {
        write_log(1, "LOADER ERROR: Fallo al leer programa desde disco.\n");
        free(words_buffer);
        return -1;
    }

    write_log(0, "LOADER: Programa leido desde disco a buffer. %d palabras.\n", entry->size_words);

    // PASO 2: Calcular direccion base y limite en RAM
    int base_address = MEM_USER_START + (partition_id * PARTITION_SIZE);
    int limit_address = base_address + PARTITION_SIZE - 1;

    write_log(0, "LOADER: Particion %d: direcciones RAM [%d-%d].\n",
              partition_id, base_address, limit_address);

    // PASO 3: Escribir palabras en RAM (usando bus_write)
    // La primera posicion se deja VACIA como marca del inicio de la pila
    int sp_initial = limit_address; // SP apunta al final de la particion

    // Escribir cada palabra en su posicion en RAM
    for (int i = 0; i < entry->size_words; i++)
    {
        int address = base_address + i;

        if (bus_write(address, words_buffer[i], 3) != 0) // client_id = 3 (Loader)
        {
            write_log(1, "LOADER ERROR: Fallo al escribir palabra %d en RAM (dir %d).\n", i, address);
            free(words_buffer);
            return -1;
        }
    }

    write_log(0, "LOADER: Todas las palabras escritas en RAM exitosamente.\n");

    // PASO 4: Inicializar contexto del proceso (registros)
    memset(&pcb->context, 0, sizeof(CPU_Context));

    // Registros de particion
    pcb->context.RB = base_address;              // Registro Base (inicio de particion)
    pcb->context.RL = limit_address;             // Registro Limite (final de particion)
    pcb->context.PSW.PC = entry->n_start;        // Program Counter (dentro de PSW)
    pcb->context.SP = sp_initial;                // Stack Pointer (final de particion)

    // Configuracion del PSW (Program Status Word)
    pcb->context.PSW.Mode = USER_MODE;           // Modo usuario
    pcb->context.PSW.Interrupts = 1;             // Interrupciones habilitadas
    pcb->context.PSW.CC = 0;                     // Codigo Condicion inicial

    // Otros registros
    pcb->context.AC = 0;                         // Acumulador
    pcb->context.MAR = 0;                        // Memory Address Register
    pcb->context.MDR = 0;                        // Memory Data Register
    pcb->context.IR = 0;                         // Instruction Register
    pcb->context.RX = 0;                         // Registro indice

    write_log(0, "LOADER: Contexto inicializado.\n");
    write_log(0, "LOADER:   RB (Base)=%d, RL (Limite)=%d\n", pcb->context.RB, pcb->context.RL);
    write_log(0, "LOADER:   PC (Program Counter)=%d (dentro de PSW)\n", pcb->context.PSW.PC);
    write_log(0, "LOADER:   SP (Stack Pointer)=%d (primera posicion VACIA)\n", pcb->context.SP);

    // PASO 5: Actualizar estado en tabla de archivos
    entry->state = FILE_STATE_READY;
    entry->partition_id = partition_id;

    // PASO 6: Actualizar estado en PCB
    pcb->state = STATE_READY;
    pcb->partition_id = partition_id;

    // Liberar buffer temporal
    free(words_buffer);

    write_log(1, "LOADER: ===== CARGA DISCO->RAM COMPLETADA =====\n");
    write_log(1, "LOADER: PID=%d cargado en Particion %d, listo para ejecutar.\n", pid, partition_id);

    return 0;
}