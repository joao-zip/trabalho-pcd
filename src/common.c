#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700   /* expõe M_PI de <math.h> */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>

/* ================================================================== */
/* Configuração / CLI                                                 */
/* ================================================================== */

void config_defaults(Config *c, const char *version)
{
    c->N           = 128;
    c->iters       = 30;
    c->threads     = 1;
    c->first_touch = 1;
    strcpy(c->schedule, "static");
    strcpy(c->bind, "false");
    c->variant     = 2;
    c->pin         = 0;
    c->rep         = 0;
    c->init        = INIT_DIRICHLET;
    c->dump        = NULL;
    c->version     = version ? version : "seq";
    c->validate    = 0;
    c->csv_header  = 0;
}

void config_usage(const char *prog)
{
    fprintf(stderr,
        "Uso: %s [opcoes]\n"
        "  -n, --size N            tamanho da grade (NxNxN)      [128]\n"
        "  -i, --iters K           numero de iteracoes           [30]\n"
        "  -t, --threads P         numero de threads             [1]\n"
        "      --schedule S        static|dynamic|guided         [static]\n"
        "      --bind B            rotulo de afinidade p/ CSV    [false]\n"
        "      --first-touch 0|1   init paralela (NUMA)          [1]\n"
        "      --variant V         variante de paralelizacao     [2]\n"
        "      --pin               fixar threads a nucleos (pth)\n"
        "      --rep R             indice da repeticao (CSV)     [0]\n"
        "      --init K            dirichlet|sine                [dirichlet]\n"
        "      --dump ARQ          salvar grade final em binario\n"
        "      --validate          imprimir erro analitico (sine)\n"
        "      --csv-header        imprimir cabecalho CSV e sair\n"
        "  -h, --help              esta ajuda\n",
        prog);
}

static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

int config_parse(Config *c, int argc, char **argv)
{
    for (int a = 1; a < argc; a++) {
        const char *arg = argv[a];
        #define NEED_VAL()                                                  \
            do {                                                            \
                if (a + 1 >= argc) {                                        \
                    fprintf(stderr, "erro: %s requer um valor\n", arg);     \
                    return 1;                                               \
                }                                                           \
            } while (0)

        if (streq(arg, "-n") || streq(arg, "--size")) {
            NEED_VAL(); c->N = atoi(argv[++a]);
        } else if (streq(arg, "-i") || streq(arg, "--iters")) {
            NEED_VAL(); c->iters = atoi(argv[++a]);
        } else if (streq(arg, "-t") || streq(arg, "--threads")) {
            NEED_VAL(); c->threads = atoi(argv[++a]);
        } else if (streq(arg, "--schedule")) {
            NEED_VAL(); strncpy(c->schedule, argv[++a], sizeof(c->schedule) - 1);
            c->schedule[sizeof(c->schedule) - 1] = '\0';
        } else if (streq(arg, "--bind")) {
            NEED_VAL(); strncpy(c->bind, argv[++a], sizeof(c->bind) - 1);
            c->bind[sizeof(c->bind) - 1] = '\0';
        } else if (streq(arg, "--first-touch")) {
            NEED_VAL(); c->first_touch = atoi(argv[++a]);
        } else if (streq(arg, "--variant")) {
            NEED_VAL(); c->variant = atoi(argv[++a]);
        } else if (streq(arg, "--pin")) {
            c->pin = 1;
        } else if (streq(arg, "--rep")) {
            NEED_VAL(); c->rep = atoi(argv[++a]);
        } else if (streq(arg, "--init")) {
            NEED_VAL();
            const char *v = argv[++a];
            if (streq(v, "dirichlet"))      c->init = INIT_DIRICHLET;
            else if (streq(v, "sine"))      c->init = INIT_SINE;
            else { fprintf(stderr, "erro: --init %s invalido\n", v); return 1; }
        } else if (streq(arg, "--dump")) {
            NEED_VAL(); c->dump = argv[++a];
        } else if (streq(arg, "--validate")) {
            c->validate = 1;
        } else if (streq(arg, "--csv-header")) {
            c->csv_header = 1;
        } else if (streq(arg, "-h") || streq(arg, "--help")) {
            config_usage(argv[0]);
            return 2; /* sinaliza "sair com sucesso" ao chamador */
        } else {
            fprintf(stderr, "erro: argumento desconhecido: %s\n", arg);
            config_usage(argv[0]);
            return 1;
        }
        #undef NEED_VAL
    }

    if (c->N < 3) {
        fprintf(stderr, "erro: N deve ser >= 3 (tem %d)\n", c->N);
        return 1;
    }
    if (c->iters < 0) {
        fprintf(stderr, "erro: iters deve ser >= 0\n");
        return 1;
    }
    if (c->threads < 1) c->threads = 1;
    return 0;
}

