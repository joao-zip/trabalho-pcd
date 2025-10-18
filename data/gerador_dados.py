import numpy as np
import argparse
import random

def gerar_dados_kmeans(N, K, arquivo_dados, arquivo_centroides, seed=42):
    """
    Gera dados para o problema K-means 1D com clusters pré-definidos.

    Args:
        N (int): Número total de pontos.
        K (int): Número de clusters (e centróides).
        arquivo_dados (str): Caminho para salvar os pontos de dados.
        arquivo_centroides (str): Caminho para salvar os centróides iniciais.
        seed (int): Semente para reprodutibilidade dos resultados[cite: 105].
    """
    print(f"Gerando {N} pontos para {K} clusters...")
    np.random.seed(seed)
    random.seed(seed)

    # 1. Gerar os centróides "reais" em torno dos quais os dados se agruparão
    # Por exemplo, para K=4, podemos ter clusters em torno de 100, 500, 900, 1300.
    centros_reais = [i * 400 + np.random.uniform(-50, 50) for i in range(K)]
    
    # 2. Gerar os pontos de dados agrupados em torno desses centros
    pontos = []
    pontos_por_cluster = N // K
    desvio_padrao = 50

    for centro in centros_reais:
        cluster = np.random.normal(loc=centro, scale=desvio_padrao, size=pontos_por_cluster)
        pontos.extend(cluster)

    # Embaralhar os pontos
    np.random.shuffle(pontos)

    # 3. Escolher os centróides iniciais para o algoritmo
    centroides_iniciais = random.sample(pontos, K)

    # 4. Salvar os arquivos no formato CSV de uma coluna [cite: 18]
    np.savetxt(arquivo_dados, pontos, fmt='%.4f')
    print(f"Arquivo de dados '{arquivo_dados}' salvo com {len(pontos)} linhas.")

    np.savetxt(arquivo_centroides, centroides_iniciais, fmt='%.4f')
    print(f"Arquivo de centróides '{arquivo_centroides}' salvo com {len(centroides_iniciais)} linhas.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Gerador de Dados para K-means 1D")
    parser.add_argument('-n', type=int, default=100000, help='Número total de pontos (N)')
    parser.add_argument('-k', type=int, default=8, help='Número de clusters (K)')
    parser.add_argument('--dados', type=str, default='dados.csv', help='Nome do arquivo de saída para os dados')
    parser.add_argument('--centroides', type=str, default='centroides_iniciais.csv', help='Nome do arquivo de saída para os centróides')
    parser.add_argument('--seed', type=int, default=42, help='Semente para o gerador de números aleatórios')
    
    args = parser.parse_args()
    
    gerar_dados_kmeans(args.n, args.k, args.dados, args.centroides, args.seed)
