#!/usr/bin/env bash
# env_info.sh - registra o ambiente do servidor para reprodutibilidade.
# Gera arquivos em results/env/ e um resumo no stdout.
#
# Uso: scripts/env_info.sh
set -u
cd "$(dirname "$0")/.."

OUT=results/env
mkdir -p "$OUT"
STAMP="$(date +%Y%m%d_%H%M%S)"

log() { echo "==> $*"; }
save() {  # save <arquivo> <comando...>
    local f="$OUT/$1"; shift
    echo "# $* (em $(date -Is))" > "$f"
    "$@" >> "$f" 2>&1
    echo "  [$f]"
}

log "Coletando informacoes de ambiente ($STAMP)"

# --- CPU / topologia ---
save cpu_lscpu.txt        lscpu
save cpu_online.txt       cat /sys/devices/system/cpu/online
command -v numactl >/dev/null 2>&1 && save numa_numactl.txt numactl -H \
    || echo "  numactl ausente"
command -v lstopo-no-graphics >/dev/null 2>&1 && save topo_hwloc.txt lstopo-no-graphics \
    || true

# --- Memoria ---
save mem_free.txt         free -g
save mem_meminfo.txt      head -5 /proc/meminfo

# --- Cache (tamanhos por nivel) ---
{
  echo "# tamanhos de cache por CPU0"
  for idx in /sys/devices/system/cpu/cpu0/cache/index*; do
    [ -d "$idx" ] || continue
    lvl=$(cat "$idx/level" 2>/dev/null)
    typ=$(cat "$idx/type" 2>/dev/null)
    sz=$(cat "$idx/size" 2>/dev/null)
    shared=$(cat "$idx/shared_cpu_list" 2>/dev/null)
    echo "L$lvl $typ: $sz (compartilhada por CPUs: $shared)"
  done
} > "$OUT/cache.txt"; echo "  [$OUT/cache.txt]"

# --- Governor / frequencia (afeta muito energia) ---
{
  echo "# scaling_governor por CPU (afeta energia/tempo!)"
  for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -f "$g" ] || continue
    echo "$g = $(cat "$g")"
  done | sort -u
  echo
  echo "# turbo boost:"
  [ -f /sys/devices/system/cpu/intel_pstate/no_turbo ] && \
    echo "intel_pstate/no_turbo = $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) (1=turbo OFF)"
  [ -f /sys/devices/system/cpu/cpufreq/boost ] && \
    echo "cpufreq/boost = $(cat /sys/devices/system/cpu/cpufreq/boost) (1=boost ON)"
} > "$OUT/cpu_governor.txt"; echo "  [$OUT/cpu_governor.txt]"

# --- RAPL / powercap ---
{
  echo "# Disponibilidade do RAPL via /sys/class/powercap"
  if [ -d /sys/class/powercap ]; then
    for d in /sys/class/powercap/intel-rapl:*; do
      [ -e "$d" ] || continue
      name=$(cat "$d/name" 2>/dev/null || echo "?")
      if e=$(cat "$d/energy_uj" 2>/dev/null); then
        readable="LEGIVEL (energy_uj=$e)"
      else
        readable="NAO LEGIVEL (permissao?)"
      fi
      range=$(cat "$d/max_energy_range_uj" 2>/dev/null || echo "?")
      echo "$d  name=$name  $readable  max_range_uj=$range"
    done
  else
    echo "powercap AUSENTE (CPU sem RAPL ou kernel sem o driver)"
  fi
  echo
  echo "# Permissoes dos energy_uj:"
  ls -l /sys/class/powercap/intel-rapl:*/energy_uj 2>/dev/null || echo "  (nenhum)"
} > "$OUT/rapl.txt"; echo "  [$OUT/rapl.txt]"

# --- perf: eventos de energia (plano B) ---
command -v perf >/dev/null 2>&1 && \
    save perf_energy.txt sh -c "perf list 2>/dev/null | grep -i energy || echo 'sem eventos de energia no perf'" \
    || echo "  perf ausente"

# --- GPU (CUDA opcional) ---
if command -v nvidia-smi >/dev/null 2>&1; then
    save gpu_nvidia_smi.txt nvidia-smi
else
    echo "nvidia-smi ausente (sem GPU NVIDIA ou driver)" > "$OUT/gpu_nvidia_smi.txt"
    echo "  [sem GPU NVIDIA]"
fi

# --- Software / compilador / flags ---
save sw_uname.txt         uname -a
save sw_gcc.txt           gcc --version
command -v nvcc >/dev/null 2>&1 && save sw_nvcc.txt nvcc --version || true
{
  echo "# flags -march=native expandidas por este gcc:"
  gcc -O3 -march=native -E -v - </dev/null 2>&1 | grep -Eo -- '-m[a-z0-9=.-]+' | sort -u
} > "$OUT/sw_march_native.txt" 2>&1; echo "  [$OUT/sw_march_native.txt]"

# --- Carga do sistema no momento ---
save load_uptime.txt      uptime

echo
log "Resumo rapido:"
echo "  CPU:     $(lscpu | awk -F: '/Model name/{gsub(/^ +/,"",$2);print $2; exit}')"
echo "  Sockets: $(lscpu | awk -F: '/^Socket/{gsub(/ /,"",$2);print $2}')  | Cores/socket: $(lscpu | awk -F: '/Core\(s\) per socket/{gsub(/ /,"",$2);print $2}')  | Threads/core: $(lscpu | awk -F: '/Thread\(s\) per core/{gsub(/ /,"",$2);print $2}')"
echo "  CPUs online: $(nproc)"
L3=$(lscpu | awk -F: '/L3 cache/{gsub(/^ +/,"",$2);print $2; exit}')
echo "  L3 cache: ${L3:-?}"
echo "  Mem: $(free -g | awk '/Mem:/{print $2" GiB"}')"
echo "  RAPL: $(grep -c LEGIVEL "$OUT/rapl.txt" 2>/dev/null) dominio(s) legivel(is)"
echo
echo "Todos os arquivos em: $OUT/"
