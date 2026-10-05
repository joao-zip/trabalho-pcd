# Difusão de Calor 3D: saturação de banda e eficiência energética

Trabalho da disciplina de Programação Concorrente e Distribuída (Unifesp).
Implementamos um stencil de Jacobi para a equação do calor 3D (esquema
explícito FTCS de 7 pontos) nas versões sequencial e OpenMP, com
instrumentação para medir tempo, banda de memória efetiva, energia e
comportamento de cache.

A questão que investigamos: a partir de quantas threads a saturação da banda
de memória faz com que adicionar mais paralelismo deixe de compensar em
energia. Para explicar a causa física dessa saturação, incluímos uma análise
de cache que varre o tamanho da grade em torno da L3 e localiza o ponto em que
os dados deixam de caber na cache.

## Modelo numérico

- Grade `N×N×N` de `double` em um vetor 1D contíguo, com índice
  `IDX(i,j,k) = ((i*N)+j)*N + k`.
- Método de Jacobi com dois buffers: lê de `A`, escreve em `B`, troca.
- `r = α·Δt/Δx² = 0.1` (condição de estabilidade do FTCS 3D: `r ≤ 1/6`).
- Contorno de Dirichlet: face `i=0` a 100 °C, demais faces a 0, interior a 0.
- As duas versões usam a mesma expressão por ponto e são compiladas sem
  `-ffast-math`, de modo que o resultado paralelo é idêntico ao sequencial
  bit a bit.

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

Flags: `-O3 -march=native -std=c11 -Wall -Wextra`, sem `-ffast-math`. Os
binários ficam em `bin/`.

## Uso

```bash
./bin/heat_omp -n 256 -i 100 -t 16 \
    --schedule static --first-touch 1 --variant 2 --bind close
```

Cada execução imprime uma linha em CSV. `--csv-header` mostra o cabeçalho e
`--help` lista as opções.

Variantes do OpenMP (`--variant`): 1 abre uma região paralela por iteração
(fork/join); 2 usa uma única região (padrão); 3 aplica `collapse(2)`. A
afinidade é controlada pelo ambiente (`OMP_PROC_BIND`, `OMP_PLACES=cores`).

## Verificação de correção

```bash
# validação analítica (condição inicial senoidal)
./bin/heat_seq -n 128 -i 50 --init sine --validate
#   vs solução contínua : erro da ordem de O(dx^2)
#   vs autovalor discreto: erro da ordem do epsilon de máquina

# paralelo deve ser igual ao sequencial
./bin/heat_seq -n 96 -i 30 --dump ref.bin
./bin/heat_omp -n 96 -i 30 -t 8 --variant 2 --dump out.bin
./bin/compare ref.bin out.bin      # Linf = 0

# verificação de condição de corrida (ThreadSanitizer)
scripts/run_tsan.sh 64 8 4
```

Sobre o ThreadSanitizer: o GCC não instrumenta o `libgomp`, o que gera falsos
positivos no OpenMP, já que a sincronização das barreiras implícitas fica
invisível ao detector. Esses casos são suprimidos em `tools/tsan.supp`.
Confirmamos que o detector continua válido removendo a barreira e observando
o TSan apontar a corrida real na atualização do stencil.

## Medição de energia

A máquina de testes (AMD Threadripper PRO 5965WX) tem `perf_event_paranoid=4`
e os contadores RAPL em modo `0400` (leitura só por root). Como é uma máquina
compartilhada, evitamos alterar o estado global: a energia é medida por
envelope, com `sudo perf stat -a -e power/energy-pkg/` em torno de cada
execução do subconjunto principal, subtraindo uma linha de base (`-i 0`).
Assim `perf_event_paranoid`, as permissões do RAPL e o governor permanecem
inalterados.

Para medir energia, valide a sessão sudo antes de rodar:

```bash
sudo -v
scripts/run_experiments.sh
```

Sem isso, use `--no-energy`: o estudo de tempo, banda e speedup roda do mesmo
jeito e a energia sai como `NaN`.

Observações sobre o hardware (registradas em `results/env/`):

- O RAPL dessa CPU AMD expõe apenas o domínio *package* (não há `dram`), então
  a energia reportada é a do pacote.
- O governor `schedutil` e o turbo estão ativos. Não os alteramos por ser
  máquina compartilhada; para reduzir o efeito da variação de frequência,
  usamos repetições com mediana e registramos a frequência efetiva
  (`freq_mhz`) em cada linha do CSV.
- A L3 tem 4 fatias de 32 MB (128 MB no total), uma por CCX de 6 cores. Isso
  orientou a escolha dos tamanhos de grade.

O `scripts/setup_env.sh` (com `--restore`) serve apenas para o caso de uso
exclusivo da máquina, em que se pode fixar o governor e liberar o RAPL. Ele
não é necessário no fluxo por envelope descrito acima.