/* ================================================================== */
/* Grade                                                              */
/* ================================================================== */

double *grid_alloc(int N)
{
    size_t n = (size_t)N * (size_t)N * (size_t)N;
    size_t bytes = n * sizeof(double);
    /* aligned_alloc exige tamanho múltiplo do alinhamento. */
    size_t align = 64;
    if (bytes % align != 0) bytes += align - (bytes % align);
    double *g = aligned_alloc(align, bytes);
    if (!g) {
        fprintf(stderr, "erro: aligned_alloc(%zu bytes) falhou: %s\n",
                bytes, strerror(errno));
        exit(1);
    }
    return g;
}

void grid_free(double *g) { free(g); }

/*
 * Inicialização. IMPORTANTE: gravamos as faces (contorno) em A e em B,
 * porque o algoritmo de Jacobi só atualiza o interior; o swap A<->B a
 * cada iteração perderia o contorno se ele existisse só em A.
 *
 * A inicialização é sequencial aqui. As versões paralelas fazem seu
 * próprio first-touch quando first_touch==1 (mesma divisão do cálculo).
 */
void grid_init(double *A, double *B, int N, InitKind init)
{
    const size_t n = (size_t)N * N * N;

    if (init == INIT_DIRICHLET) {
        /* tudo 0 primeiro */
        for (size_t p = 0; p < n; p++) A[p] = 0.0;
        /* face i = 0 a 100 graus */
        for (int j = 0; j < N; j++)
            for (int k = 0; k < N; k++)
                A[IDX(0, j, k, N)] = 100.0;
    } else { /* INIT_SINE */
        const double dx = 1.0 / (N - 1);
        for (int i = 0; i < N; i++) {
            double sx = sin(M_PI * i * dx);
            for (int j = 0; j < N; j++) {
                double sy = sin(M_PI * j * dx);
                for (int k = 0; k < N; k++) {
                    double sz = sin(M_PI * k * dx);
                    A[IDX(i, j, k, N)] = sx * sy * sz;
                }
            }
        }
        /* contorno de INIT_SINE já é 0 (sin(0)=sin(pi)=0). */
    }

    /* Copia para B para que o contorno sobreviva ao swap. */
    memcpy(B, A, n * sizeof(double));
}

double grid_checksum(const double *A, int N)
{
    const size_t n = (size_t)N * N * N;
    /* Soma de Kahan para reduzir erro de arredondamento na verificação. */
    double sum = 0.0, comp = 0.0;
    for (size_t p = 0; p < n; p++) {
        double y = A[p] - comp;
        double t = sum + y;
        comp = (t - sum) - y;
        sum = t;
    }
    return sum;
}

void grid_dump(const char *path, const double *A, int N)
{
    const size_t n = (size_t)N * N * N;
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "erro: nao abriu %s p/ escrita: %s\n",
                path, strerror(errno));
        exit(1);
    }
    size_t w = fwrite(A, sizeof(double), n, f);
    if (w != n) {
        fprintf(stderr, "erro: escreveu %zu de %zu doubles em %s\n", w, n, path);
        fclose(f);
        exit(1);
    }
    fclose(f);
}

/* ================================================================== */
/* Validação analítica (INIT_SINE)                                    */
/* ================================================================== */

/*
 * Solução contínua da equação do calor para o modo fundamental:
 *   T(x,y,z,t) = exp(-3 pi^2 alpha t) * sin(pi x) sin(pi y) sin(pi z)
 *
 * Não temos alpha e dt separadamente, só r = alpha*dt/dx^2.
 *   alpha*dt = r*dx^2  ->  alpha*t = r*dx^2*iters/dt ... precisa de dt.
 * Trabalhamos em unidades de dt: t = iters*dt, alpha*t = r*dx^2*iters/dt*dt
 *   = r*dx^2*iters ... NÃO: alpha*t = (r*dx^2/dt)*(iters*dt) = r*dx^2*iters.
 * Logo o expoente contínuo é: -3*pi^2 * r * dx^2 * iters.
 */
