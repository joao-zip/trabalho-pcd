/*
 * kmeans_1d_mpi.c
 * Solução para Etapa 3 - MPI (Memória Distribuída)
 * Compilar: mpicc -O2 -std=c99 kmeans_1d_mpi.c -o kmeans_1d_mpi -lm
 * Executar: mpirun -np 4 ./kmeans_1d_mpi dados.csv centroides_iniciais.csv
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mpi.h>

// --- Funções Auxiliares de Leitura (Reaproveitadas do Naive) ---

static int count_rows(const char *path) {
    FILE *f = fopen(path, "r");
    if(!f) return -1;
    int rows = 0; 
    char line[8192];
    while(fgets(line, sizeof(line), f)){
        int only_ws = 1;
        for(char *p=line; *p; p++) {
            if(*p!=' ' && *p!='\t' && *p!='\n' && *p!='\r'){ only_ws=0; break; }
        }
        if(!only_ws) rows++;
    }
    fclose(f);
    return rows;
}

static double *read_csv_1col(const char *path, int *n_out){
    int R = count_rows(path);
    if(R <= 0) return NULL;
    double *A = (double*)malloc((size_t)R * sizeof(double));
    if(!A) return NULL;
    
    FILE *f = fopen(path, "r");
    if(!f) { free(A); return NULL; }
    
    char line[8192];
    int r = 0;
    while(fgets(line, sizeof(line), f)){
        int only_ws = 1;
        for(char *p=line; *p; p++) {
            if(*p!=' ' && *p!='\t' && *p!='\n' && *p!='\r'){ only_ws=0; break; }
        }
        if(only_ws) continue;
        
        char *tok = strtok(line, ",; \t");
        if(tok) {
            A[r++] = atof(tok);
        }
        if(r >= R) break;
    }
    fclose(f);
    *n_out = r;
    return A;
}

static void write_assign_csv(const char *path, const int *assign, int N){
    if(!path) return;
    FILE *f = fopen(path, "w");
    if(!f) { fprintf(stderr, "Erro escrita assign\n"); return; }
    for(int i=0; i<N; i++) fprintf(f, "%d\n", assign[i]);
    fclose(f);
}

static void write_centroids_csv(const char *path, const double *C, int K){
    if(!path) return;
    FILE *f = fopen(path, "w");
    if(!f) { fprintf(stderr, "Erro escrita centroids\n"); return; }
    for(int c=0; c<K; c++) fprintf(f, "%.6f\n", C[c]);
    fclose(f);
}

// --- Funções do Kernel K-Means (Adaptadas para processamento local) ---

// Calcula assignment local e retorna SSE local
static double local_assignment(const double *local_X, int local_N, 
                               const double *C, int K, 
                               int *local_assign) 
{
    double sse = 0.0;
    for(int i = 0; i < local_N; i++) {
        double val = local_X[i];
        int best = -1;
        double best_dist = 1e300;
        
        for(int c = 0; c < K; c++) {
            double diff = val - C[c];
            double d2 = diff * diff;
            if(d2 < best_dist) {
                best_dist = d2;
                best = c;
            }
        }
        local_assign[i] = best;
        sse += best_dist;
    }
    return sse;
}

// Prepara somas locais para o update
static void local_sums_counts(const double *local_X, int local_N, 
                              const int *local_assign, int K,
                              double *local_sum, int *local_cnt)
{
    // Zera acumuladores
    for(int c=0; c<K; c++) {
        local_sum[c] = 0.0;
        local_cnt[c] = 0;
    }
    
    for(int i=0; i < local_N; i++) {
        int c = local_assign[i];
        local_sum[c] += local_X[i];
        local_cnt[c] += 1;
    }
}

// --- Main ---

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if(argc < 3 && rank == 0){
        printf("Uso: %s dados.csv centroides.csv [max_iter] [eps] [outAssign] [outCent]\n", argv[0]);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Parâmetros de configuração
    int max_iter = (argc > 3) ? atoi(argv[3]) : 50;
    double eps   = (argc > 4) ? atof(argv[4]) : 1e-4;
    const char *outAssign = (argc > 5) ? argv[5] : NULL;
    const char *outCentroid = (argc > 6) ? argv[6] : NULL;

    // Variáveis globais (apenas rank 0 aloca tudo, outros alocam parcial)
    double *X_full = NULL;
    int N_total = 0;
    int K = 0;
    double *C = NULL; // Todos terão cópia de C

    // 1. Leitura de dados (Apenas Rank 0)
    if(rank == 0) {
        X_full = read_csv_1col(argv[1], &N_total);
        if(!X_full) { fprintf(stderr, "Erro ler dados\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
        
        double *C_tmp = read_csv_1col(argv[2], &K);
        if(!C_tmp) { fprintf(stderr, "Erro ler centroides\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
        
        C = C_tmp; // Rank 0 já tem C
    }

    // 2. Broadcast de metadados (N e K)
    MPI_Bcast(&N_total, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&K, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Aloca C em todos os outros processos
    if(rank != 0) {
        C = (double*)malloc(K * sizeof(double));
    }
    
    // Broadcast inicial de C 
    MPI_Bcast(C, K, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    // 3. Distribuição dos pontos (Scatterv para suportar N não divisível)
    // Calcula quantos pontos cada processo vai receber
    int *sendcounts = NULL;
    int *displs = NULL;
    int local_N = N_total / size;
    int remainder = N_total % size;

    if(rank == 0) {
        sendcounts = (int*)malloc(size * sizeof(int));
        displs = (int*)malloc(size * sizeof(int));
        int sum = 0;
        for(int i=0; i<size; i++) {
            sendcounts[i] = local_N + (i < remainder ? 1 : 0);
            displs[i] = sum;
            sum += sendcounts[i];
        }
    }

    // Ajusta o local_N do próprio processo (para alocação)
    int my_N = local_N + (rank < remainder ? 1 : 0);
    double *local_X = (double*)malloc(my_N * sizeof(double));
    int *local_assign = (int*)malloc(my_N * sizeof(int));

    // Distribui os dados
    MPI_Scatterv(X_full, sendcounts, displs, MPI_DOUBLE, 
                 local_X, my_N, MPI_DOUBLE, 
                 0, MPI_COMM_WORLD);

    // Buffers auxiliares para redução
    double *local_sum = (double*)malloc(K * sizeof(double));
    int *local_cnt = (int*)malloc(K * sizeof(int));
    double *global_sum = (double*)malloc(K * sizeof(double));
    int *global_cnt = (int*)malloc(K * sizeof(int));

    // --- LOOP DO K-MEANS ---
    double prev_sse = 1e300;
    double global_sse = 0.0;
    int it = 0;
    
    double start_time = MPI_Wtime();

    for(it = 0; it < max_iter; it++) {
        
        // A. Assignment Local 
        double my_sse = local_assignment(local_X, my_N, C, K, local_assign);

        // B. Redução Global do SSE 
        MPI_Allreduce(&my_sse, &global_sse, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

        // Verifica convergência
        double rel = fabs(global_sse - prev_sse) / (prev_sse > 0.0 ? prev_sse : 1.0);
        if(rel < eps) {
            // Convergiu. Incrementa it para contar esta execução e sai.
            it++; 
            break;
        }
        prev_sse = global_sse;

        // C. Update Step: Calcular somas locais
        local_sums_counts(local_X, my_N, local_assign, K, local_sum, local_cnt);

        // D. Redução Global de Somas e Contagens [cite: 70]
        // Precisamos somar os vetores de todos os processos
        MPI_Allreduce(local_sum, global_sum, K, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(local_cnt, global_cnt, K, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

        // E. Atualizar Centróides (cada processo faz isso) [cite: 71]
        for(int c=0; c<K; c++) {
            if(global_cnt[c] > 0) {
                C[c] = global_sum[c] / (double)global_cnt[c];
            } else {
                // Estratégia simples (Naive): manter o antigo ou resetar.
                // No código original, copiava X[0]. Em MPI, X[0] global está no rank 0.
                // Para simplificar e evitar comunicação extra não descrita no PDF,
                // mantemos o C[c] inalterado se o cluster esvaziar.
            }
        }
    }

    double end_time = MPI_Wtime();

    // 4. Consolidação dos Resultados (Assign) no Rank 0
    // Opcional: só se o usuário pediu arquivo de saída
    int *full_assign = NULL;
    if(rank == 0 && outAssign) {
        full_assign = (int*)malloc(N_total * sizeof(int));
    }

    if(outAssign) {
        MPI_Gatherv(local_assign, my_N, MPI_INT,
                    full_assign, sendcounts, displs, MPI_INT,
                    0, MPI_COMM_WORLD);
    }

    // 5. Saída de Dados (Rank 0)
    if(rank == 0) {
        double ms = (end_time - start_time) * 1000.0;
        printf("K-means 1D (MPI) - P=%d\n", size);
        printf("N=%d K=%d max_iter=%d eps=%g\n", N_total, K, max_iter, eps);
        printf("Iterações: %d | SSE final: %.6f | Tempo: %.2f ms\n", it, global_sse, ms);

        if(outAssign) {
            write_assign_csv(outAssign, full_assign, N_total);
            free(full_assign);
        }
        if(outCentroid) {
            write_centroids_csv(outCentroid, C, K);
        }
        
        // Limpezas exclusivas do rank 0
        free(X_full);
        free(sendcounts);
        free(displs);
    }

    // Limpezas gerais
    free(local_X);
    free(local_assign);
    free(local_sum);
    free(local_cnt);
    free(global_sum);
    free(global_cnt);
    if(C) free(C); // rank 0 e outros alocaram (rank 0 apontou para leitura, mas ok liberar no final se for malloc)

    MPI_Finalize();
    return 0;
}