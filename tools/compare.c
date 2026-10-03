#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * compare - erro máximo entre dois dumps binários da grade.
 *
 *   compare ref.bin out.bin [tol]
 *
 * Lê dois arquivos de doubles (mesmo tamanho), reporta erro absoluto
 * máximo (Linf) e L2 relativo. Sai com código != 0 se Linf > tol.
 * tol padrão = 1e-12.
 *
 * Como o kernel é idêntico entre versões e compilamos sem -ffast-math,
 * o esperado é Linf = 0 (bit-a-bit). A tolerância pequena cobre
 * eventuais diferenças de ordem de redução que não usamos, mas serve
 * de rede de segurança.
 */

static double *read_all(const char *path, size_t *n_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "erro: nao abriu %s\n", path); exit(2); }
    if (fseek(f, 0, SEEK_END) != 0) { fprintf(stderr, "erro: fseek %s\n", path); exit(2); }
    long bytes = ftell(f);
    if (bytes < 0) { fprintf(stderr, "erro: ftell %s\n", path); exit(2); }
    rewind(f);

    if ((size_t)bytes % sizeof(double) != 0) {
        fprintf(stderr, "erro: %s nao e multiplo de sizeof(double)\n", path);
        exit(2);
    }
    size_t n = (size_t)bytes / sizeof(double);
    double *buf = malloc(n * sizeof(double));
    if (!buf) { fprintf(stderr, "erro: malloc\n"); exit(2); }
    size_t r = fread(buf, sizeof(double), n, f);
    if (r != n) { fprintf(stderr, "erro: leitura curta em %s\n", path); exit(2); }
    fclose(f);
    *n_out = n;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "uso: %s ref.bin out.bin [tol=1e-12]\n", argv[0]);
        return 2;
    }
    double tol = (argc >= 4) ? atof(argv[3]) : 1e-12;

    size_t na, nb;
    double *a = read_all(argv[1], &na);
    double *b = read_all(argv[2], &nb);

    if (na != nb) {
        fprintf(stderr, "erro: tamanhos diferentes: %s tem %zu, %s tem %zu doubles\n",
                argv[1], na, argv[2], nb);
        return 2;
    }

    double linf = 0.0, num = 0.0, den = 0.0;
    size_t argmax = 0;
    for (size_t i = 0; i < na; i++) {
        double e = fabs(a[i] - b[i]);
        if (e > linf) { linf = e; argmax = i; }
        num += e * e;
        den += a[i] * a[i];
    }
    double l2rel = (den > 0.0) ? sqrt(num / den) : sqrt(num);

    printf("N_pontos=%zu  Linf=%.6e  L2rel=%.6e  (indice_max=%zu: %.17g vs %.17g)  tol=%.1e\n",
           na, linf, l2rel, argmax, a[argmax], b[argmax], tol);

    free(a);
    free(b);

    if (linf > tol) {
        printf("FALHOU: Linf %.6e > tol %.1e\n", linf, tol);
        return 1;
    }
    printf("OK: dentro da tolerancia\n");
    return 0;
}
