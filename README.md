# Projeto PCD: K-means 1D Paralelo

Implementação sequencial e paralela (OpenMP) do algoritmo K-means 1D para a disciplina de Programação Concorrente e Distribuída.

## Estrutura do Projeto

O projeto está organizado da seguinte forma:

├── data/             # Pasta para armazenar os datasets gerados

├── serial/           # Contém o código-fonte e executável da versão serial

├── openmp/           # Contém o código-fonte e executável da versão OpenMP

├── gerador_dados.py  # Script para criar os datasets

└── README.md         # Este arquivo

## Geração dos Datasets

```{python}
python gerador_dados.py -n 100000 -k 8 --dados dados_medio.csv --centroides centroides_medio.csv
```
## Compilando os Programas

```{c
gcc -O2 -std=c99 ./serial/kmeans_1d_naive.c -o ./serial/kmeans_1d_naive -lm
```

Versão OpenMP

```{c}
gcc -O2 -std=c99 -fopenmp ./openmp/kmeans_1d_omp.c -o ./openmp/kmeans_1d_omp -lm
```

## Rodando os Programas
```
./[pasta]/[executavel] [dados.csv] [centroides.csv] [max_iter] [eps] [outAssign] [outCentroid]
```
