#!/usr/bin/env bash
# run_stream.sh - mede a banda de pico de memoria (STREAM, McCalpin) com os
# MESMOS numeros de threads e afinidade dos experimentos, para calcular o
# "% do pico" da banda efetiva do stencil.
#
# Gera results/raw/stream_<stamp>.csv com colunas:
#   threads,bind,kernel,best_mbs,avg_mbs,min_mbs
#
# NAO altera estado permanente da maquina.
#
# Uso: scripts/run_stream.sh [--pilot]
set -u
cd "$(dirname "$0")/.."

PILOT=0
[ "${1:-}" = "--pilot" ] && PILOT=1

SRC=tools/stream/stream.c
[ -f "$SRC" ] || { echo "faltando $SRC (baixe o STREAM)"; exit 1; }

# Tamanho do array: precisa ser >= 4x a ultima cache.
# L3 total desta maquina = 128 MB. 80M doubles = ~640 MB por array (3 arrays
# ~1.9 GB) -> muito acima de 4x L3. No piloto usamos menor p/ rapidez.
if [ "$PILOT" -eq 1 ]; then
    ARRAY_SIZE=20000000      # ~160 MB/array (>4x L3? nao, mas suficiente p/ testar pipeline)
    NTIMES=10
    THREADS=(1 2 4 8)
else
    ARRAY_SIZE=80000000      # ~640 MB/array, 3 arrays ~1.9 GB
    NTIMES=20
    THREADS=(1 2 4 6 8 12 16 24 48)
fi
BINDS=(close spread)

STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="results/raw/stream_${STAMP}.csv"
mkdir -p results/raw bin

echo "==> Compilando STREAM (ARRAY_SIZE=$ARRAY_SIZE, NTIMES=$NTIMES)"
gcc -O3 -march=native -fopenmp \
    -DSTREAM_ARRAY_SIZE="$ARRAY_SIZE" -DNTIMES="$NTIMES" \
    "$SRC" -o bin/stream -lm || { echo "falha na compilacao"; exit 1; }

echo "threads,bind,kernel,best_mbs,avg_time_s,min_time_s,max_time_s" > "$OUT"

apply_bind() {
    case "$1" in
        close)  export OMP_PROC_BIND=close;  export OMP_PLACES=cores ;;
        spread) export OMP_PROC_BIND=spread; export OMP_PLACES=cores ;;
    esac
}

for t in "${THREADS[@]}"; do
    for b in "${BINDS[@]}"; do
        apply_bind "$b"
        export OMP_NUM_THREADS="$t"
        echo "  threads=$t bind=$b ..."
        # Saida do STREAM (linha por kernel):
        #   "Copy:   <best_MBs>  <avg_time>  <min_time>  <max_time>"
        # Best Rate MB/s = coluna 2; colunas 3-5 sao TEMPOS (s).
        bin/stream 2>/dev/null | awk -v t="$t" -v b="$b" '
            /^Copy:|^Scale:|^Add:|^Triad:/ {
                k=$1; sub(/:/,"",k);
                printf "%s,%s,%s,%.1f,%.6f,%.6f,%.6f\n", t, b, k, $2, $3, $4, $5
            }' >> "$OUT"
    done
done

echo "==> Concluido. CSV: $OUT"
echo "== Triad (pico de banda) por threads/bind =="
awk -F, 'NR==1 || $3=="Triad"' "$OUT" | column -t -s,
