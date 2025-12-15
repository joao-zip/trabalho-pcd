# K-means 1D - Implementação OpenMP

Implementação paralela do algoritmo K-means em 1 dimensão usando OpenMP

## Compilação

```bash
gcc -O2 -fopenmp -std=c99 kmeans_1d_omp.c -o kmeans_1d_omp -lm
```

## Uso

```bash
export OMP_NUM_THREADS=4
./kmeans_1d_omp dados.csv centroides_iniciais.csv
```

### Parâmetros

- `dados.csv`: Arquivo CSV com N pontos (1 valor por linha, sem cabeçalho)
- `centroides_iniciais.csv`: Arquivo CSV com K centróides iniciais (1 valor por linha)
- `max_iter` (opcional): Número máximo de iterações (padrão: 50)
- `eps` (opcional): Critério de parada (variação relativa do SSE, padrão: 1e-4)
- `assign.csv` (opcional): Arquivo de saída com atribuições de cluster
- `centroids.csv` (opcional): Arquivo de saída com centróides finais

### Exemplo pra variar threads automático em bash

```bash
for T in 1 2 4 8 16 32; do
    echo "=== Threads: $T ==="
    export OMP_NUM_THREADS=$T
    ./kmeans_1d_omp ../data/dados_medio.csv ../data/centroides_medio.csv
done
```