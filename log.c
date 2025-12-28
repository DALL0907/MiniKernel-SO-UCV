#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static FILE *log_file = NULL;

void log_init()
{
    // Abre el archivo log.txt en modo escritura (sobrescribe si ya existe)
    log_file = fopen("log.txt", "w");
    if (log_file == NULL)
    {
        perror("Error al abrir el archivo de log");
    }
    else
    {
        write_log(0, "Log iniciado.\n");
    }
}

void log_close()
{
    if (log_file != NULL)
    {
        write_log(0, "Log cerrado.\n");
        fclose(log_file);
        log_file = NULL;
    }
}

void write_log(int console, const char *format, ...)
{
    if (log_file == NULL)
    {
        return; // Si el archivo no está abierto, no hacer nada
    }
    va_list parametros; // es como un puntero para los argumentos variables

    // Obtener la hora actual
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[20];
    // Formato: [2025-12-25 10:30:00]
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    // Escribir el tiempo en el archivo de log
    fprintf(log_file, "[%s] ", time_str);

    // Manejar los argumentos variables
    va_start(parametros, format); // inicializa parametros que es un va_list
    vfprintf(log_file, format, parametros);
    va_end(parametros);

    // Asegurarse de que se escriba inmediatamente
    fflush(log_file);

    // Si se solicita, también imprimir en la consola
    if (console)
    {
        va_start(parametros, format); // se inicializa de nuevo, el escribir en log lo dejo apuntando a la parte final de la lista de args
        vprintf(format, parametros);
        va_end(parametros);
    }
}