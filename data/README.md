# Gerador de Dados para K-means 1D

Script Python para gerar conjuntos de dados sintéticos para teste do K-means.

## Dependências

```bash
pip install numpy
```

## Uso

```bash
python3 gerador_dados.py -n <N> -k <K> [opções]
```

### Parâmetros

- `-n`: Número total de pontos (N)
- `-k`: Número de clusters (K)
- `--dados`: Nome do arquivo de saída para os dados (padrão: `dados.csv`)
- `--centroides`: Nome do arquivo de saída para centróides iniciais (padrão: `centroides_iniciais.csv`)
- `--seed`: Semente para reprodutibilidade (padrão: 42)

### Exemplos

```bash
# Dataset pequeno (10K pontos, 4 clusters)
python3 gerador_dados.py -n 10000 -k 4 --dados dados_pequeno.csv --centroides centroides_pequeno.csv

# Dataset médio (100K pontos, 8 clusters)
python3 gerador_dados.py -n 100000 -k 8 --dados dados_medio.csv --centroides centroides_medio.csv

# Dataset grande (1M pontos, 16 clusters)
python3 gerador_dados.py -n 1000000 -k 16 --dados dados_grande.csv --centroides centroides_grande.csv
```

## Como Funciona

1. **Geração de Centros Reais**: Cria K centros espaçados uniformemente
2. **Distribuição de Pontos**: Gera pontos em torno de cada centro usando distribuição normal
3. **Embaralhamento**: Randomiza a ordem dos pontos
4. **Centróides Iniciais**: Seleciona K pontos espaçados como centróides iniciais

## Características dos Dados

- **Espaçamento**: Clusters são bem separados para facilitar convergência
- **Desvio Padrão**: ~15% do espaçamento entre clusters
- **Distribuição**: Normal (Gaussiana) em torno de cada centro
- **Reprodutibilidade**: Mesma seed gera mesmos dados

## Formato de Saída

Ambos os arquivos CSV têm formato simples:
- 1 valor por linha
- Sem cabeçalho
- Valores em ponto flutuante

### Exemplo dados.csv
```
123.4567
456.7890
789.0123
...
```

### Exemplo centroides_iniciais.csv
```
100.0000
500.0000
900.0000
1300.0000
```
