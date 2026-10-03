#!/usr/bin/env bash
# setup_env.sh - prepara o ambiente para MEDICAO CONFIAVEL.
# RODE COM sudo, UMA VEZ, antes da bateria de experimentos:
#     sudo scripts/setup_env.sh
#
# O que faz (e por que):
#  1. Torna os contadores RAPL legiveis por usuario comum (energy_uj e' 0400
#     por padrao por causa do ataque PLATYPUS; para o trabalho, liberamos
#     leitura). Assim o heat_* le energia sem rodar como root.
#  2. Fixa o governor de frequencia em 'performance' (remove a variacao do
#     schedutil, que polui as medidas de tempo e energia).
#  3. (Opcional, comentado) desativa o turbo/boost para frequencia estavel.
#
# Para REVERTER, use: sudo scripts/setup_env.sh --restore
#
# Este script salva o estado anterior em results/env/prev_state.txt.

set -u
cd "$(dirname "$0")/.."
OUT=results/env
mkdir -p "$OUT"
STATE="$OUT/prev_state.txt"

if [ "$(id -u)" -ne 0 ]; then
    echo "erro: rode com sudo (precisa de root p/ RAPL e governor)"
    exit 1
fi

MODE="${1:-apply}"

restore() {
    echo "== Revertendo =="
    if [ -f "$STATE" ]; then
        # governor anterior
        gov=$(grep '^governor=' "$STATE" | cut -d= -f2)
        if [ -n "${gov:-}" ]; then
            for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
                echo "$gov" > "$g" 2>/dev/null
            done
            echo "governor restaurado para: $gov"
        fi
        # boost anterior
        boost=$(grep '^boost=' "$STATE" | cut -d= -f2)
        if [ -n "${boost:-}" ] && [ -f /sys/devices/system/cpu/cpufreq/boost ]; then
            echo "$boost" > /sys/devices/system/cpu/cpufreq/boost
            echo "boost restaurado para: $boost"
        fi
    fi
    # RAPL: volta a 0400 (padrao seguro)
    chmod 0400 /sys/class/powercap/intel-rapl:*/energy_uj 2>/dev/null
    echo "permissoes RAPL restauradas para 0400"
    echo "OK."
    exit 0
}

if [ "$MODE" = "--restore" ]; then
    restore
fi

echo "== Salvando estado anterior em $STATE =="
{
    echo "# estado anterior ($(date -Is))"
    echo "governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)"
    [ -f /sys/devices/system/cpu/cpufreq/boost ] && \
        echo "boost=$(cat /sys/devices/system/cpu/cpufreq/boost)"
} > "$STATE"
cat "$STATE"

echo
echo "== 1. Liberando leitura do RAPL (energy_uj -> 0444) =="
chmod 0444 /sys/class/powercap/intel-rapl:*/energy_uj 2>/dev/null
ls -l /sys/class/powercap/intel-rapl:*/energy_uj

echo
echo "== 2. Fixando governor em 'performance' =="
n=0
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance > "$g" 2>/dev/null && n=$((n+1))
done
echo "governor 'performance' aplicado a $n CPUs"
echo "governor atual (cpu0): $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"

echo
echo "== 3. Turbo/boost =="
if [ -f /sys/devices/system/cpu/cpufreq/boost ]; then
    echo "boost atual = $(cat /sys/devices/system/cpu/cpufreq/boost) (1=ON)"
    echo "  -> mantido ON. Para frequencia MAXIMAMENTE estavel (recomendado"
    echo "     para comparar energia entre thread counts), considere desligar:"
    echo "     echo 0 > /sys/devices/system/cpu/cpufreq/boost"
fi

echo
echo "Pronto. Agora o heat_* le RAPL sem sudo. Reverta depois com:"
echo "    sudo scripts/setup_env.sh --restore"
