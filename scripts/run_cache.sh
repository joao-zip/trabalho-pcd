#!/usr/bin/env bash
# run_cache.sh - analise de cache: varre N em torno do tamanho da L3 e mede
# cache misses por nivel com `perf stat` (envelope), para localizar o
# "ponto de virada" onde o cubo deixa de caber na cache e a banda satura.
#
# Conecta a saturacao de banda (speedup que trava) com a CAUSA: o aumento
# da taxa de miss de L3 quando 2*N^3*8 bytes excede a cache.
#
# NAO altera estado permanente da maquina (perf como envelope sob sudo).
# Requer sessao sudo valida (sudo -v) por causa de perf_event_paranoid=4.
#
# Uso: scripts/run_cache.sh [--pilot]
#
# Saida: results/raw/cache_<stamp>.csv com colunas:
#   N,threads,bind,rep,mem_mb,time_s,mlups,
#   cache_refs,cache_misses,l1d_misses,llc_loads,llc_misses,miss_source
set -u
cd "$(dirname "$0")/.."

PILOT=0
[ "${1:-}" = "--pilot" ] && PILOT=1

BIN=bin/heat_omp
[ -x "$BIN" ] || { echo "faltando $BIN (rode: make omp)"; exit 1; }

# Eventos de cache. Esta CPU (AMD Zen 3, family 25) NAO expoe eventos de L3
# (LLC-* nao existem). Usamos L1 dados + L2 (que o perf lista como
# l2_cache_req_stat.*). O ponto de virada relevante para 1 thread e' quando
# os dados excedem o L2 (512 KB) e depois a fatia de L3 (32 MB) -> aqui
# medimos ate o L2; a fatia de L3 e' inferida pelo MLUPS e pela razao L2.
#
#   L1-dcache-loads / L1-dcache-load-misses : L1 dados
#   l2_cache_req_stat.ic_dc_hit_in_l2       : hits em L2 (instr+dados)
#   l2_cache_req_stat.ic_dc_miss_in_l2      : misses em L2 (instr+dados)
# taxa de miss de L2 = l2_miss / (l2_hit + l2_miss)
EVENTS="L1-dcache-loads,L1-dcache-load-misses,l2_cache_req_stat.ic_dc_hit_in_l2,l2_cache_req_stat.ic_dc_miss_in_l2"

# Varredura de N. L3: fatia 32MB (1 CCX) -> virada ~N=125;
# total 128MB -> ~N=200. Varremos em volta disso.
if [ "$PILOT" -eq 1 ]; then
    NS=(48 64 96 128 160)
    THREADS=(1)
    N_REPS=1
    ITERS=100
else
    NS=(48 64 80 96 112 128 144 160 192 224 256 320 400)
    THREADS=(1 8)
    N_REPS=5
    ITERS=200
fi
BIND=close

STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="results/raw/cache_${STAMP}.csv"
mkdir -p results/raw

echo "N,threads,bind,rep,mem_mb,time_s,mlups,l1d_loads,l1d_misses,l2_hits,l2_misses,miss_source" > "$OUT"

# perf disponivel sob sudo sem senha?
PERF_OK=1
if ! sudo -n LC_ALL=C perf stat -e cache-misses -a -- sleep 0.05 >/dev/null 2>&1; then
    PERF_OK=0
    echo "[aviso] perf nao disponivel (rode 'sudo -v' antes). Medindo so tempo/MLUPS; eventos = NaN."
fi

apply_bind() {
    export OMP_PROC_BIND=close; export OMP_PLACES=cores
}

# le um evento especifico do stderr do perf (formato -x,)
# $1 = texto do perf, $2 = nome do evento
get_ev() {
    printf '%s\n' "$1" | awk -F, -v ev="$2" '
        $0 ~ ev { v=$1; gsub(/[ \t]/,"",v); if(v=="" || v=="<notcounted>" || v=="<notsupported>") v="nan"; print v; found=1; exit }
        END { if(!found) print "nan" }'
}

for N in "${NS[@]}"; do
    mem_mb=$(LC_ALL=C awk -v n="$N" 'BEGIN{printf "%.1f", 2.0*n*n*n*8/1048576.0}')
    for t in "${THREADS[@]}"; do
        apply_bind
        export OMP_NUM_THREADS="$t"
        for r in $(seq 1 "$N_REPS"); do
            printf "\r  N=%s t=%s (%s MB) rep=%s     " "$N" "$t" "$mem_mb" "$r"
            args=(-n "$N" -i "$ITERS" -t "$t" --variant 2 --schedule static \
                  --bind "$BIND" --first-touch 1)

            # linha CSV do binario (para tempo e mlups)
            line=$("$BIN" "${args[@]}" 2>/dev/null)
            time_s=$(printf '%s' "$line" | cut -d, -f10)
            mlups=$(printf '%s' "$line" | cut -d, -f11)

            l1l=nan; l1m=nan; l2h=nan; l2m=nan; src=none
            if [ "$PERF_OK" -eq 1 ]; then
                perf_out=$(sudo -n LC_ALL=C perf stat -x, -e "$EVENTS" \
                              -- "$BIN" "${args[@]}" 2>&1 >/dev/null)
                l1l=$(get_ev "$perf_out" "L1-dcache-loads")
                l1m=$(get_ev "$perf_out" "L1-dcache-load-misses")
                l2h=$(get_ev "$perf_out" "ic_dc_hit_in_l2")
                l2m=$(get_ev "$perf_out" "ic_dc_miss_in_l2")
                src=perf
            fi

            echo "$N,$t,$BIND,$r,$mem_mb,$time_s,$mlups,$l1l,$l1m,$l2h,$l2m,$src" >> "$OUT"
        done
    done
done
echo
echo "==> Concluido. CSV: $OUT"
echo "L3: fatia 32 MB (1 CCX) -> virada esperada ~N=125; total 128 MB -> ~N=200"