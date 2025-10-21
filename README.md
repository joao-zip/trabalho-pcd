# Projeto PCD: K-means 1D Paralelo

Implementação sequencial e paralela (OpenMP) do algoritmo K-means 1D para a disciplina de Programação Concorrente e Distribuída.

Estrutura do Projeto

O projeto está organizado da seguinte forma:

/
├── data/             # Pasta para armazenar os datasets gerados (.csv) - CRIE ESTA PASTA
├── serial/           # Contém o código-fonte e executável da versão serial
├── openmp/           # Contém o código-fonte e executável da versão OpenMP
├── gerador_dados.py  # Script para criar os datasets
└── README.md         # Este arquivo


Atenção: Crie a pasta data/ manualmente antes de gerar os datasets.

1. Geração dos Datasets

Os testes de desempenho devem ser feitos com conjuntos de dados grandes para que os ganhos de paralelização sejam evidentes.

Requisitos:

Python 3

Bibliotecas: numpy

O script gerador_dados.py é usado para criar os arquivos dados.csv e centroides_iniciais.csv.

Exemplos de Geração

Todos os comandos devem ser executados a partir da pasta raiz do projeto.

Conjunto Médio (N=100.000, K=8)

python gerador_dados.py -n 100000 -k 8 --dados ./data/dados_medio.csv --centroides ./data/centroides_medio.csv


Conjunto Grande (N=1.000.000, K=16)

python gerador_dados.py -n 1000000 -k 16 --dados ./data/dados_grande.csv --centroides ./data/centroides_grande.csv


Conjunto Grande com K maior (para testes CPU-bound)

python gerador_dados.py -n 1000000 -k 128 --dados ./data/dados_grande_k128.csv --centroides ./data/centroides_grande_k128.csv


2. Compilando os Programas

Os comandos de compilação também devem ser executados a partir da pasta raiz.

Versão Serial (Baseline)

gcc -O2 -std=c99 ./serial/kmeans_1d_naive.c -o ./serial/kmeans_1d_naive -lm


Versão OpenMP

(Note a flag -fopenmp)

gcc -O2 -std=c99 -fopenmp ./openmp/kmeans_1d_omp.c -o ./openmp/kmeans_1d_omp -lm


3. Rodando os Programas

Os programas aceitam argumentos de linha de comando para definir os arquivos de entrada e os parâmetros de execução.

Formato do Comando

./[pasta]/[executavel] [dados.csv] [centroides.csv] [max_iter] [eps] [outAssign] [outCentroid] [num_threads]


[pasta]/[executavel]: Caminho para o programa (ex: ./serial/kmeans_1d_naive).

[dados.csv]: Caminho para o arquivo de dados (ex: ./data/dados_grande.csv).

[centroides.csv]: Caminho para o arquivo de centróides (ex: ./data/centroides_grande.csv).

[max_iter]: (Opcional) Número máximo de iterações (padrão: 50).

[eps]: (Opcional) Critério de parada (padrão: 1e-4).

[outAssign]: (Opcional) Nome do arquivo para salvar as atribuições. Use NULL para não salvar.

[outCentroid]: (Opcional) Nome do arquivo para salvar os centróides finais. Use NULL para não salvar.

[num_threads]: (Apenas OpenMP, opcional) Número de threads (padrão: 4).

Exemplo de Execução

1. Rodar a versão Serial (Baseline)
(Para coletar o tempo de base, rode este comando de 3 a 5 vezes e tire a média)

./serial/kmeans_1d_naive ./data/dados_grande.csv ./data/centroides_grande.csv 50 0.0001 NULL NULL


2. Rodar a versão OpenMP (com 8 threads)
(Para medir o speedup, rode de 3 a 5 vezes para cada contagem de thread e tire a média)

./openmp/kmeans_1d_omp ./data/dados_grande.csv ./data/centroides_grande.csv 50 0.0001 NULL NULL 8


3. Rodar a versão OpenMP (com 16 threads)

./openmp/kmeans_1d_omp ./data/dados_grande.csv ./data/centroides_grande.csv 50 0.0001 NULL NULL 16

