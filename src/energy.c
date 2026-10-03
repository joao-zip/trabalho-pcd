#define _POSIX_C_SOURCE 200809L
#include "energy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>

/*
 * Estrutura do sysfs powercap (Intel RAPL):
 *
 *   /sys/class/powercap/intel-rapl:0/            -> package 0
 *       name              = "package-0"
 *       energy_uj         = contador em microjoules
 *       max_energy_range_uj
 *       intel-rapl:0:0/   -> subdomínio (ex.: "dram", "core", "uncore")
 *           name          = "dram"
 *           energy_uj
 *           max_energy_range_uj
 *
 * Somamos todos os "package-*" em pkg_j e todos os "dram" em dram_j.
 * Se nada for legível, available=0 e as leituras retornam NaN.
 */

#define POWERCAP_ROOT "/sys/class/powercap"

static int read_uint64_file(const char *path, unsigned long long *out)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int rc = fscanf(f, "%llu", out);
    fclose(f);
    return (rc == 1) ? 0 : -1;
}

static int read_name_file(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, (int)n, f)) { fclose(f); return -1; }
    fclose(f);
    /* remove newline */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return 0;
}

/* Tenta registrar um domínio dado o diretório .../intel-rapl:X[:Y]. */
static void try_add_domain(EnergyMonitor *m, const char *dir)
{
    if (m->n_domains >= ENERGY_MAX_DOMAINS) return;

    char name_path[256], energy_path[256], range_path[256];
    snprintf(name_path,   sizeof(name_path),   "%s/name", dir);
    snprintf(energy_path, sizeof(energy_path), "%s/energy_uj", dir);
    snprintf(range_path,  sizeof(range_path),  "%s/max_energy_range_uj", dir);

    char name[64];
    if (read_name_file(name_path, name, sizeof(name)) != 0) return;

    /* precisa conseguir ler energy_uj (permissão!) */
    unsigned long long e;
    if (read_uint64_file(energy_path, &e) != 0) return;

    int is_dram = (strncmp(name, "dram", 4) == 0);
    int is_pkg  = (strncmp(name, "package", 7) == 0);
    if (!is_dram && !is_pkg) return; /* ignora core/uncore/psys */

    unsigned long long range = 0;
    double max_range_j = 0.0;
    if (read_uint64_file(range_path, &range) == 0)
        max_range_j = (double)range / 1e6;

    EnergyDomain *d = &m->domains[m->n_domains++];
    snprintf(d->path, sizeof(d->path), "%s", energy_path);
    d->max_range_j = max_range_j;
    d->is_dram     = is_dram;
    m->available   = 1;
}

void energy_monitor_init(EnergyMonitor *m)
{
    memset(m, 0, sizeof(*m));
    m->available = 0;

    DIR *root = opendir(POWERCAP_ROOT);
    if (!root) return; /* sem suporte: available fica 0 */

    struct dirent *ent;
    while ((ent = readdir(root)) != NULL) {
        /* domínios de topo têm a forma intel-rapl:N */
        if (strncmp(ent->d_name, "intel-rapl:", 11) != 0) continue;
        /* só os de topo (um ':' após o prefixo) para o pacote;
           subdomínios são varridos abaixo */
        char top[512];
        snprintf(top, sizeof(top), "%s/%s", POWERCAP_ROOT, ent->d_name);
        try_add_domain(m, top);

        /* varre subdomínios intel-rapl:N:M dentro deste diretório */
        DIR *sub = opendir(top);
        if (sub) {
            struct dirent *s;
            while ((s = readdir(sub)) != NULL) {
                if (strncmp(s->d_name, "intel-rapl:", 11) != 0) continue;
                char subdir[1024];
                snprintf(subdir, sizeof(subdir), "%s/%s", top, s->d_name);
                try_add_domain(m, subdir);
            }
            closedir(sub);
        }
    }
    closedir(root);
}

EnergySample energy_read(const EnergyMonitor *m)
{
    EnergySample s;
    if (!m->available) {
        s.pkg_j  = NAN;
        s.dram_j = NAN;
        return s;
    }

    double pkg = 0.0, dram = 0.0;
    int got_pkg = 0, got_dram = 0;

    for (int i = 0; i < m->n_domains; i++) {
        unsigned long long e;
        if (read_uint64_file(m->domains[i].path, &e) != 0) continue;
        double ej = (double)e / 1e6; /* uJ -> J */
        if (m->domains[i].is_dram) { dram += ej; got_dram = 1; }
        else                       { pkg  += ej; got_pkg  = 1; }
    }

    s.pkg_j  = got_pkg  ? pkg  : NAN;
    s.dram_j = got_dram ? dram : NAN;
    return s;
}

/*
 * Diferença tratando overflow. O contador energy_uj é monotônico até
 * atingir max_energy_range_uj, quando reinicia do zero. Como somamos
 * vários domínios num único escalar, o tratamento exato por domínio
 * exigiria guardar leituras por domínio. Na prática, os laços medidos
 * são curtos (poucos segundos) e o range é grande (dezenas a centenas
 * de kJ), então um único wrap no total é raro. Fazemos a correção
 * simples: se a diferença for negativa, somamos o maior range conhecido.
 */
EnergySample energy_delta(const EnergyMonitor *m, EnergySample s0, EnergySample s1)
{
    /* maior range entre domínios de cada tipo, para corrigir wrap */
    double max_pkg_range = 0.0, max_dram_range = 0.0;
    for (int i = 0; i < m->n_domains; i++) {
        if (m->domains[i].is_dram) {
            if (m->domains[i].max_range_j > max_dram_range)
                max_dram_range = m->domains[i].max_range_j;
        } else {
            if (m->domains[i].max_range_j > max_pkg_range)
                max_pkg_range = m->domains[i].max_range_j;
        }
    }

    EnergySample d;

    if (isnan(s0.pkg_j) || isnan(s1.pkg_j)) {
        d.pkg_j = NAN;
    } else {
        d.pkg_j = s1.pkg_j - s0.pkg_j;
        if (d.pkg_j < 0.0 && max_pkg_range > 0.0) d.pkg_j += max_pkg_range;
    }

    if (isnan(s0.dram_j) || isnan(s1.dram_j)) {
        d.dram_j = NAN;
    } else {
        d.dram_j = s1.dram_j - s0.dram_j;
        if (d.dram_j < 0.0 && max_dram_range > 0.0) d.dram_j += max_dram_range;
    }

    return d;
}
