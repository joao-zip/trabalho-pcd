#ifndef ENERGY_H
#define ENERGY_H

/*
 * Medição de energia via RAPL (Running Average Power Limit) exposto pelo
 * subsistema powercap do Linux em /sys/class/powercap/intel-rapl:*.
 *
 * Uso:
 *     EnergyMonitor m;
 *     energy_monitor_init(&m);          // descobre domínios uma vez
 *     EnergySample s0 = energy_read(&m);
 *     ... laço de cálculo ...
 *     EnergySample s1 = energy_read(&m);
 *     EnergySample d  = energy_delta(&m, s0, s1);  // trata overflow do contador
 *
 * Se o RAPL não estiver disponível (permissão, CPU sem suporte, etc.), os
 * campos de energia saem como NaN e o resto do programa segue normalmente
 * (plano B: perf/likwid, documentado no plano).
 */

#include <stdint.h>

#define ENERGY_MAX_DOMAINS 16

typedef struct {
    /* Energia acumulada em Joules. NaN quando indisponível. */
    double pkg_j;   /* soma de todos os domínios "package-N"      */
    double dram_j;  /* soma de todos os subdomínios "dram"        */
} EnergySample;

typedef struct {
    char     path[256];        /* .../energy_uj                      */
    double   max_range_j;      /* max_energy_range_uj em Joules      */
    int      is_dram;          /* 1 se o domínio é DRAM, 0 se package*/
} EnergyDomain;

typedef struct {
    EnergyDomain domains[ENERGY_MAX_DOMAINS];
    int          n_domains;
    int          available;    /* 1 se ao menos um domínio foi lido  */
} EnergyMonitor;

/* Descobre os domínios RAPL legíveis. Seguro chamar mesmo sem suporte. */
void         energy_monitor_init(EnergyMonitor *m);

/* Lê a energia acumulada atual (Joules). Campos = NaN se indisponível. */
EnergySample energy_read(const EnergyMonitor *m);

/* Diferença s1 - s0 tratando overflow do contador por domínio. */
EnergySample energy_delta(const EnergyMonitor *m, EnergySample s0, EnergySample s1);

#endif /* ENERGY_H */
