# K-means 1D - Implementação Serial

Implementação sequencial (baseline) do algoritmo K-means em 1 dimensão.

## Compilação

```bash
gcc -O2 -std=c99 kmeans_1d_naive.c -o kmeans_1d_naive -lm
```

## Uso

```bash
./kmeans_1d_naive dados.csv centroides_iniciais.csv [max_iter] [eps] [assign.csv] [centroids.csv]
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
./kmeans_1d_naive ../data/dados_pequeno.csv ../data/centroides_pequeno.csv 50 1e-6 assign.csv centroids.csv
```

## Saída

O programa imprime no terminal:
- Número de pontos (N) e clusters (K)
- Número de iterações realizadas
- SSE (Sum of Squared Errors) final
- Tempo total de execução em milissegundos

## Algoritmo

1. **Assignment**: Para cada ponto, encontra o centróide mais próximo (distância Euclidiana ao quadrado)
2. **Update**: Recalcula cada centróide como a média dos pontos atribuídos a ele
3. Repete até convergência (variação do SSE < eps) ou max_iter

## Notas

- Se um cluster ficar vazio durante o update, ele recebe o primeiro ponto (X[0]) como estratégia simples
- Usa `clock()` da stdlib para medição de tempo
