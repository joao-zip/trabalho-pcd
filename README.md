# Difusão de Calor 3D — Saturação de Banda × Eficiência Energética

Trabalho de Programação Concorrente e Distribuída. Stencil de Jacobi (equação
do calor 3D, FTCS explícito de 7 pontos) implementado em Sequencial e OpenMP,
instrumentado para medir tempo, banda efetiva, energia e comportamento de cache.

**Pergunta de pesquisa:** *a partir de quantas threads a saturação da largura
de banda de memória torna o paralelismo adicional energeticamente ineficiente?*

O estudo é complementado por uma **análise de cache** (varredura do tamanho da
grade em torno da L3) que identifica o *ponto de virada* onde os dados deixam
de caber na cache — a causa física da saturação de banda.

## Modelo numérico

- Grade `N×N×N` de `double`, vetor 1D contíguo, `IDX(i,j,k)=((i*N)+j)*N+k`.
- Jacobi com dois buffers (`A` lê, `B` escreve, `swap`).
- `r = α·Δt/Δx² = 0.1` (estável: `r ≤ 1/6`).
- Contorno de Dirichlet: face `i=0` a 100 °C, demais a 0; interior a 0.
- Todas as versões usam **a mesma expressão por ponto** e compilam **sem
  `-ffast-math`** → o resultado paralelo é **bit-a-bit idêntico** ao sequencial.

## Estrutura

```
src/        common.{h,c}  energy.{h,c}  heat_seq.c  heat_omp.c
tools/      compare.c  stream/stream.c  tsan.supp
scripts/    env_info.sh  setup_env.sh  energy_perf.sh
            run_experiments.sh  run_stream.sh  run_cache.sh  run_tsan.sh
analysis/   analyze.py
results/    env/  raw/  analysis/
```

## Compilação

```bash
make all          # heat_seq + heat_omp + compare
```
Flags: `-O3 -march=native -std=c11 -Wall -Wextra` (sem `-ffast-math`).
Binários em `bin/`.

## Uso de um binário

```bash
./bin/heat_omp -n 256 -i 100 -t 16 \
    --schedule static --first-touch 1 --variant 2 --bind close
```
Imprime uma linha CSV. `--csv-header` mostra o cabeçalho. `--help` lista opções.

Variantes:
- **OpenMP** `--variant`: `1` região paralela por iteração (fork/join);
  `2` região única (padrão); `3` `collapse(2)`.

Afinidade OpenMP via ambiente: `OMP_PROC_BIND` + `OMP_PLACES=cores`.

## Correção

```bash
# validação analítica (condição senoidal)
./bin/heat_seq -n 128 -i 50 --init sine --validate
#   vs solução CONTÍNUA  : erro ~ O(dx^2)  (convergência física)
#   vs autovalor DISCRETO: erro ~ eps de máquina (kernel bit-correto)

# igualdade paralela = sequencial
./bin/heat_seq -n 96 -i 30 --dump ref.bin
./bin/heat_omp -n 96 -i 30 -t 8 --variant 2 --dump out.bin
./bin/compare ref.bin out.bin      # Linf = 0

# checagem de corrida (ThreadSanitizer)
scripts/run_tsan.sh 64 8 4
```

Nota sobre o ThreadSanitizer: o GCC não instrumenta o `libgomp`, gerando
falsos positivos no OpenMP cuja sincronização (barreiras implícitas) é
invisível ao detector. Suprimimos apenas esses via `tools/tsan.supp`. A
validade do detector foi confirmada removendo a barreira e observando o TSan
acusar a corrida real na atualização do stencil.

## Medição de energia — sem alterar o estado da máquina

Esta máquina (AMD Threadripper PRO 5965WX) tem `perf_event_paranoid=4` e os
contadores RAPL em modo `0400` (só root). Para **não alterar estado global**
(que afetaria outros usuários), a energia é medida por **envelope** com
`sudo perf stat -a -e power/energy-pkg/` ao redor de cada execução do
subconjunto principal, subtraindo uma linha de base (`-i 0`). Nada persiste:
`perf_event_paranoid`, permissões do RAPL e o governor ficam intactos.

Para habilitar a medição de energia, valide a sessão sudo antes:
```bash
sudo -v                       # (ou configure NOPASSWD para perf)
scripts/run_experiments.sh    # mede energia no subconjunto principal
```
Sem isso, use `--no-energy`: o estudo de tempo/banda/speedup roda igual e a
energia sai `NaN` (plano B do enunciado).

**Notas de hardware desta máquina** (registradas em `results/env/`):
- RAPL AMD expõe só o domínio *package* (sem `dram`) → energia é só do pacote.
- Governor `schedutil` e turbo ativos (não alteramos, por ser máquina
  compartilhada). Mitigamos com 15 repetições + **mediana** e registrando a
  frequência efetiva (`freq_mhz`) em cada linha do CSV.
