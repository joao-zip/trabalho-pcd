#!/usr/bin/env bash
# run_experiments.sh - bateria de experimentos do estudo de saturacao de
# banda x eficiencia energetica. Gera um CSV bruto em results/raw/.
#
# NAO altera estado permanente da maquina. A energia (opcional) e' medida
# por ENVELOPE com `sudo perf stat` apenas num SUBCONJUNTO das execucoes
# (as que respondem a pergunta de pesquisa), conforme decidido.
#
# Uso:
#   scripts/run_experiments.sh [--pilot] [--no-energy]
#     --pilot     : matriz reduzida e 3 reps (teste rapido da pipeline)
#     --no-energy : nao tenta medir energia (nem chama sudo/perf)
#
# Protocolo (item 9 do enunciado):
#   1. registra o ambiente (env_info.sh)
#   2. aquecimento (1 execucao descartada) por configuracao
#   3. N_REPS repeticoes; a lista (config x rep) e' EMBARALHADA
#   4. tudo num CSV unico com timestamp
#   5. verifica a carga (uptime) antes de comecar

set -u
cd "$(dirname "$0")/.."
source scripts/energy_perf.sh

# --------- opcoes ---------
PILOT=0
FAST=0
WANT_ENERGY=1
for a in "$@"; do
    case "$a" in
        --pilot)     PILOT=1 ;;
        --fast)      FAST=1 ;;
        --no-energy) WANT_ENERGY=0 ;;
        *) echo "opcao desconhecida: $a"; exit 1 ;;
    esac
done

BIN_SEQ=bin/heat_seq
BIN_OMP=bin/heat_omp
for b in "$BIN_SEQ" "$BIN_OMP"; do
    [ -x "$b" ] || { echo "faltando $b (rode: make all omp)"; exit 1; }
done

STAMP="$(date +%Y%m%d_%H%M%S)"
RAW="results/raw/heat_${STAMP}.csv"
mkdir -p results/raw

# --------- parametros da matriz ---------
NPROC_PHYS=24
NPROC_LOG=48

if [ "$PILOT" -eq 1 ]; then
    THREADS=(1 2 4 8)
    NS_CACHE=(96)
    NS_MEM=(200)
    NS_HUGE=()               # sem 1000 no piloto (lento)
    ITERS_CACHE=200
    ITERS_MEM=40
    ITERS_HUGE=0
    N_REPS=3
    FIRST_TOUCH=(1)
    BINDS=(close)
    SCHEDULES=(static)
    OMP_VARIANTS=(2)
elif [ "$FAST" -eq 1 ]; then
    # Matriz ENXUTA: foca no eixo da pergunta de pesquisa (threads x N x
    # energia), podando dimensoes secundarias (bind/schedule/variant) que
    # o piloto mostrou nao mudarem a conclusao. Energia em TODAS as
    # execucoes. ~3*9*2*10 = 540 execucoes, roda em ~1h.
    THREADS=(1 2 4 6 8 12 16 24 48)
    NS_CACHE=(96)
    NS_MEM=(256)
    NS_HUGE=(1000)
    ITERS_CACHE=2000
    ITERS_MEM=100
    ITERS_HUGE=30
    N_REPS=10
    FIRST_TOUCH=(0 1)        # mantem efeito NUMA
    BINDS=(close)            # so close (melhor p/ memory-bound)
    SCHEDULES=(static)       # so static
    OMP_VARIANTS=(2)         # so a variante padrao
else
    THREADS=(1 2 4 6 8 12 16 24 48)
    NS_CACHE=(96)
    NS_MEM=(256)
    NS_HUGE=(1000)
    ITERS_CACHE=2000
    ITERS_MEM=100
    ITERS_HUGE=30
    N_REPS=15
    FIRST_TOUCH=(0 1)
    BINDS=(false close spread)
    SCHEDULES=(static dynamic)
    OMP_VARIANTS=(1 2 3)
