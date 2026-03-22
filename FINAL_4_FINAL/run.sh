#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SKIP_SKITTER=0
for arg in "$@"; do
    case "$arg" in
        --skip-skitter)
            SKIP_SKITTER=1
            ;;
        -h|--help)
            echo "Usage: ./run.sh [--skip-skitter]"
            echo "  --skip-skitter   Run only wiki-Vote and email-Enron"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Use --help for usage."
            exit 1
            ;;
    esac
done

# ============================================================
#  Brandes' Betweenness Centrality — Execution Script
#  Team 4 | Assignment 1
# ============================================================

echo "============================================================"
echo "  Brandes' Betweenness Centrality — Assignment 1"
echo "============================================================"

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
if [ "$SKIP_SKITTER" -eq 0 ]; then
    download_if_missing "as-skitter.txt"   "https://snap.stanford.edu/data/as-skitter.txt.gz"
fi

# --- Step 3: Run on all three datasets ---
echo ""
echo "[3/3] Running Brandes' algorithm on all datasets ..."
echo ""

echo "--- Dataset 1: wiki-Vote ---"
./brandes -top 20 -o results_wiki-Vote.txt wiki-Vote.txt
echo ""

echo "--- Dataset 2: email-Enron ---"
./brandes -top 20 -o results_email-Enron.txt email-Enron.txt
echo ""

if [ "$SKIP_SKITTER" -eq 0 ]; then
    echo "--- Dataset 3: as-Skitter (this can take very long) ---"
    ./brandes -top 20 -o results_as-skitter.txt as-skitter.txt
    echo ""
fi

echo "============================================================"
echo "  All runs complete."
echo "  Results saved to:"
echo "    - results_wiki-Vote.txt"
echo "    - results_email-Enron.txt"
if [ "$SKIP_SKITTER" -eq 0 ]; then
    echo "    - results_as-skitter.txt"
else
    echo "    - as-skitter skipped (use ./run.sh without --skip-skitter for full run)"
fi
echo "============================================================"
