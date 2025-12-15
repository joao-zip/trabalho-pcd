# K-means 1D Paralelo

Implementação de K-means unidimensional em C com três abordagens: sequencial, OpenMP e CUDA.

## Estrutura

```
trabalho-pcd/
 data/                    
 serial/                  
 openmp/                      
 mpi/                     
```

## Como usar

### Gerar dados

```bash
cd data
python3 gerador_dados.py -n 100000 -k 8 --dados dados.csv --centroides centroides.csv
cd ..
```

### Compilar

```bash
# Serial
gcc -O2 -std=c99 serial/kmeans_1d_naive.c -o serial/kmeans_1d_naive -lm

# OpenMP
gcc -O2 -std=c99 -fopenmp openmp/kmeans_1d_omp.c -o openmp/kmeans_1d_omp -lm
```

### Executar

```bash
# Serial
./serial/kmeans_1d_naive data/dados.csv data/centroides.csv 50 1e-6

# OpenMP com 4 threads
export OMP_NUM_THREADS=4
./openmp/kmeans_1d_omp data/dados.csv data/centroides.csv 50 1e-6
```

Formato: `programa dados.csv centroides.csv`

## Configurações de teste

| Dataset | Pontos    | Clusters |
|---------|-----------|----------|
| Pequeno | 10.000    | 4        |
| Médio   | 100.000   | 8        |
| Grande  | 1.000.000 | 16       |