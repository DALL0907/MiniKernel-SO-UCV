#ifndef LOG_H
#define LOG_H

// abre el archivo log.txt
int log_init();

// Cierra el archivo de log
void log_close();

/* * Registra un evento.
 * console= 1 para imprimir también en pantalla, 0 solo archivo.
 * format: Cadena de formato estilo printf.
 * ...: Argumentos variables.
 */
void write_log(int console, const char *format, ...); // se usa `...` para N parametros

#endif