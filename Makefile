# Makefile - Difusao de Calor 3D (stencil de Jacobi)
# Mesmas flags de otimizacao em todas as versoes. SEM -ffast-math
# (para que o resultado paralelo seja bit-a-bit igual ao sequencial).

CC       = gcc
CSTD     = -std=c11
OPT      = -O3 -march=native
WARN     = -Wall -Wextra
CFLAGS   = $(OPT) $(CSTD) $(WARN)
LDFLAGS  = -lm

SRC      = src
TOOLS    = tools
BIN      = bin

COMMON   = $(SRC)/common.c $(SRC)/energy.c
COMMON_H = $(SRC)/common.h $(SRC)/energy.h

.PHONY: all clean seq omp tools dirs

all: dirs seq omp compare

dirs:
	@mkdir -p $(BIN)

# ---- Sequencial (baseline / referencia) ----
seq: $(BIN)/heat_seq
$(BIN)/heat_seq: $(SRC)/heat_seq.c $(COMMON) $(COMMON_H) | dirs
	$(CC) $(CFLAGS) $(SRC)/heat_seq.c $(COMMON) -o $@ $(LDFLAGS)

# ---- OpenMP (versao principal dos experimentos) ----
omp: $(BIN)/heat_omp
$(BIN)/heat_omp: $(SRC)/heat_omp.c $(COMMON) $(COMMON_H) | dirs
	$(CC) $(CFLAGS) -fopenmp $(SRC)/heat_omp.c $(COMMON) -o $@ $(LDFLAGS)

# ---- Ferramenta de comparacao ----
compare: $(BIN)/compare
$(BIN)/compare: $(TOOLS)/compare.c | dirs
	$(CC) $(CFLAGS) $(TOOLS)/compare.c -o $@ $(LDFLAGS)

# ---- Verificacao rapida de corrida (grade pequena) ----
tsan: dirs
	$(CC) $(CFLAGS) -fopenmp -fsanitize=thread $(SRC)/heat_omp.c $(COMMON) -o $(BIN)/heat_omp_tsan $(LDFLAGS)

clean:
	rm -rf $(BIN) *.bin