## Reproduzindo os experimentos

```bash
# 1. ambiente
scripts/env_info.sh                 # -> results/env/

# 2. teste rápido da pipeline
make all
scripts/run_experiments.sh --pilot --no-energy
scripts/run_stream.sh --pilot
scripts/run_cache.sh --pilot

# 3. bateria completa (rodar com a máquina ociosa)
sudo -v
scripts/run_experiments.sh          # -> results/raw/heat_<stamp>.csv
scripts/run_stream.sh               # -> results/raw/stream_<stamp>.csv
scripts/run_cache.sh                # -> results/raw/cache_<stamp>.csv

# 4. análise e gráficos
python3 analysis/analyze.py         # -> results/analysis/
```

Há também um modo `--fast` em `run_experiments.sh`, que reduz a matriz às
dimensões que mais importam para a questão central (varredura de threads por
tamanho de grade, com energia), útil quando não há tempo para a matriz
completa.

Tamanhos de grade usados (em relação à L3 de 128 MB):

- N=96 (cerca de 14 MB): cabe em uma fatia de L3; regime limitado por computação.
- N=256 (cerca de 256 MB): excede a L3 total; regime limitado por memória.
- N=1000 (cerca de 16 GB): muito maior que a cache.

## Análise de cache

O script `run_cache.sh` varre o tamanho da grade em torno da L3 e mede eventos
de cache com `perf stat` (mesmo esquema de envelope da energia):

```bash
sudo -v
scripts/run_cache.sh                # -> results/raw/cache_<stamp>.csv
python3 analysis/analyze.py         # gera fig_cache_*.png e cache_summary.csv
```

Essa CPU (AMD Zen 3) não expõe contadores de L3 pelo `perf` (os eventos `LLC-*`
aparecem como `<not supported>`), então medimos L1 e L2 (`L1-dcache-*` e
`l2_cache_req_stat.ic_dc_{hit,miss}_in_l2`) e usamos o MLUPS como indicador
indireto do comportamento da L3.

Com uma thread, o ponto de virada aparece no `fig_cache_mlups.png`: o MLUPS cai
ao cruzar os 32 MB (tamanho da fatia de L3 de um CCX), de cerca de 2370 MLUPS
em N=96 (13 MB) para cerca de 1460 em N=160 (62 MB). A taxa de miss de L2 se
mantém estável (em torno de 5%) nessa faixa, o que indica que a desaceleração
vem da L3 (os dados deixam de caber na fatia e passam a vir da DRAM), e não da
L2. A cadeia que o estudo descreve é: a virada de cache em 32 MB leva à
saturação de banda (a banda efetiva encosta no pico do STREAM), que por sua vez
leva à ineficiência energética (a curva de energia em função das threads tem
forma de "U").

Limitação de instrumentação: sem contadores de L3 nessa máquina, a evidência
sobre a L3 é indireta (MLUPS mais a miss de L2). Os nomes dos eventos variam
entre CPUs; confirme com `sudo perf list | grep -iE 'l2|l3|cache'` e ajuste a
variável `EVENTS` em `scripts/run_cache.sh` conforme necessário.

## Métricas (calculadas em `analyze.py`)

| Métrica | Fórmula |
|---|---|
| MLUPS | `(N-2)³·iters / tempo / 1e6` |
| Banda efetiva (GB/s) | `MLUPS·1e6·16 / 1e9` (16 B por ponto) |
| % do pico | banda efetiva / STREAM Triad (mesmos threads e bind) |
| Potência (W) | energia / tempo |
| EDP | energia · tempo |
| MLUPS/W | MLUPS / potência |
| Speedup | `T_seq/T_p` e `T_1/T_p` |
| Eficiência | speedup / p |

Sobre o "% do pico" acima de 100%: para N que cabe na cache (como N=96), o
stencil roda a partir da L3, com banda bem acima da DRAM medida pelo STREAM.
Isso é esperado e indica que o regime não é limitado por DRAM, ao contrário do
caso de N grande, em que a banda efetiva encosta no pico do STREAM e o
paralelismo adicional deixa de compensar.

## Formato do CSV

```
version,variant,N,iters,threads,schedule,bind,first_touch,rep,
time_s,mlups,gbs,energy_pkg_j,energy_dram_j,checksum,freq_mhz,energy_source
```

A coluna `energy_dram_j` é sempre `NaN` nessa CPU AMD, que não tem o domínio
DRAM no RAPL.

## Dependências

- GCC com suporte a OpenMP (usamos a versão 11).
- Python 3 com `pandas`, `numpy` e `matplotlib` para a análise.
- `perf` (pacote `linux-tools`) para medir energia e eventos de cache.
- O STREAM (McCalpin) está em `tools/stream/stream.c` e é usado como
  referência de banda de pico; os créditos são do autor original.
