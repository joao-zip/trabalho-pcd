#!/usr/bin/env bash
# energy_perf.sh - mede a energia de PACOTE (Joules) de um comando, usando
# `perf stat` como ENVELOPE. NAO altera estado permanente da maquina:
# perf_event_paranoid, permissoes do RAPL e governor ficam intactos.
#
# Requer sudo (perf_event_paranoid=4 nesta maquina). Rode a bateria dentro
# de uma sessao sudo com timestamp valido, ou configure NOPASSWD para perf.
#
# Uso:
#   source scripts/energy_perf.sh
#   perf_energy_available            # 0 se der p/ medir, 1 caso contrario
#   E=$(measure_energy_j -- ./bin/heat_omp -n 96 -i 30 -t 8)   # imprime Joules
#
# Observacoes de metodo:
#  - perf stat -a e' SYSTEM-WIDE: mede o pacote inteiro. Rode com a maquina
#    ociosa. O run_experiments.sh subtrai uma baseline (execucao -i 0).
#  - Locale: forcamos LC_ALL=C para o perf usar PONTO decimal, evitando
#    conflito com o separador de campo (virgula) do modo -x,.

PERF_EVENT="power/energy-pkg/"

perf_energy_available() {
    command -v perf >/dev/null 2>&1 || return 1
    # tenta uma medicao curta sob sudo, sem prompt (-n)
    if sudo -n LC_ALL=C perf stat -x, -a -e "$PERF_EVENT" -- sleep 0.05 \
         >/dev/null 2>&1; then
        return 0
    fi
    return 1
}

# measure_energy_j -- <comando...>
# Imprime a energia de pacote em Joules (ponto decimal). Vazio se falhar.
measure_energy_j() {
    # descarta o '--' inicial se presente
    [ "${1:-}" = "--" ] && shift
    local err
    err=$(sudo -n LC_ALL=C perf stat -x, -a -e "$PERF_EVENT" -- "$@" 2>&1 >/dev/null)
    # linha do evento: "<valor>,<unidade?>,Joules,power/energy-pkg/,..."
    # com -x, e LC_ALL=C o 1o campo e' o valor com PONTO decimal.
    printf '%s\n' "$err" \
        | awk -F, -v ev="$PERF_EVENT" '
            $0 ~ ev { gsub(/^[ \t]+|[ \t]+$/, "", $1); print $1; found=1; exit }
            END { if (!found) exit 1 }'
}
