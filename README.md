# Projeto PCD: K-means 1D "Naive"

Implementação sequencial e paralela (OpenMP, CUDA e MPI) do algoritmo K-means 1D para a disciplina de Programação Concorrente e Distribuída.

## Estrutura do Projeto

```
trabalho-pcd/
├── data/                    # Gerador e dados de teste
│   ├── gerador_dados.py
│   └── README.md
├── serial/                  # Implementação sequencial (baseline)
│   ├── kmeans_1d_naive.c
│   └── README.md
├── openmp/                  # Implementação OpenMP
│   ├── kmeans_1d_omp.c
│   └── README.md
├── cuda/                    # Implementação CUDA
│   ├── kmeans_1d_cuda.cu
│   └── README.md
├── mpi/                     # Implementação MPI
└── README.md
```

## Início Rápido

### 1. Gerar Dados de Teste

```bash
cd data
python3 gerador_dados.py -n 100000 -k 8 --dados dados_medio.csv --centroides centroides_medio.csv
```

### 2. Compilar Programas

**Versão Serial:**
```bash
gcc -O2 -std=c99 ./serial/kmeans_1d_naive.c -o ./serial/kmeans_1d_naive -lm
```

**Versão OpenMP:**
```bash
gcc -O2 -std=c99 -fopenmp ./openmp/kmeans_1d_omp.c -o ./openmp/kmeans_1d_omp -lm
```

### 3. Executar Programas

**Formato geral:**
```bash
./[pasta]/[executavel] [dados.csv] [centroides.csv] [max_iter] [eps] [assign.csv] [centroids.csv]
```

**Exemplo Serial:**
```bash
./serial/kmeans_1d_naive data/dados_medio.csv data/centroides_medio.csv 50 1e-6 assign.csv centroids.csv
```

**Exemplo OpenMP:**
```bash
export OMP_NUM_THREADS=4
./openmp/kmeans_1d_omp data/dados_medio.csv data/centroides_medio.csv 50 1e-6 assign.csv centroids.csv
```

**Exemplo CUDA** (requer GPU NVIDIA)
```bash
nvcc -O2 cuda/kmeans_1d_cuda.cu -o cuda/kmeans_1d_cuda
./cuda/kmeans_1d_cuda data/dados_grande.csv data/centroides_grande.csv 50 1e-6 assign.csv centroids.csv 256
```

### 4. Análise Visual (Notebook Jupyter)

Para análises:

```bash
jupyter notebook analise_desempenho.ipynb
```

O notebook inclui:
- Visualização de clusters
- Gráficos de speedup e eficiência
- Comparação de tempos de execução
- Tabelas de resultados

### Métricas

- **SSE** (Sum of Squared Errors): Soma das distâncias ao quadrado de cada ponto ao seu centróide

## Conjuntos de Teste Sugeridos

| Nome    | N         | K  | Uso                          |
|---------|-----------|----|-----------------------------|
| Pequeno | 10,000    | 4  | Testes rápidos, debug       |
| Médio   | 100,000   | 8  | Análise de escalonamento    |
| Grande  | 1,000,000 | 16 | Desempenho máximo           |

## Validação e Testes

### Critérios de Corretude
- SSE consistente entre serial e OpenMP (tolerância < 1e-3)
- SSE não aumenta entre iterações
- Convergência em número similar de iterações

## Dependências

### Compilação
- GCC 7.0+ com suporte C99 e OpenMP
- `libm` (biblioteca matemática)

### Python (testes e análise)
```bash
pip install numpy pandas matplotlib seaborn jupyter
```
## Referências

1. Lloyd, S. (1982). "Least squares quantization in PCM". IEEE Transactions on Information Theory.
2. Arthur, D., & Vassilvitskii, S. (2007). "k-means++: The advantages of careful seeding". SODA.
3. OpenMP Architecture Review Board. "OpenMP Application Programming Interface"