void sine_error_continuous(const double *A, int N, int iters,
                           double *linf, double *l2)
{
    const double dx = 1.0 / (N - 1);
    const double decay = exp(-3.0 * M_PI * M_PI * HEAT_R * dx * dx * (double)iters);

    double maxerr = 0.0, num = 0.0, den = 0.0;
    for (int i = 0; i < N; i++) {
        double sx = sin(M_PI * i * dx);
        for (int j = 0; j < N; j++) {
            double sy = sin(M_PI * j * dx);
            for (int k = 0; k < N; k++) {
                double sz = sin(M_PI * k * dx);
                double exact = decay * sx * sy * sz;
                double got   = A[IDX(i, j, k, N)];
                double e = fabs(got - exact);
                if (e > maxerr) maxerr = e;
                num += e * e;
                den += exact * exact;
            }
        }
    }
    *linf = maxerr;
    *l2   = (den > 0.0) ? sqrt(num / den) : sqrt(num);
}

/*
 * Autovalor DISCRETO exato do esquema FTCS para o modo
 * sin(pi i dx) sin(pi j dx) sin(pi k dx):
 *
 *   B[p] = A[p] + r*(vizinhos - 6 A[p]).
 * Para esse modo, cada vizinho em x contribui cos(pi dx) do valor central,
 * então o fator por iteração é:
 *   g = 1 - 2r(3 - cos(pi dx)*3)  ... por simetria nas 3 direções:
 *   g = 1 + r*(2*cos(pi dx) - 2)*3 ? Vamos derivar direito abaixo no código.
 *
 * De fato, para o modo separável, o laplaciano discreto de 7 pontos dá
 * autovalor lambda = 2*(cos(pi dx) - 1) por direção (em unidades de 1/dx^2
 * já embutidas em r). Somando 3 direções e multiplicando por r:
 *   g = 1 + r * [ 6*cos(pi dx) - 6 ]  = 1 - 6r(1 - cos(pi dx)).
 * Após 'iters' passos: fator = g^iters. Isso deve casar o resultado
 * numérico a ~epsilon de máquina, confirmando o kernel bit-a-bit.
 */
void sine_error_discrete(const double *A, int N, int iters,
                         double *linf, double *l2)
{
    const double dx = 1.0 / (N - 1);
    const double g  = 1.0 - 6.0 * HEAT_R * (1.0 - cos(M_PI * dx));
    const double fac = pow(g, (double)iters);

    double maxerr = 0.0, num = 0.0, den = 0.0;
    for (int i = 0; i < N; i++) {
        double sx = sin(M_PI * i * dx);
        for (int j = 0; j < N; j++) {
            double sy = sin(M_PI * j * dx);
            for (int k = 0; k < N; k++) {
                double sz = sin(M_PI * k * dx);
                double exact = fac * sx * sy * sz;
                double got   = A[IDX(i, j, k, N)];
                double e = fabs(got - exact);
                if (e > maxerr) maxerr = e;
                num += e * e;
                den += exact * exact;
            }
        }
    }
    *linf = maxerr;
    *l2   = (den > 0.0) ? sqrt(num / den) : sqrt(num);
}

/* ================================================================== */
/* Tempo                                                              */
/* ================================================================== */

double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ================================================================== */
/* CSV                                                                */
/* ================================================================== */

void print_csv_header(void)
{
    printf("version,variant,N,iters,threads,schedule,bind,first_touch,rep,"
           "time_s,mlups,gbs,energy_pkg_j,energy_dram_j,checksum\n");
}

void print_csv(const Config *c, double time_s, EnergySample e, double checksum)
{
    /* MLUPS: pontos do interior atualizados por segundo (milhoes).   */
    double interior = (double)(c->N - 2) * (c->N - 2) * (c->N - 2);
    double mlups = (time_s > 0.0)
                 ? interior * (double)c->iters / time_s / 1e6
                 : 0.0;
    /* Banda efetiva (GB/s) com B=16 bytes/ponto (1 leitura + 1 escrita). */
    double gbs = mlups * 1e6 * 16.0 / 1e9;

    printf("%s,%d,%d,%d,%d,%s,%s,%d,%d,%.6f,%.3f,%.3f,%.3f,%.3f,%.10g\n",
           c->version, c->variant, c->N, c->iters, c->threads,
           c->schedule, c->bind, c->first_touch, c->rep,
           time_s, mlups, gbs, e.pkg_j, e.dram_j, checksum);
}