- L3 = 4×32 MB (128 MB), 6 cores por CCX. Isso define os tamanhos de grade.

O `scripts/setup_env.sh` (aplicar/`--restore`) existe **apenas** para o caso de
uso exclusivo da máquina (fixa governor e libera RAPL). **Não é necessário** no
fluxo por envelope acima e não deve ser usado em máquina compartilhada.

## Reproduzir os experimentos

```bash
# 1. ambiente
scripts/env_info.sh                 # -> results/env/

# 2. piloto rápido (valida a pipeline)
make all
scripts/run_experiments.sh --pilot --no-energy
scripts/run_stream.sh --pilot
scripts/run_cache.sh --pilot

# 3. bateria completa (algumas horas; rodar com a máquina ociosa)
sudo -v
scripts/run_experiments.sh          # -> results/raw/heat_<stamp>.csv
scripts/run_stream.sh               # -> results/raw/stream_<stamp>.csv
scripts/run_cache.sh                # -> results/raw/cache_<stamp>.csv

# 4. análise + gráficos
python3 analysis/analyze.py         # -> results/analysis/
```

Tamanhos de grade (ajustados à L3 de 128 MB):
- **N=96** (~14 MB): cabe numa fatia de L3 → regime **compute-bound**.
- **N=256** (~256 MB): excede a L3 total → regime **memory-bound**.
- **N=1000** (~16 GB): muito maior que a cache.

## Análise de cache (`run_cache.sh`)

Para explicar a *causa física* da saturação de banda, `run_cache.sh` varre o
tamanho da grade N em torno da L3 e mede os *cache misses* por nível com
`perf stat` (mesmo envelope `sudo` da energia; não altera estado da máquina):

```bash
sudo -v
scripts/run_cache.sh                # -> results/raw/cache_<stamp>.csv
python3 analysis/analyze.py         # gera fig_cache_*.png e cache_summary.csv
```

A L3 desta máquina são 4 fatias de 32 MB (128 MB total), uma fatia por CCX de
6 cores. **Esta CPU (AMD Zen 3) não expõe contadores de L3** via `perf`
(`LLC-*` saem como `<not supported>`), então medimos **L1 e L2** com os
eventos `L1-dcache-*` e `l2_cache_req_stat.ic_dc_{hit,miss}_in_l2`, e usamos o
**MLUPS como proxy do comportamento de L3**.

Com 1 thread, o *ponto de virada* aparece claramente no `fig_cache_mlups.png`:
o MLUPS **despenca ao cruzar os 32 MB** (fatia de L3 de um CCX) — de ~2370
MLUPS em N=96 (13 MB) para ~1460 em N=160 (62 MB). A taxa de miss de L2
permanece estável (~5%) nessa faixa, confirmando que a desaceleração vem do
**L3** (dados que deixam de caber na fatia e caem para a DRAM), não do L2. A
cadeia causal do estudo fica: **cache (virada em 32 MB) → saturação de banda
(gruda no pico do STREAM) → ineficiência energética (curva de energia em “U”)**.

> **Limitação de instrumentação (documentada):** sem contadores de L3 nesta
> AMD, a evidência de L3 é indireta (MLUPS + miss de L2). Os nomes dos eventos
> variam entre CPUs; confirme com `sudo perf list | grep -iE 'l2|l3|cache'` e
> ajuste `EVENTS` em `scripts/run_cache.sh`.

## Métricas (calculadas em `analyze.py`)

| Métrica | Fórmula |
|---|---|
| MLUPS | `(N-2)³·iters / tempo / 1e6` |
| Banda efetiva (GB/s) | `MLUPS·1e6·16 / 1e9` (16 B/ponto) |
| % do pico | banda efetiva / STREAM Triad (mesmos threads+bind) |
| Potência (W) | energia / tempo |
| EDP | energia · tempo |
| MLUPS/W | MLUPS / potência |
| Speedup | `T_seq/T_p` e `T_1/T_p` |
| Eficiência | speedup / p |

**Interpretação do `% do pico > 100%`:** para N que cabe na cache (ex.: N=96),
o stencil roda da L3, com banda muito acima da DRAM medida pelo STREAM. Isso é
**esperado** e indica que o regime **não** é limitado por DRAM — o oposto do
caso memory-bound (N grande), onde a banda efetiva gruda no pico do STREAM e o
paralelismo adicional para de compensar.

## Saída CSV

```
version,variant,N,iters,threads,schedule,bind,first_touch,rep,
time_s,mlups,gbs,energy_pkg_j,energy_dram_j,checksum,freq_mhz,energy_source
```
`energy_dram_j` é sempre `NaN` nesta CPU AMD (sem domínio DRAM no RAPL).
