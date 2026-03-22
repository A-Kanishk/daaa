# Assignment 1 Submission - Team 4

## Contents
- `REPORT.pdf` - Final report/documentation
- `brandes.cpp` - Source code (Brandes' algorithm for Betweenness Centrality)
- `run.sh` - Execution script for TA (compiles, downloads data if needed, runs)
- `README.md` - This file

## Data Files (Preprocessed Links)
The following SNAP datasets are used. They are edge-list files and our loader
performs preprocessing at read-time (ignores comments, removes self-loops,
removes duplicate undirected edges, treats graph as unweighted-undirected).

| Dataset | Download Link |
|---------|---------------|
| wiki-Vote | https://snap.stanford.edu/data/wiki-Vote.txt.gz |
| email-Enron | https://snap.stanford.edu/data/email-Enron.txt.gz |
| as-Skitter | https://snap.stanford.edu/data/as-skitter.txt.gz |

The `run.sh` script will **automatically download and extract** these datasets
if they are not already present in the working directory.

## Installation / Run Instructions (TA)
```bash
chmod +x run.sh
./run.sh
```

Optional modes:
```bash
./run.sh --skip-skitter   # run wiki-Vote + email-Enron only
./run.sh --only-skitter   # run as-skitter only (CPU exact)
```

This will:
1. Compile `brandes.cpp` with `g++ -O2 -std=c++17`
2. Download the three datasets (if not already present)
3. Run exact betweenness centrality on all three datasets
4. Output top 20 nodes, execution time, and space consumption for each

### Requirements
- `g++` with C++17 support
- `wget` and `gunzip` (for downloading datasets)
- Linux environment recommended

## Lightning AI (H200) Fast Path for as-skitter
This path is intended for running only the as-skitter dataset on a GPU machine.

Files added for this:
- `run_h200_skitter.sh` (one-command runner)
- `skitter_gpu_h200.py` (GPU BC with RAPIDS cuGraph)

Run on Lightning terminal:
```bash
chmod +x run_h200_skitter.sh
./run_h200_skitter.sh
```

If exact mode is too slow, use approximate mode with sampling:
```bash
./run_h200_skitter.sh --k 4096
```

Outputs generated:
- `results_as-skitter_gpu.txt` (Top-20 nodes + scores + timing + space info)
- `run_as-skitter_gpu.log` (terminal execution log)

Notes:
- Graph is treated as unweighted and undirected.
- Preprocessing removes self-loops and duplicate undirected edges.
- `run_h200_skitter.sh` uses a 3-hour timeout guard.

## Notes
- As required by the assignment, all graphs are treated as **unweighted and undirected**.
- as-Skitter exact computation can take a very long time on typical machines.
- Output files are saved as `results_wiki-Vote.txt`, `results_email-Enron.txt`, `results_as-skitter.txt`.
