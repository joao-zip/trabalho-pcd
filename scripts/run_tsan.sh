#!/usr/bin/env bash
# Checagem de corrida (data race) com ThreadSanitizer numa grade pequena.
#
# O runtime libgomp do GCC nao e' instrumentado para o TSan, entao gera
# falsos positivos cuja sincronizacao (barreiras implicitas de omp for /
# omp single) e' invisivel ao detector. Usamos tools/tsan.supp para
# silenciar SOMENTE esses falsos positivos ligados ao runtime.
#
# A validade do detector foi confirmada em testes de desenvolvimento:
# ao remover a barreira/sincronizacao, o TSan acusa a corrida real no
# kernel (funcao de atualizacao do stencil). Logo, as supressoes nao
# escondem corridas do nosso codigo.
#
# ASLR: o TSan pode falhar com "unexpected memory mapping" sob ASLR alto;
#       por isso rodamos via `setarch -R` (ASLR desativado no processo).
#
# Uso: scripts/run_tsan.sh [N] [ITERS] [THREADS]
set -u
cd "$(dirname "$0")/.."

N=${1:-64}
IT=${2:-8}
T=${3:-4}
ARCH="$(uname -m)"
SUPP="$(pwd)/tools/tsan.supp"

make tsan >/dev/null 2>&1 || { echo "falha ao compilar alvo tsan"; exit 1; }

run() { setarch "$ARCH" -R "$@"; }

fail=0

echo "=== OpenMP (supressoes de libgomp; espera-se ZERO apos supressao) ==="
for v in 1 2 3; do
    export TSAN_OPTIONS="halt_on_error=0 suppressions=$SUPP"
    out=$(run env OMP_SCHEDULE=static ./bin/heat_omp_tsan \
              -n "$N" -i "$IT" -t "$T" --variant "$v" --first-touch 1 2>&1)
    n=$(printf '%s\n' "$out" | grep -c "SUMMARY: ThreadSanitizer")
    printf "  omp variant %s: %d avisos\n" "$v" "$n"
    [ "$n" -eq 0 ] || { fail=1; printf '%s\n' "$out"; }
done

if [ "$fail" -eq 0 ]; then
    echo "RESULTADO: LIMPO (nenhuma corrida detectada)"
else
    echo "RESULTADO: AVISOS ENCONTRADOS (ver acima)"
fi
exit "$fail"