fi

# Subconjunto PRINCIPAL onde medimos energia (pergunta de pesquisa):
# N grande e N de cache, first-touch on, bind close, schedule static,
# variante padrao. Todos os thread counts.
energy_subset() {   # $1=version $2=N $3=ft $4=bind $5=sched $6=variant
    [ "$WANT_ENERGY" -eq 1 ] || return 1
    # no modo fast a matriz ja e' pequena: mede energia em TODA execucao omp
    if [ "$FAST" -eq 1 ]; then
        [ "$1" = "omp" ] && return 0 || return 1
    fi
    [ "$3" = "1" ] || return 1
    [ "$4" = "close" ] || return 1
    [ "$5" = "static" ] || return 1
    if [ "$1" = "omp" ]; then [ "$6" = "2" ] || return 1; fi
    return 0
}

# --------- checagens iniciais ---------
echo "==> Ambiente"
scripts/env_info.sh >/dev/null 2>&1 || echo "  (env_info.sh falhou, seguindo)"
echo "==> Carga atual (deve estar baixa):"
uptime | sed 's/^/    /'

ENERGY_OK=1
if [ "$WANT_ENERGY" -eq 1 ]; then
    if perf_energy_available; then
        echo "==> Energia: perf disponivel (sudo -n). Medindo no subconjunto principal."
    else
        ENERGY_OK=0
        echo "==> Energia: perf NAO disponivel sem senha. Rode antes:"
        echo "      sudo -v         # valida a sessao sudo"
        echo "    ou use --no-energy. Seguindo SEM energia."
    fi
fi

# --------- afinidade OpenMP ---------
apply_omp_bind() {   # $1=bind
    case "$1" in
        false)  export OMP_PROC_BIND=false; unset OMP_PLACES ;;
        close)  export OMP_PROC_BIND=close; export OMP_PLACES=cores ;;
        spread) export OMP_PROC_BIND=spread; export OMP_PLACES=cores ;;
    esac
}

# frequencia media aproximada (contexto; nao requer perf)
freq_mhz() {
    awk '/cpu MHz/{s+=$4;n++} END{if(n)printf "%.0f", s/n; else print "NA"}' /proc/cpuinfo
}

# --------- monta a lista de tarefas ---------
# Cada tarefa: "version|variant|N|iters|threads|schedule|bind|ft"
TASKS=()

add_tasks_for_N() {   # $1=N $2=iters
    local N="$1" iters="$2" t ft bind sch v
    for t in "${THREADS[@]}"; do
        # sequencial so faz sentido com 1 thread
        if [ "$t" -eq 1 ]; then
            for ft in "${FIRST_TOUCH[@]}"; do
                TASKS+=("seq|2|$N|$iters|1|static|false|$ft")
            done
        fi
        for ft in "${FIRST_TOUCH[@]}"; do
            for bind in "${BINDS[@]}"; do
                for sch in "${SCHEDULES[@]}"; do
                    for v in "${OMP_VARIANTS[@]}"; do
                        TASKS+=("omp|$v|$N|$iters|$t|$sch|$bind|$ft")
                    done
                done
            done
        done
    done
}

for N in "${NS_CACHE[@]}"; do add_tasks_for_N "$N" "$ITERS_CACHE"; done
for N in "${NS_MEM[@]}";   do add_tasks_for_N "$N" "$ITERS_MEM";   done
for N in "${NS_HUGE[@]}";  do add_tasks_for_N "$N" "$ITERS_HUGE";  done

echo "==> ${#TASKS[@]} configuracoes x $N_REPS reps = $(( ${#TASKS[@]} * N_REPS )) execucoes"

# --------- expande com repeticoes e EMBARALHA ---------
JOBS=()
for task in "${TASKS[@]}"; do
    for r in $(seq 1 "$N_REPS"); do
        JOBS+=("$task|$r")
    done
done
# embaralha
mapfile -t JOBS < <(printf '%s\n' "${JOBS[@]}" | shuf)

