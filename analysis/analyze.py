#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyze.py - consolida os CSVs brutos, calcula metricas derivadas e gera
os graficos do estudo de saturacao de banda x eficiencia energetica.

Uso:
    python3 analysis/analyze.py [--raw results/raw] [--out results/analysis]

Entradas (em --raw):
    heat_*.csv   : execucoes do stencil (a mais recente e' usada, ou todas)
    stream_*.csv : banda de pico (STREAM Triad), opcional

Saidas (em --out):
    summary.csv          : mediana/desvio por configuracao + metricas
    knee.csv             : threads de menor tempo/energia/EDP por (versao,N)
    fig_*.png            : graficos

Metricas (por configuracao, agregando repeticoes):
    time_s   -> mediana e desvio
    mlups    -> mediana
    gbs      -> banda efetiva (16 B/ponto)
    speedup  -> T_seq(N) / T_p   e   T_1(N)/T_p
    efic     -> speedup / p
    power_w  -> energia / tempo
    edp      -> energia * tempo
    mlups_w  -> mlups / power_w
    pct_peak -> gbs / STREAM_Triad(threads, bind)
"""
import argparse
import glob
import os
import sys

import numpy as np
import pandas as pd

import matplotlib
matplotlib.use("Agg")  # sem display
import matplotlib.pyplot as plt


# ----------------------------------------------------------------------
# Carregamento
# ----------------------------------------------------------------------
def load_experiments(raw_dir, all_files=False):
    files = sorted(glob.glob(os.path.join(raw_dir, "heat_*.csv")))
    if not files:
        sys.exit(f"nenhum heat_*.csv em {raw_dir}")
    use = files if all_files else [files[-1]]
    print(f"[carregando] {len(use)} arquivo(s) de experimento:")
    for f in use:
        print(f"    {f}")
    df = pd.concat([pd.read_csv(f) for f in use], ignore_index=True)
    # tipos
    num = ["variant", "N", "iters", "threads", "first_touch", "rep",
           "time_s", "mlups", "gbs", "energy_pkg_j", "energy_dram_j",
           "checksum", "freq_mhz"]
    for c in num:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")
    return df


def load_stream(raw_dir):
    files = sorted(glob.glob(os.path.join(raw_dir, "stream_*.csv")))
    if not files:
        print("[aviso] nenhum stream_*.csv; '% do pico' ficara indisponivel")
        return None
    print(f"[carregando] STREAM: {files[-1]}")
    s = pd.read_csv(files[-1])
    triad = s[s["kernel"] == "Triad"].copy()
    # GB/s a partir de MB/s
    triad["triad_gbs"] = triad["best_mbs"] / 1000.0
    # pico por (threads,bind): melhor entre binds tambem disponivel
    return triad


# ----------------------------------------------------------------------
# Verificacao de correcao (checksums)
# ----------------------------------------------------------------------
def check_correctness(df):
    print("\n== Verificacao de correcao (checksum unico por N) ==")
    ok = True
    for N, g in df.groupby("N"):
        u = g["checksum"].round(3).nunique()
        status = "OK" if u == 1 else f"FALHA ({u} valores!)"
        if u != 1:
            ok = False
        print(f"  N={N}: {status}  checksum={g['checksum'].iloc[0]:.4f}")
    return ok


# ----------------------------------------------------------------------
# Agregacao + metricas
# ----------------------------------------------------------------------
GROUP_KEYS = ["version", "variant", "N", "iters", "threads",
              "schedule", "bind", "first_touch"]


def aggregate(df, drop_first_rep=True):
    d = df.copy()
    if drop_first_rep and "rep" in d.columns:
        # descarta a menor rep de cada grupo como aquecimento extra
        # (o script ja faz warmup dedicado; isto e' uma salvaguarda)
        pass
    agg = d.groupby(GROUP_KEYS).agg(
        time_med=("time_s", "median"),
        time_std=("time_s", "std"),
        mlups_med=("mlups", "median"),
        gbs_med=("gbs", "median"),
        energy_med=("energy_pkg_j", "median"),
        freq_med=("freq_mhz", "median"),
        nreps=("time_s", "count"),
    ).reset_index()

    # potencia, EDP, mlups/W (so onde ha energia)
    agg["power_w"] = agg["energy_med"] / agg["time_med"]
    agg["edp"] = agg["energy_med"] * agg["time_med"]
    agg["mlups_w"] = np.where(agg["power_w"] > 0,
                              agg["mlups_med"] / agg["power_w"], np.nan)

    # speedup vs sequencial (mesmo N) e vs 1 thread (mesma versao/N)
    # T_seq(N): mediana da versao 'seq' com 1 thread, por N
    tseq = (agg[(agg["version"] == "seq")]
            .groupby("N")["time_med"].median().to_dict())
    # T_1(versao,N): menor thread disponivel (=1) da propria versao/config
    t1 = (agg[agg["threads"] == 1]
          .groupby(["version", "N", "variant", "schedule", "bind", "first_touch"])
          ["time_med"].median())

    def speedup_seq(row):
        base = tseq.get(row["N"])
        return base / row["time_med"] if base else np.nan

    def speedup_self(row):
        key = (row["version"], row["N"], row["variant"],
               row["schedule"], row["bind"], row["first_touch"])
        base = t1.get(key)
        return base / row["time_med"] if base else np.nan

    agg["speedup_seq"] = agg.apply(speedup_seq, axis=1)
    agg["speedup_self"] = agg.apply(speedup_self, axis=1)
    agg["efic_seq"] = agg["speedup_seq"] / agg["threads"]
    agg["efic_self"] = agg["speedup_self"] / agg["threads"]
    return agg


def add_pct_peak(agg, triad):
    if triad is None:
        agg["pct_peak"] = np.nan
        return agg
    # pico por (threads, bind); se bind='false', usa o melhor pico daquele t
    peak = {}
    best_by_t = triad.groupby("threads")["triad_gbs"].max().to_dict()
    for _, r in triad.iterrows():
        peak[(int(r["threads"]), r["bind"])] = r["triad_gbs"]

    def pct(row):
        p = peak.get((int(row["threads"]), row["bind"]))
        if p is None:
            p = best_by_t.get(int(row["threads"]))
        return 100.0 * row["gbs_med"] / p if p else np.nan

    agg["pct_peak"] = agg.apply(pct, axis=1)
    return agg


# ----------------------------------------------------------------------
# Selecao para graficos: config principal (omp v2, ft1, close, static)
# ----------------------------------------------------------------------
def main_config(agg, version="omp"):
    if version == "omp":
        m = ((agg["version"] == "omp") & (agg["variant"] == 2) &
             (agg["first_touch"] == 1) & (agg["bind"] == "close") &
             (agg["schedule"] == "static"))
    else:
        m = ((agg["version"] == "pth") & (agg["variant"] == 0) &
             (agg["first_touch"] == 1) & (agg["bind"] == "close"))
    return agg[m].sort_values(["N", "threads"])


# ----------------------------------------------------------------------
# Graficos
# ----------------------------------------------------------------------
def savefig(fig, out, name):
    path = os.path.join(out, name)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)
    print(f"  [fig] {path}")


def plot_time_power(sub, out):
    for N, g in sub.groupby("N"):
        if g.empty:
            continue
        fig, ax1 = plt.subplots(figsize=(7, 4.5))
        ax1.plot(g["threads"], g["time_med"], "o-", color="tab:blue",
                 label="tempo (s)")
        ax1.set_xlabel("threads")
        ax1.set_ylabel("tempo (s)", color="tab:blue")
        ax1.tick_params(axis="y", labelcolor="tab:blue")
        ax1.set_title(f"Tempo e potencia x threads (N={N})")
        if g["power_w"].notna().any():
            ax2 = ax1.twinx()
            ax2.plot(g["threads"], g["power_w"], "s--", color="tab:red",
                     label="potencia (W)")
            ax2.set_ylabel("potencia (W)", color="tab:red")
            ax2.tick_params(axis="y", labelcolor="tab:red")
        savefig(fig, out, f"fig_time_power_N{N}.png")


def plot_energy(sub, out):
    g = sub[sub["energy_med"].notna()]
    if g.empty:
        print("  [energia] sem dados de energia; pulando fig_energy")
        return
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for N, gg in g.groupby("N"):
        ax.plot(gg["threads"], gg["energy_med"], "o-", label=f"N={N}")
        # marca o minimo de energia
        idx = gg["energy_med"].idxmin()
        ax.scatter([gg.loc[idx, "threads"]], [gg.loc[idx, "energy_med"]],
                   s=140, facecolors="none", edgecolors="k", zorder=5)
    ax.set_xlabel("threads")
    ax.set_ylabel("energia pkg (J)")
    ax.set_title("Energia x threads (circulo = minimo)")
    ax.legend()
    savefig(fig, out, "fig_energy.png")


def plot_bandwidth(sub, triad, out):
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for N, g in sub.groupby("N"):
        ax.plot(g["threads"], g["gbs_med"], "o-", label=f"stencil N={N}")
    if triad is not None:
        for b, gg in triad.groupby("bind"):
            gg = gg.sort_values("threads")
            ax.plot(gg["threads"], gg["triad_gbs"], "--",
                    label=f"STREAM Triad ({b})")
    ax.set_xlabel("threads")
    ax.set_ylabel("banda (GB/s)")
    ax.set_title("Banda efetiva x threads (vs pico STREAM)")
    ax.legend()
    savefig(fig, out, "fig_bandwidth.png")


def plot_speedup_efic(sub, out):
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(11, 4.5))
    for N, g in sub.groupby("N"):
        a1.plot(g["threads"], g["speedup_seq"], "o-", label=f"N={N}")
        a2.plot(g["threads"], g["efic_seq"], "o-", label=f"N={N}")
    # linha ideal
    t = sorted(sub["threads"].unique())
    a1.plot(t, t, "k:", alpha=0.5, label="ideal")
    a1.set_xlabel("threads"); a1.set_ylabel("speedup (T_seq/T_p)")
    a1.set_title("Speedup"); a1.legend()
    a2.axhline(1.0, color="k", ls=":", alpha=0.5)
    a2.set_xlabel("threads"); a2.set_ylabel("eficiencia")
    a2.set_title("Eficiencia"); a2.legend()
    savefig(fig, out, "fig_speedup_efic.png")


def plot_edp_mlupsw(sub, out):
    g = sub[sub["energy_med"].notna()]
    if g.empty:
        print("  [edp] sem energia; pulando fig_edp_mlupsw")
        return
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(11, 4.5))
    for N, gg in g.groupby("N"):
        a1.plot(gg["threads"], gg["edp"], "o-", label=f"N={N}")
        a2.plot(gg["threads"], gg["mlups_w"], "o-", label=f"N={N}")
    a1.set_xlabel("threads"); a1.set_ylabel("EDP (J*s)")
    a1.set_title("Energy-Delay Product"); a1.legend()
    a2.set_xlabel("threads"); a2.set_ylabel("MLUPS/W")
    a2.set_title("Eficiencia energetica (MLUPS/W)"); a2.legend()
    savefig(fig, out, "fig_edp_mlupsw.png")


def plot_firsttouch_bind(agg, out):
    # barras: efeito de first-touch e bind para omp v2 static, um N grande
    Ns = sorted(agg["N"].unique())
    Nbig = Ns[-1]
    base = agg[(agg["version"] == "omp") & (agg["variant"] == 2) &
               (agg["schedule"] == "static") & (agg["N"] == Nbig)]
    if base.empty:
        return
    # escolhe um thread count intermediario disponivel
    ts = sorted(base["threads"].unique())
    tsel = ts[len(ts) // 2]
    b = base[base["threads"] == tsel]
    if b.empty:
        return
    fig, ax = plt.subplots(figsize=(7, 4.5))
    labels, vals = [], []
    for ft in sorted(b["first_touch"].unique()):
        for bind in ["false", "close", "spread"]:
            row = b[(b["first_touch"] == ft) & (b["bind"] == bind)]
            if not row.empty:
                labels.append(f"ft{ft}/{bind}")
                vals.append(row["time_med"].iloc[0])
    ax.bar(labels, vals, color="tab:purple")
    ax.set_ylabel("tempo (s)")
    ax.set_title(f"Efeito de first-touch e afinidade (N={Nbig}, t={tsel})")
    plt.setp(ax.get_xticklabels(), rotation=30, ha="right")
    savefig(fig, out, "fig_firsttouch_bind.png")


# ----------------------------------------------------------------------
# Tabela do "joelho"
# ----------------------------------------------------------------------
def knee_table(sub):
    rows = []
    for (ver, N), g in sub.groupby(["version", "N"]):
        r = {"version": ver, "N": N}
        r["t_min_time"] = g.loc[g["time_med"].idxmin(), "threads"]
        if g["energy_med"].notna().any():
            gg = g[g["energy_med"].notna()]
            r["t_min_energy"] = gg.loc[gg["energy_med"].idxmin(), "threads"]
            r["t_min_edp"] = gg.loc[gg["edp"].idxmin(), "threads"]
        else:
            r["t_min_energy"] = np.nan
            r["t_min_edp"] = np.nan
        rows.append(r)
    return pd.DataFrame(rows)


# ----------------------------------------------------------------------
# Analise de CACHE (varredura de N; cache_*.csv do run_cache.sh)
# ----------------------------------------------------------------------
# Tamanhos de cache desta maquina (AMD Threadripper PRO 5965WX):
L3_SLICE_MB = 32.0    # uma fatia de L3 por CCX (6 cores)
L3_TOTAL_MB = 128.0   # L3 total (4 fatias)
L2_MB = 0.5           # L2 por core (512 KB)


def load_cache(raw_dir):
    files = sorted(glob.glob(os.path.join(raw_dir, "cache_*.csv")))
    if not files:
        return None
    print(f"[carregando] cache: {files[-1]}")
    c = pd.read_csv(files[-1])
    for col in ["N", "threads", "rep", "mem_mb", "time_s", "mlups",
                "l1d_loads", "l1d_misses", "l2_hits", "l2_misses"]:
        if col in c.columns:
            c[col] = pd.to_numeric(c[col], errors="coerce")
    return c


def cache_summary(c):
    """Agrega por (N, threads): medianas e taxas de miss de L1 e L2."""
    g = c.groupby(["N", "threads"]).agg(
        mem_mb=("mem_mb", "first"),
        time_med=("time_s", "median"),
        mlups_med=("mlups", "median"),
        l1_loads=("l1d_loads", "median"),
        l1_misses=("l1d_misses", "median"),
        l2_hits=("l2_hits", "median"),
        l2_misses=("l2_misses", "median"),
    ).reset_index()
    # taxas (fracao). Onde nao ha contadores, fica NaN.
    g["l1_miss_rate"] = g["l1_misses"] / g["l1_loads"]
    g["l2_miss_rate"] = g["l2_misses"] / (g["l2_hits"] + g["l2_misses"])
    return g.sort_values(["threads", "N"])


def find_turning_point(gsub):
    """
    Localiza o 'ponto de virada' em N: onde MLUPS cai de forma mais abrupta
    (maior queda relativa entre N consecutivos). Retorna (N_virada, mem_mb).
    """
    gg = gsub.sort_values("N").reset_index(drop=True)
    if len(gg) < 2:
        return None
    best_drop = 0.0
    best_i = None
    for i in range(1, len(gg)):
        prev = gg.loc[i - 1, "mlups_med"]
        cur = gg.loc[i, "mlups_med"]
        if prev and prev > 0:
            drop = (prev - cur) / prev
            if drop > best_drop:
                best_drop = drop
                best_i = i
    if best_i is None:
        return None
    return (int(gg.loc[best_i, "N"]), float(gg.loc[best_i, "mem_mb"]),
            best_drop)


def plot_cache(csum, out):
    for metric, ylabel, fname, title in [
        ("mlups_med", "MLUPS", "fig_cache_mlups.png",
         "Desempenho x tamanho (ponto de virada da cache)"),
        ("l2_miss_rate", "taxa de miss de L2", "fig_cache_missrate.png",
         "Taxa de miss de L2 x tamanho"),
        ("l1_miss_rate", "taxa de miss de L1", "fig_cache_l1.png",
         "Taxa de miss de L1 x tamanho"),
    ]:
        if metric not in csum or csum[metric].notna().sum() == 0:
            if metric != "mlups_med":
                print(f"  [cache] sem dados p/ {metric}; pulando {fname}")
                continue
        fig, ax = plt.subplots(figsize=(7.5, 4.8))
        for t, g in csum.groupby("threads"):
            g = g.sort_values("mem_mb")
            ax.plot(g["mem_mb"], g[metric], "o-", label=f"{t} thread(s)")
        # linhas verticais nos tamanhos de cache (por core)
        ax.axvline(L2_MB, color="tab:green", ls=":", alpha=0.8,
                   label=f"L2/core ({L2_MB*1024:.0f} KB)")
        ax.axvline(L3_SLICE_MB, color="tab:orange", ls="--", alpha=0.8,
                   label=f"L3 fatia ({L3_SLICE_MB:.0f} MB)")
        ax.axvline(L3_TOTAL_MB, color="tab:red", ls="--", alpha=0.8,
                   label=f"L3 total ({L3_TOTAL_MB:.0f} MB)")
        ax.set_xscale("log", base=2)
        ax.set_xlabel("memoria dos 2 buffers (MB, escala log2)")
        ax.set_ylabel(ylabel)
        ax.set_title(title)
        ax.legend(fontsize=8)
        savefig(fig, out, fname)


def analyze_cache(raw_dir, out):
    c = load_cache(raw_dir)
    if c is None:
        print("[cache] nenhum cache_*.csv; pulando analise de cache")
        return
    csum = cache_summary(c)
    csum.to_csv(os.path.join(out, "cache_summary.csv"), index=False)
    print(f"[salvo] {os.path.join(out, 'cache_summary.csv')} "
          f"({len(csum)} pontos)")

    print("\n== Ponto de virada da cache (por threads) ==")
    for t, g in csum.groupby("threads"):
        tp = find_turning_point(g)
        if tp:
            N, mem, drop = tp
            print(f"  {t} thread(s): virada em N={N} "
                  f"(~{mem:.0f} MB, queda de {100*drop:.0f}% no MLUPS)")
    plot_cache(csum, out)


# ----------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default="results/raw")
    ap.add_argument("--out", default="results/analysis")
    ap.add_argument("--all", action="store_true",
                    help="usa todos os heat_*.csv (nao so o mais recente)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    df = load_experiments(args.raw, all_files=args.all)
    triad = load_stream(args.raw)

    check_correctness(df)

    agg = aggregate(df)
    agg = add_pct_peak(agg, triad)
    agg.to_csv(os.path.join(args.out, "summary.csv"), index=False)
    print(f"\n[salvo] {os.path.join(args.out, 'summary.csv')} "
          f"({len(agg)} configuracoes)")

    # config principal para os graficos (OpenMP v2)
    sub = main_config(agg, "omp")
    if sub.empty:
        print("[aviso] config principal OpenMP vazia; usando tudo de omp")
        sub = agg[agg["version"] == "omp"].sort_values(["N", "threads"])

    print("\n== Gerando graficos ==")
    plot_time_power(sub, args.out)
    plot_energy(sub, args.out)
    plot_bandwidth(sub, triad, args.out)
    plot_speedup_efic(sub, args.out)
    plot_edp_mlupsw(sub, args.out)
    plot_firsttouch_bind(agg, args.out)

    knee = knee_table(sub)
    knee.to_csv(os.path.join(args.out, "knee.csv"), index=False)
    print("\n== Tabela do 'joelho' (threads otimos) ==")
    print(knee.to_string(index=False))
    print(f"\n[salvo] {os.path.join(args.out, 'knee.csv')}")

    # analise de cache (opcional; so se houver cache_*.csv)
    print("\n== Analise de cache ==")
    analyze_cache(args.raw, args.out)

    print("\nConcluido.")


if __name__ == "__main__":
    main()
