# Compilador y Banderas
CC = gcc
# -Wall: Muestra todos los warnings
# -g: Agrega informacion de debug (util para gdb y valgrind)
# -pthread: NECESARIO para que dma.c funcione con hilos
CFLAGS = -Wall -g -pthread

# Archivos Objeto (Resultados de compilar cada .c)
# SE AGREGÓ kernel.o AQUÍ
OBJS = main.o cpu.o memory.o bus.o disk.o dma.o load.o log.o kernel.o

# Nombre del ejecutable final
EXEC = simulador

# Regla Principal (lo que se ejecuta al escribir 'make')
all: $(EXEC)

# Como crear el ejecutable final linkeando los objetos
$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Regla generica para compilar cualquier .c a .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Regla para limpiar archivos temporales ('make clean')
# Asegura borrar log.txt y el ejecutable para entregar limpio
clean:
	rm -f $(OBJS) $(EXEC) log.txt

# Regla para probar rapido ('make run')
run: $(EXEC)
	./$(EXEC)

# Regla para empaquetar la entrega ('make zip')
# Comprime solo los .c, .h y el Makefile (ignora ejecutables y .o)
zip: clean
	zip -r SO_Proy2_CII_CI2_CI3.zip *.c *.h Makefile