#!/usr/bin/env python3
import argparse
import os
import time
import math
import pandas as pd


def _try_gpu_mem_query():
    try:
        import pynvml
        pynvml.nvmlInit()
        h = pynvml.nvmlDeviceGetHandleByIndex(0)
        mem = pynvml.nvmlDeviceGetMemoryInfo(h)
        return {
            "total": int(mem.total),
            "used": int(mem.used),
            "free": int(mem.free),
            "name": pynvml.nvmlDeviceGetName(h).decode("utf-8") if hasattr(pynvml.nvmlDeviceGetName(h), "decode") else str(pynvml.nvmlDeviceGetName(h)),
        }
    except Exception:
        return None


def _fmt_bytes(value):
    if value is None:
        return "N/A"
    units = ["B", "KB", "MB", "GB", "TB"]
    x = float(value)
    u = 0
    while x >= 1024.0 and u < len(units) - 1:
        x /= 1024.0
        u += 1
    return f"{x:.2f} {units[u]}"


def main():
    parser = argparse.ArgumentParser(description="GPU betweenness for as-skitter on Lightning H200")
    parser.add_argument("--input", default="as-skitter.txt", help="Input edge list file")
    parser.add_argument("--output", default="results_as-skitter_gpu.txt", help="Output results file")
    parser.add_argument("--top", type=int, default=20, help="Top-k nodes to print")
    parser.add_argument("--k", type=int, default=None, help="Sample count for approximate BC (omit for exact)")
    parser.add_argument("--seed", type=int, default=42, help="Seed for approximate BC")
    args = parser.parse_args()

    t0 = time.time()

    import cudf
    import cugraph

    if not os.path.exists(args.input):
        raise FileNotFoundError(f"Input file not found: {args.input}")

    mem_before = _try_gpu_mem_query()

    edges = cudf.read_csv(
        args.input,
        delim_whitespace=True,
        comment="#",
        header=None,
        names=["src", "dst"],
        usecols=[0, 1],
        dtype=["int64", "int64"],
    )

    edges = edges[edges["src"] != edges["dst"]]

    lo = edges[["src", "dst"]].min(axis=1)
    hi = edges[["src", "dst"]].max(axis=1)
    edges = cudf.DataFrame({"src": lo, "dst": hi}).drop_duplicates(ignore_index=True)

    g = cugraph.Graph(directed=False)
    g.from_cudf_edgelist(edges, source="src", destination="dst", renumber=False)

    t_load_done = time.time()

    bc = cugraph.betweenness_centrality(
        g,
        k=args.k,
        normalized=False,
        endpoints=False,
        seed=args.seed,
    )

    t_bc_done = time.time()

    col = "betweenness_centrality"
    if col not in bc.columns:
        possible = [c for c in bc.columns if c != "vertex"]
        if not possible:
            raise RuntimeError("Could not locate BC score column in cuGraph output")
        col = possible[0]

    top_df = bc.sort_values(col, ascending=False).head(args.top)
    top_pd = top_df.to_pandas()

    mem_after = _try_gpu_mem_query()

    total_time = t_bc_done - t0
    load_time = t_load_done - t0
    bc_time = t_bc_done - t_load_done

    mode = "APPROXIMATE" if args.k is not None else "EXACT"

    with open(args.output, "w", encoding="utf-8") as f:
        f.write("================================================================\n")
        f.write("  GPU BETWEENNESS CENTRALITY — AS-SKITTER\n")
        f.write("================================================================\n")
        f.write(f"Mode: {mode}\n")
        if args.k is not None:
            f.write(f"k (sample sources): {args.k}\n")
        f.write(f"Input: {args.input}\n")
        f.write(f"Top-k: {args.top}\n")
        f.write("Graph type: Undirected, Unweighted\n")
        f.write("Preprocessing: removed self-loops and duplicate undirected edges\n")
        f.write("----------------------------------------------------------------\n")
        f.write(f"Load+preprocess time: {load_time:.3f} seconds\n")
        f.write(f"BC compute time:      {bc_time:.3f} seconds\n")
        f.write(f"Total time:           {total_time:.3f} seconds\n")

        if mem_before is not None:
            f.write(f"GPU:                  {mem_before['name']}\n")
            f.write(f"GPU memory before:    used={_fmt_bytes(mem_before['used'])}, free={_fmt_bytes(mem_before['free'])}, total={_fmt_bytes(mem_before['total'])}\n")
        if mem_after is not None:
            f.write(f"GPU memory after:     used={_fmt_bytes(mem_after['used'])}, free={_fmt_bytes(mem_after['free'])}, total={_fmt_bytes(mem_after['total'])}\n")

        f.write("================================================================\n\n")
        f.write("Rank\tNode ID\tBetweenness\n")
        f.write("----\t-------\t-----------\n")

        rank = 1
        for row in top_pd.itertuples(index=False):
            vertex = int(getattr(row, "vertex"))
            score = float(getattr(row, col))
            f.write(f"{rank}\t{vertex}\t{score:.6f}\n")
            rank += 1

    print(f"Wrote: {args.output}")
    print(f"Mode: {mode}")
    print(f"Total time: {total_time:.3f} seconds")


if __name__ == "__main__":
    main()
