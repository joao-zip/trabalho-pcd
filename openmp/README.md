# K-means 1D - Implementação OpenMP

Implementação paralela do algoritmo K-means em 1 dimensão usando OpenMP (CPU com memória compartilhada).

## Compilação

```bash
gcc -O2 -fopenmp -std=c99 kmeans_1d_omp.c -o kmeans_1d_omp -lm
```

## Uso

```bash
export OMP_NUM_THREADS=4
./kmeans_1d_omp dados.csv centroides_iniciais.csv [max_iter] [eps] [assign.csv] [centroids.csv]
```

### Parâmetros

- `dados.csv`: Arquivo CSV com N pontos (1 valor por linha, sem cabeçalho)
- `centroides_iniciais.csv`: Arquivo CSV com K centróides iniciais (1 valor por linha)
- `max_iter` (opcional): Número máximo de iterações (padrão: 50)
- `eps` (opcional): Critério de parada (variação relativa do SSE, padrão: 1e-4)
- `assign.csv` (opcional): Arquivo de saída com atribuições de cluster
- `centroids.csv` (opcional): Arquivo de saída com centróides finais

### Exemplo

```bash
# Testar com diferentes números de threads
for T in 1 2 4 8 16; do
    echo "=== Threads: $T ==="
    export OMP_NUM_THREADS=$T
    ./kmeans_1d_omp ../data/dados_medio.csv ../data/centroides_medio.csv 50 1e-6
done
```

## Paralelização

### Assignment Step
- **Paralelizado**: `#pragma omp parallel for reduction(+:sse)`
- Cada thread processa um subconjunto de pontos independentemente
- Reduction clause acumula SSE de todas as threads

### Update Step
- **Estratégia**: Acumuladores locais por thread
- Cada thread mantém arrays locais `sum_local[]` e `cnt_local[]`
- Após o loop paralelo, usa `#pragma omp atomic` para combinar resultados
- Evita contenção comparado a usar critical sections no loop

## Medições de Desempenho

Para medir speedup e eficiência:

```bash
# Baseline (1 thread)
OMP_NUM_THREADS=1 ./kmeans_1d_omp dados.csv centroides.csv

# Escalonamento
OMP_NUM_THREADS=2 ./kmeans_1d_omp dados.csv centroides.csv
OMP_NUM_THREADS=4 ./kmeans_1d_omp dados.csv centroides.csv
OMP_NUM_THREADS=8 ./kmeans_1d_omp dados.csv centroides.csv
```

Calcular:
- **Speedup**: `tempo_serial / tempo_paralelo`
- **Eficiência**: `speedup / num_threads × 100%`

## Otimizações Possíveis

1. **Schedule clause**: Testar `static`, `dynamic`, `guided`
   ```c
   #pragma omp parallel for schedule(dynamic, 64) reduction(+:sse)
   ```

2. **Chunk size**: Ajustar para balance de carga ideal

3. **False sharing**: Já evitado com acumuladores locais

## Saída

Igual à versão serial, mas com medição de tempo usando `omp_get_wtime()` para maior precisão.

## Validação

Para garantir corretude, compare resultados com a versão serial:
- SSE final deve ser idêntico (dentro de tolerância numérica)
- Centróides finais devem ser os mesmos
- Número de iterações deve ser igual
