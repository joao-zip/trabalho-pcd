#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include "energy.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Versão SEQUENCIAL de referência (baseline T_seq e verificação de
 * correção para todas as demais versões).
 *
 * Kernel de 7 pontos (FTCS explícito). A ordem das operações por ponto
 * é EXATAMENTE esta em todas as versões; compilado sem -ffast-math, o
 * resultado paralelo deve ser bit-a-bit idêntico (erro = 0).
 */
static void step(const double *restrict A, double *restrict B, int N, double r)
{
    const size_t plane = (size_t)N * N; /* deslocamento em i */
    for (int i = 1; i < N - 1; i++) {
        for (int j = 1; j < N - 1; j++) {
            for (int k = 1; k < N - 1; k++) {
                size_t c = IDX(i, j, k, N);
                B[c] = A[c] + r * ( A[c - plane] + A[c + plane]
                                  + A[c - N]     + A[c + N]
                                  + A[c - 1]     + A[c + 1]
                                  - 6.0 * A[c] );
            }
        }
    }
}

int main(int argc, char **argv)
{
    Config c;
    config_defaults(&c, "seq");
    int rc = config_parse(&c, argc, argv);
    if (rc == 2) return 0;   /* --help */
    if (rc != 0) return 1;

    if (c.csv_header) { print_csv_header(); return 0; }

    double *A = grid_alloc(c.N);
    double *B = grid_alloc(c.N);
    grid_init(A, B, c.N, c.init);

    EnergyMonitor mon;
    energy_monitor_init(&mon);

    /* --- região medida: apenas o laço de iterações --- */
    EnergySample e0 = energy_read(&mon);
    double t0 = now();

    for (int it = 0; it < c.iters; it++) {
        step(A, B, c.N, HEAT_R);
        double *tmp = A; A = B; B = tmp; /* swap */
    }

    double t1 = now();
    EnergySample e1 = energy_read(&mon);
    /* -------------------------------------------------- */

    EnergySample de = energy_delta(&mon, e0, e1);
    double checksum = grid_checksum(A, c.N);

    print_csv(&c, t1 - t0, de, checksum);

    /* Validação analítica (só faz sentido com --init sine). */
    if (c.validate && c.init == INIT_SINE) {
        double linf_c, l2_c, linf_d, l2_d;
        sine_error_continuous(A, c.N, c.iters, &linf_c, &l2_c);
        sine_error_discrete  (A, c.N, c.iters, &linf_d, &l2_d);
        fprintf(stderr,
            "# validacao sine (N=%d, iters=%d, r=%.3f):\n"
            "#   vs solucao CONTINUA : Linf=%.6e  L2rel=%.6e  (esperado ~O(dx^2)+O(dt))\n"
            "#   vs autovalor DISCRETO: Linf=%.6e  L2rel=%.6e  (esperado ~eps de maquina)\n",
            c.N, c.iters, HEAT_R, linf_c, l2_c, linf_d, l2_d);
    }

    if (c.dump) grid_dump(c.dump, A, c.N);

    grid_free(A);
    grid_free(B);
    return 0;
}
