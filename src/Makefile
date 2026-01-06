# Compilador y Banderas
CC = gcc
# -Wall: Muestra todos los warnings
# -g: Agrega informacion de debug (util para gdb)
# -pthread: NECESARIO para que dma.c funcione con hilos
CFLAGS = -Wall -g -pthread

# Archivos Objeto (Resultados de compilar cada .c)
OBJS = main.o cpu.o memory.o bus.o disk.o dma.o load.o log.o

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
clean:
	rm -f $(OBJS) $(EXEC) log.txt

# Regla para probar rapido ('make run')
run: $(EXEC)
	./$(EXEC)