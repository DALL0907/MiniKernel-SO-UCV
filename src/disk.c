#include "disk.h"
#include "log.h"
#include <pthread.h>
#include <string.h>

// Inicializar un disco como un arreglo global estático tridimensional (es de 4D para simplificar las cosas)
// Cada sector almacena exactamente 9 caracteres
static char DISK[DISK_TRACKS][DISK_CYLINDERS][DISK_SECTORS][SECTOR_BYTES];

// Se garantiza que solo un hilo accederá al disco a la vez mediante el bus
static pthread_mutex_t disk_lock;

void disk_init(void)
{
    // Inicializar todas las posiciones con una cadena vacía con memset
    memset(DISK, '\0', sizeof(DISK));
    //Verificar errores que pueda arrojar esta funcion de abajo
    pthread_mutex_init(&disk_lock, NULL);
}

int disk_read_sector(int track, int cylinder, int sector, char *out_buf)
{
    // Validar parámetros
    if (track < 0 || track >= DISK_TRACKS ||
        cylinder < 0 || cylinder >= DISK_CYLINDERS ||
        sector < 0 || sector >= DISK_SECTORS ||
        out_buf == NULL)
    {
        return -1;
    }

    // Bloquear el acceso al disco
    pthread_mutex_lock(&disk_lock);

    // Copiar el contenido del sector al buffer de salida
    memcpy(out_buf, DISK[track][cylinder][sector], SECTOR_BYTES);
    write_log(0, "Leyendo en disco: pista %d, cilindro %d, sector %d, data: %.9s\n",
              track, cylinder, sector, out_buf);

    // Desbloquear el acceso al disco
    pthread_mutex_unlock(&disk_lock);
    return 0;
}

int disk_write_sector(int track, int cylinder, int sector, const char *in_buf)
{
    // Validar parámetros
    if (track < 0 || track >= DISK_TRACKS ||
        cylinder < 0 || cylinder >= DISK_CYLINDERS ||
        sector < 0 || sector >= DISK_SECTORS ||
        in_buf == NULL)
    {
        return -1;
    }
    // Bloquear el acceso al disco
    pthread_mutex_lock(&disk_lock);

    // Copiar el contenido del buffer de entrada al sector
    memcpy(DISK[track][cylinder][sector], in_buf, SECTOR_BYTES);
    write_log(0, "Escribiendo en disco: pista %d, cilindro %d, sector %d, data: %.9s\n",
              track, cylinder, sector, in_buf);

    // Desbloquear el acceso al disco
    pthread_mutex_unlock(&disk_lock);
    return 0;
}

void disk_destroy(void)
{
    //Verificar errores que pueda arrojar esta funcion de abajo
    pthread_mutex_destroy(&disk_lock);
}