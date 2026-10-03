#ifndef COMMON_H
#define COMMON_H

/*
 * Módulo comum a todas as versões (seq, pthreads, openmp, cuda).
 * Garante que inicialização, indexação, saída e medição sejam idênticas,
 * para que as comparações de desempenho/energia sejam justas.
 */

#include <stddef.h>
#include "energy.h"

/* ------------------------------------------------------------------ */
/* Constantes físicas/numéricas (iguais em todas as versões)          */
/* ------------------------------------------------------------------ */

/* r = alpha*dt/dx^2. Estabilidade do FTCS 3D: r <= 1/6. Usamos 0.1.  */
#define HEAT_R 0.1

/* Domínio físico [0,1]^3 -> dx = 1/(N-1). Usado na validação sine.   */

/* ------------------------------------------------------------------ */
/* Indexação: k é o índice mais interno (contíguo em memória)          */
/* ------------------------------------------------------------------ */
#define IDX(i, j, k, N) ((((size_t)(i) * (size_t)(N)) + (size_t)(j)) * (size_t)(N) + (size_t)(k))

/* ------------------------------------------------------------------ */
/* Condição inicial                                                    */
/* ------------------------------------------------------------------ */
typedef enum {
    INIT_DIRICHLET = 0, /* interior 0, face i=0 a 100, demais faces 0 */
    INIT_SINE      = 1  /* T=sin(pi x)sin(pi y)sin(pi z), contorno 0  */
} InitKind;

/* ------------------------------------------------------------------ */
/* Configuração de uma execução (preenchida pela CLI)                  */
/* ------------------------------------------------------------------ */
typedef struct {
    int         N;              /* tamanho da grade (N x N x N)        */
    int         iters;          /* número de iterações no tempo        */
    int         threads;        /* p (nº de threads pedido)            */
    int         first_touch;    /* 1 = init paralela (NUMA first-touch)*/
    char        schedule[16];   /* static | dynamic | guided (OpenMP)  */
    char        bind[16];       /* false | close | spread (rótulo)     */
    int         variant;        /* variação de paralelização           */
    int         pin;            /* 1 = fixar threads a núcleos (pth)   */
    int         rep;            /* índice da repetição (rótulo p/ CSV) */
    InitKind    init;           /* condição inicial                    */
    const char *dump;           /* arquivo p/ salvar grade final (ou 0)*/
    const char *version;        /* rótulo: "seq","pth","omp","cuda"    */
    int         validate;       /* 1 = imprimir erro analítico (sine)  */
    int         csv_header;     /* 1 = imprimir cabeçalho CSV e sair   */
} Config;

/* Valores padrão + parse dos argumentos. Retorna 0 em sucesso.        */
void config_defaults(Config *c, const char *version);
int  config_parse(Config *c, int argc, char **argv);
void config_usage(const char *prog);

/* ------------------------------------------------------------------ */
/* Grade                                                               */
/* ------------------------------------------------------------------ */

/* aligned_alloc(64, N^3 * sizeof(double)). Aborta se falhar.          */
double *grid_alloc(int N);
void    grid_free(double *g);

/* Inicializa A e B com a condição inicial escolhida.                  */
/* As faces são gravadas em AMBOS os buffers (o swap não perde contorno)*/
void    grid_init(double *A, double *B, int N, InitKind init);

/* Soma de todos os pontos (verificação rápida de correção).           */
double  grid_checksum(const double *A, int N);

/* Salva a grade em binário (N^3 doubles, little-endian nativo).       */
void    grid_dump(const char *path, const double *A, int N);

/* ------------------------------------------------------------------ */
/* Validação analítica (condição INIT_SINE)                            */
/* ------------------------------------------------------------------ */

/* Erros contra a solução CONTÍNUA T(t)=exp(-3 pi^2 alpha t) T(0).      */
/* alpha*t é derivado de r e iters: alpha*dt = r*dx^2, t = iters*dt.    */
/* Preenche *linf e *l2 (norma L2 relativa).                           */
void    sine_error_continuous(const double *A, int N, int iters,
                              double *linf, double *l2);

/* Erro contra o autovalor DISCRETO exato do esquema FTCS.             */
/* Deve cair a ~epsilon de máquina se o stencil estiver bit-correto.   */
void    sine_error_discrete(const double *A, int N, int iters,
                            double *linf, double *l2);

/* ------------------------------------------------------------------ */
/* Tempo                                                               */
/* ------------------------------------------------------------------ */
double  now(void); /* segundos, CLOCK_MONOTONIC */

/* ------------------------------------------------------------------ */
/* Saída CSV                                                           */
/* ------------------------------------------------------------------ */
void    print_csv_header(void);
void    print_csv(const Config *c, double time_s, EnergySample e, double checksum);

#endif /* COMMON_H */
