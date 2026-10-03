#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include "energy.h"

#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

/*
 * Versão OpenMP. As três variantes compartilham EXATAMENTE a mesma
 * expressão por ponto do kernel sequencial (mesma ordem de operações),
 * então o resultado é bit-a-bit idêntico ao heat_seq (sem -ffast-math).
 *
 * Variantes (--variant):
 *   1: uma região `parallel for` por iteração (mede overhead de fork/join)
 *   2: uma única região `parallel` fora do laço de tempo, `omp for` dentro
 *      (PADRÃO — versão otimizada; barreira implícita sincroniza)
 *   3: igual à 2, mas com collapse(2) nos laços i,j (granularidade fina)
 *
 * Escalonamento: schedule(runtime) -> controlado por OMP_SCHEDULE.
 * Afinidade: OMP_PROC_BIND / OMP_PLACES (variáveis de ambiente).
 */

/* Corpo do stencil para um plano i fixo. Mantido em uma função inline
 * para reaproveitar em todas as variantes sem duplicar a expressão. */
static inline void step_row_ij(const double *restrict A, double *restrict B,
                               int N, double r, int i, int j)
{
    const size_t plane = (size_t)N * N;
    const size_t base  = IDX(i, j, 0, N);
    #pragma omp simd
    for (int k = 1; k < N - 1; k++) {
        size_t c = base + (size_t)k;
        B[c] = A[c] + r * ( A[c - plane] + A[c + plane]
                          + A[c - N]     + A[c + N]
                          + A[c - 1]     + A[c + 1]
                          - 6.0 * A[c] );
    }
}

/* ------------------------------------------------------------------ */
/* First-touch: inicializa a grade com a MESMA divisão static do        */
/* cálculo, para que cada página fique no nó NUMA da thread que a usa.   */
/* Sobrescreve o conteúdo já posto por grid_init (sequencial), mantendo  */
/* os mesmos valores — o que muda é QUEM toca cada página primeiro.      */
/* ------------------------------------------------------------------ */
static void first_touch_dirichlet(double *A, double *B, int N)
{
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            for (int k = 0; k < N; k++) {
                double v = (i == 0) ? 100.0 : 0.0;
                /* interior e faces != i0 ficam 0; face i0 = 100 */
                size_t c = IDX(i, j, k, N);
                A[c] = v;
                B[c] = v;
            }
        }
    }
}

int main(int argc, char **argv)
{
    Config c;
    config_defaults(&c, "omp");
    int rc = config_parse(&c, argc, argv);
    if (rc == 2) return 0;
    if (rc != 0) return 1;
    if (c.csv_header) { print_csv_header(); return 0; }

    omp_set_num_threads(c.threads);

    double *A = grid_alloc(c.N);
    double *B = grid_alloc(c.N);

    if (c.first_touch && c.init == INIT_DIRICHLET) {
        first_touch_dirichlet(A, B, c.N);
    } else {
        /* sequencial: todas as páginas no nó 0 (ou init sine) */
        grid_init(A, B, c.N, c.init);
    }

    const int    N = c.N;
    const double r = HEAT_R;
    const int    iters = c.iters;

    EnergyMonitor mon;
    energy_monitor_init(&mon);

    EnergySample e0 = energy_read(&mon);
    double t0 = now();

    if (c.variant == 1) {
        /* fork/join por iteração */
        for (int it = 0; it < iters; it++) {
            #pragma omp parallel for schedule(runtime)
            for (int i = 1; i < N - 1; i++)
                for (int j = 1; j < N - 1; j++)
                    step_row_ij(A, B, N, r, i, j);
            double *tmp = A; A = B; B = tmp;
        }
    } else if (c.variant == 3) {
        /* região única + collapse(2) */
        #pragma omp parallel
        {
            for (int it = 0; it < iters; it++) {
                #pragma omp for schedule(runtime) collapse(2)
                for (int i = 1; i < N - 1; i++)
                    for (int j = 1; j < N - 1; j++)
                        step_row_ij(A, B, N, r, i, j);
                /* barreira implícita do omp for */
                #pragma omp single
                { double *tmp = A; A = B; B = tmp; }
                /* barreira implícita do single */
            }
        }
    } else {
        /* variante 2 (padrão): região única, omp for simples */
        #pragma omp parallel
        {
            for (int it = 0; it < iters; it++) {
                #pragma omp for schedule(runtime)
                for (int i = 1; i < N - 1; i++)
                    for (int j = 1; j < N - 1; j++)
                        step_row_ij(A, B, N, r, i, j);
                #pragma omp single
                { double *tmp = A; A = B; B = tmp; }
            }
        }
    }

    double t1 = now();
    EnergySample e1 = energy_read(&mon);

    EnergySample de = energy_delta(&mon, e0, e1);
    double checksum = grid_checksum(A, N);

    print_csv(&c, t1 - t0, de, checksum);

    if (c.validate && c.init == INIT_SINE) {
        double linf_c, l2_c, linf_d, l2_d;
        sine_error_continuous(A, N, iters, &linf_c, &l2_c);
        sine_error_discrete  (A, N, iters, &linf_d, &l2_d);
        fprintf(stderr,
            "# validacao sine omp (N=%d, iters=%d): continua Linf=%.6e | discreto Linf=%.6e\n",
            N, iters, linf_c, linf_d);
    }

    if (c.dump) grid_dump(c.dump, A, N);

    grid_free(A);
    grid_free(B);
    return 0;
}
