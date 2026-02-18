#ifndef DISK_H
#define DISK_H

#include <stdint.h>
#include "brain.h"

#define DISK_TRACKS 10
#define DISK_CYLINDERS 10
#define DISK_SECTORS 100
#define SECTOR_BYTES 9 // Cada sector almacena exactamente 9 caracteres

// Inicializa el disco. Devuelve 0 ok, -1 error
int disk_init(void);

// Elimina el semáforo/mutex del disco (eliminar el disco pues)
void disk_destroy(void);

// Leer un sector
int disk_read_sector(int track, int cylinder, int sector, char *out_buf);

// Escribir un sector
int disk_write_sector(int track, int cylinder, int sector, const char *in_buf);

#endif // DISK_H