# --------- cabecalho CSV (+ colunas de contexto) ---------
{
    "$BIN_SEQ" --csv-header | tr -d '\n'
    echo ",freq_mhz,energy_source"
} > "$RAW"

# --------- executa ---------
run_one() {   # recebe a task; imprime a linha CSV completa
    local job="$1"
    IFS='|' read -r ver variant N iters threads sched bind ft rep <<< "$job"

    local bin extra=()
    case "$ver" in
        seq) bin="$BIN_SEQ" ;;
        omp) bin="$BIN_OMP"; apply_omp_bind "$bind"; export OMP_SCHEDULE="$sched";
             [ "$sched" = "dynamic" ] && export OMP_SCHEDULE="dynamic,1"
             export OMP_NUM_THREADS="$threads" ;;
    esac

    local args=(-n "$N" -i "$iters" -t "$threads" --variant "$variant"
                --schedule "$sched" --bind "$bind" --first-touch "$ft" --rep "$rep")
    args+=("${extra[@]}")

    local line eng="" src="none"
    line=$("$bin" "${args[@]}" 2>/dev/null)

    # energia (subconjunto principal)
    if [ "$ENERGY_OK" -eq 1 ] && energy_subset "$ver" "$N" "$ft" "$bind" "$sched" "$variant"; then
        # baseline: mesma config com -i 0 (init + alocacao, sem iteracoes)
        local e_full e_base
        e_full=$(measure_energy_j -- "$bin" "${args[@]}")
        e_base=$(measure_energy_j -- "$bin" -n "$N" -i 0 -t "$threads" \
                    --variant "$variant" --schedule "$sched" --bind "$bind" \
                    --first-touch "$ft" "${extra[@]}")
        if [ -n "$e_full" ] && [ -n "$e_base" ]; then
            eng=$(awk -v a="$e_full" -v b="$e_base" 'BEGIN{d=a-b; if(d<0)d=0; printf "%.3f", d}')
            src="perf-pkg"
        fi
    fi

    # injeta a energia na coluna energy_pkg_j (13a) se medida
    if [ -n "$eng" ]; then
        line=$(printf '%s\n' "$line" | awk -F, -v e="$eng" 'BEGIN{OFS=","} {$13=e; print}')
    fi

    printf '%s,%s,%s\n' "$line" "$(freq_mhz)" "$src"
}

i=0
total=${#JOBS[@]}

# --------- aquecimento: 1 execucao por configuracao unica (descartada) ---
echo "==> Aquecimento (${#TASKS[@]} configuracoes, descartado)"
w=0
for task in "${TASKS[@]}"; do
    w=$((w+1))
    printf "\r  warmup [%d/%d]        " "$w" "${#TASKS[@]}"
    IFS='|' read -r ver variant N iters threads sched bind ft <<< "$task"
    case "$ver" in
        omp) apply_omp_bind "$bind"; export OMP_NUM_THREADS="$threads"
             export OMP_SCHEDULE="$sched"; [ "$sched" = "dynamic" ] && export OMP_SCHEDULE="dynamic,1"
             bin="$BIN_OMP"; extra=() ;;
        seq) bin="$BIN_SEQ"; extra=() ;;
    esac
    "$bin" -n "$N" -i "$iters" -t "$threads" --variant "$variant" \
        --schedule "$sched" --bind "$bind" --first-touch "$ft" "${extra[@]}" \
        >/dev/null 2>&1
done
echo

# --------- execucao principal (embaralhada, medida) ---------
echo "==> Execucao principal"
for job in "${JOBS[@]}"; do
    i=$((i+1))
    printf "\r  [%d/%d] %s        " "$i" "$total" "${job//|/ }"
    run_one "$job" >> "$RAW"
done
echo
echo "==> Concluido. CSV: $RAW  ($(( $(wc -l < "$RAW") - 1 )) linhas de dados)"
