#!/bin/bash
set -euo pipefail

# H200-focused runner for as-skitter using RAPIDS cuGraph
# Usage examples:
#   ./run_h200_skitter.sh
#   ./run_h200_skitter.sh --timeout-hours 3

TIMEOUT_HOURS=""
RUN_ID="$(date +%Y%m%d_%H%M%S)"
ARTIFACT_BASE="artifacts"
ARTIFACT_DIR="${ARTIFACT_BASE}/skitter_${RUN_ID}"
LOG_FILE="${ARTIFACT_DIR}/run_as-skitter_gpu.log"
OUT_FILE="${ARTIFACT_DIR}/results_as-skitter_gpu.txt"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --timeout-hours)
      if [[ $# -lt 2 ]]; then
        echo "Error: --timeout-hours requires a value"
        exit 1
      fi
      TIMEOUT_HOURS="$2"
      shift 2
      ;;
    -h|--help)
      echo "Usage: ./run_h200_skitter.sh [--timeout-hours <hours>]"
      echo "  default: exact BC (true results) with no forced timeout"
      echo "  optional --timeout-hours: stop run after given hours"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

mkdir -p "${ARTIFACT_DIR}"

echo "Run ID: ${RUN_ID}"
echo "Artifact directory: ${ARTIFACT_DIR}"
echo "Started at: $(date -Is)" | tee "${ARTIFACT_DIR}/STARTED.txt"

echo "[1/5] Installing Python dependencies for GPU run ..."
python3 -m pip install --upgrade pip
python3 -m pip install --extra-index-url=https://pypi.nvidia.com \
  cudf-cu12 cugraph-cu12 rmm-cu12 cupy-cuda12x pynvml pandas

echo "[2/5] Configuring RAPIDS shared-library paths ..."
RAPIDS_LIB_DIRS=$(python3 - <<'PY'
import glob
import os
import site

roots = []
roots.extend(site.getsitepackages())
user_site = site.getusersitepackages()
if isinstance(user_site, str):
    roots.append(user_site)

patterns = [
    "**/libcugraph.so",
    "**/libcudf.so",
    "**/librmm.so",
    "**/libucxx.so",
    "**/libraft.so",
]

dirs = []
seen = set()
for root in roots:
    if not root or not os.path.exists(root):
        continue
    for pattern in patterns:
        for path in glob.glob(os.path.join(root, pattern), recursive=True):
            directory = os.path.dirname(path)
            if directory not in seen:
                seen.add(directory)
                dirs.append(directory)

print(":".join(dirs))
PY
)

if [[ -n "${RAPIDS_LIB_DIRS}" ]]; then
  export LD_LIBRARY_PATH="${RAPIDS_LIB_DIRS}:${LD_LIBRARY_PATH:-}"
  echo "Configured LD_LIBRARY_PATH with RAPIDS libs."
else
  echo "WARNING: Could not auto-detect RAPIDS .so directories."
fi

echo "Verifying cudf/cugraph import ..."
python3 - <<'PY'
import cudf
import cugraph
print("cudf/cugraph import OK")
PY

echo "[3/5] Downloading as-skitter if missing ..."
if [[ ! -f as-skitter.txt ]]; then
  wget -q https://snap.stanford.edu/data/as-skitter.txt.gz -O as-skitter.txt.gz
  gunzip -f as-skitter.txt.gz
fi

echo "[4/5] Running exact betweenness centrality on GPU ..."
set +e
if [[ -n "$TIMEOUT_HOURS" ]]; then
  timeout "${TIMEOUT_HOURS}h" python3 skitter_gpu_h200.py --input as-skitter.txt --top 20 --output "${OUT_FILE}" 2>&1 | tee "${LOG_FILE}"
else
  python3 skitter_gpu_h200.py --input as-skitter.txt --top 20 --output "${OUT_FILE}" 2>&1 | tee "${LOG_FILE}"
fi
EXIT_CODE=$?
set -e

if [[ ${EXIT_CODE} -eq 124 ]]; then
  echo "TIMED_OUT" | tee "${ARTIFACT_DIR}/STATUS.txt"
  echo "Timed out at: $(date -Is)" | tee "${ARTIFACT_DIR}/ENDED.txt"
  echo "Run reached timeout. Logs saved to ${LOG_FILE}"
  exit 124
fi

if [[ ${EXIT_CODE} -ne 0 ]]; then
  echo "FAILED" | tee "${ARTIFACT_DIR}/STATUS.txt"
  echo "Failed at: $(date -Is)" | tee "${ARTIFACT_DIR}/ENDED.txt"
  echo "Run failed with exit code ${EXIT_CODE}. Logs saved to ${LOG_FILE}"
  exit ${EXIT_CODE}
fi

echo "SUCCESS" | tee "${ARTIFACT_DIR}/STATUS.txt"
echo "Completed at: $(date -Is)" | tee "${ARTIFACT_DIR}/ENDED.txt"
if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "${OUT_FILE}" > "${ARTIFACT_DIR}/results.sha256"
fi

cp -f "${OUT_FILE}" results_as-skitter_gpu.txt
cp -f "${LOG_FILE}" run_as-skitter_gpu.log

echo "[5/5] Done"
echo "  - ${OUT_FILE}"
echo "  - ${LOG_FILE}"
echo "  - results_as-skitter_gpu.txt (latest copy)"
echo "  - run_as-skitter_gpu.log (latest copy)"
