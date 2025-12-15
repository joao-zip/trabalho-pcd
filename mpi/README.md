# K-means 1D - Implementação MPI

Implementação paralela do algoritmo K-means em 1 dimensão usando MPI

## Compilação

```bash
mpicc -O2 -std=c99 kmeans_1d_mpi.c -o kmeans_1d_mpi -lm
```

## Uso

```bash
mpirun -np 4 ./kmeans_1d_mpi dados.csv centroides_iniciais.csv
```

### Parâmetros

- `dados.csv`: Arquivo CSV com N pontos (1 valor por linha, sem cabeçalho)
- `centroides_iniciais.csv`: Arquivo CSV com K centróides iniciais (1 valor por linha)

### Exemplo pra variar os processos

```bash
for T in 1 2 4 8 16 32; do
    echo "=== Processos: $T ==="
    mpirun --oversubscribe -np $T ./kmeans_1d_mpi ../data/dados_medio.csv ../data/centroides_medio.csv
done
```