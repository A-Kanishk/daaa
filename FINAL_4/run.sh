#!/bin/bash
set -e

# ============================================================
#  Brandes' Betweenness Centrality — Execution Script
#  Team 4 | Assignment 1
# ============================================================

echo "============================================================"
echo "  Brandes' Betweenness Centrality — Assignment 1"
echo "============================================================"

RUN_WIKI=1
RUN_ENRON=1
RUN_SKITTER=1

for arg in "$@"; do
    case "$arg" in
        --skip-skitter)
            RUN_SKITTER=0
            ;;
        --only-skitter)
            RUN_WIKI=0
            RUN_ENRON=0
            RUN_SKITTER=1
            ;;
        --only-small)
            RUN_WIKI=1
            RUN_ENRON=1
            RUN_SKITTER=0
            ;;
        -h|--help)
            echo "Usage: ./run.sh [--skip-skitter | --only-skitter | --only-small]"
            echo "  --skip-skitter  Run wiki-Vote and email-Enron only"
            echo "  --only-skitter  Run as-skitter only"
            echo "  --only-small    Run wiki-Vote and email-Enron only"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Use --help for supported options."
            exit 1
            ;;
    esac
done

# --- Step 1: Compile ---
echo ""
echo "[1/3] Compiling brandes.cpp ..."
g++ -O2 -std=c++17 -o brandes brandes.cpp
echo "      Compilation successful."

# --- Step 2: Download datasets if not present ---
echo ""
echo "[2/3] Checking datasets ..."

download_if_missing() {
    local FILE="$1"
    local URL="$2"
    if [ ! -f "$FILE" ]; then
        echo "      Downloading $FILE ..."
        wget -q "$URL" -O "${FILE}.gz"
        gunzip "${FILE}.gz"
        echo "      Downloaded and extracted $FILE"
    else
        echo "      $FILE already present."
    fi
}

download_if_missing "wiki-Vote.txt"   "https://snap.stanford.edu/data/wiki-Vote.txt.gz"
download_if_missing "email-Enron.txt"  "https://snap.stanford.edu/data/email-Enron.txt.gz"
if [ "$RUN_SKITTER" -eq 1 ]; then
    download_if_missing "as-skitter.txt"   "https://snap.stanford.edu/data/as-skitter.txt.gz"
fi

# --- Step 3: Run on all three datasets ---
echo ""
echo "[3/3] Running Brandes' algorithm on all datasets ..."
echo ""

if [ "$RUN_WIKI" -eq 1 ]; then
    echo "--- Dataset 1: wiki-Vote ---"
    ./brandes -top 20 -o results_wiki-Vote.txt wiki-Vote.txt
    echo ""
fi

if [ "$RUN_ENRON" -eq 1 ]; then
    echo "--- Dataset 2: email-Enron ---"
    ./brandes -top 20 -o results_email-Enron.txt email-Enron.txt
    echo ""
fi

if [ "$RUN_SKITTER" -eq 1 ]; then
    echo "--- Dataset 3: as-Skitter (this can take very long) ---"
    ./brandes -top 20 -o results_as-skitter.txt as-skitter.txt
    echo ""
fi

echo "============================================================"
echo "  All runs complete."
echo "  Results saved to:"
echo "    - results_wiki-Vote.txt"
echo "    - results_email-Enron.txt"
echo "    - results_as-skitter.txt"
echo "============================================================"
