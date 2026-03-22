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
PYTHON_BIN="python3"

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
"${PYTHON_BIN}" -m pip install --upgrade pip
"${PYTHON_BIN}" -m pip install --extra-index-url=https://pypi.nvidia.com \
  cupy-cuda12x pynvml pandas

if command -v nvidia-smi >/dev/null 2>&1; then
  echo "Detected GPU/driver:"
  nvidia-smi --query-gpu=name,driver_version,cuda_version --format=csv,noheader || true
fi

RAPIDS_CANDIDATES=("26.2.*" "25.12.*" "25.10.*" "25.08.*")
RAPIDS_SELECTED=""

for RAPIDS_VER in "${RAPIDS_CANDIDATES[@]}"; do
  echo "Trying RAPIDS version: ${RAPIDS_VER}"
  if ! "${PYTHON_BIN}" -m pip install --extra-index-url=https://pypi.nvidia.com \
      "cudf-cu12==${RAPIDS_VER}" "cugraph-cu12==${RAPIDS_VER}" "rmm-cu12==${RAPIDS_VER}"; then
    echo "Install failed for RAPIDS ${RAPIDS_VER}, trying older version..."
    continue
  fi

  if "${PYTHON_BIN}" - <<'PY'
import cudf
import cugraph
from rmm._cuda.gpu import getDevice
_ = getDevice()
print("RAPIDS runtime check OK")
PY
  then
    RAPIDS_SELECTED="${RAPIDS_VER}"
    echo "Selected RAPIDS version: ${RAPIDS_SELECTED}"
    break
  else
    echo "Runtime check failed for RAPIDS ${RAPIDS_VER}, trying older version..."
  fi
done

if [[ -z "${RAPIDS_SELECTED}" ]]; then
  echo "FAILED" | tee "${ARTIFACT_DIR}/STATUS.txt"
  echo "Failed at: $(date -Is)" | tee "${ARTIFACT_DIR}/ENDED.txt"
  echo "No compatible RAPIDS version found for this Studio driver."
  echo "Please start a new Lightning Studio with newer NVIDIA driver/CUDA support and retry."
  exit 1
fi

echo "[2/5] Configuring RAPIDS/CUDA shared-library paths ..."
RAPIDS_LIB_DIRS=$("${PYTHON_BIN}" - <<'PY'
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
  "**/libnvJitLink.so*",
  "**/libcusolver.so*",
  "**/libcublas.so*",
  "**/libcusparse.so*",
  "**/libcurand.so*",
  "**/libnvrtc.so*",
  "**/libcudart.so*",
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

        extra_roots = [
          "/home/zeus/miniconda3/envs/cloudspace/lib/python3.12/site-packages/nvidia/nvjitlink/lib",
          "/home/zeus/miniconda3/envs/cloudspace/lib/python3.12/site-packages/nvidia/cusolver/lib",
          "/home/zeus/miniconda3/envs/cloudspace/lib/python3.12/site-packages/nvidia/cusparse/lib",
          "/home/zeus/miniconda3/envs/cloudspace/lib/python3.12/site-packages/nvidia/cublas/lib",
          "/home/zeus/miniconda3/envs/cloudspace/lib/python3.12/site-packages/nvidia/curand/lib",
          "/home/zeus/miniconda3/envs/cloudspace/lib/python3.12/site-packages/nvidia/cuda_runtime/lib",
          "/usr/local/cuda/lib64",
          "/usr/lib/x86_64-linux-gnu",
          "/home/zeus/miniconda3/envs/cloudspace/lib",
        ]
        for root in extra_roots:
          if not os.path.exists(root):
            continue
          for pattern in ["libcusolver.so*", "libcublas.so*", "libcusparse.so*", "libcurand.so*", "libnvrtc.so*", "libcudart.so*"]:
            matches = glob.glob(os.path.join(root, pattern))
            if matches and root not in seen:
              seen.add(root)
              dirs.append(root)

print(":".join(dirs))
PY
)

if [[ -n "${RAPIDS_LIB_DIRS}" ]]; then
  export LD_LIBRARY_PATH="${RAPIDS_LIB_DIRS}:${LD_LIBRARY_PATH:-}"
  echo "Configured LD_LIBRARY_PATH with RAPIDS/CUDA libs."
else
  echo "WARNING: Could not auto-detect RAPIDS/CUDA .so directories."
fi

echo "Checking for libcusolver availability ..."
"${PYTHON_BIN}" - <<'PY'
import ctypes.util
path = ctypes.util.find_library("cusolver")
print(f"find_library('cusolver') => {path}")
PY

echo "Verifying cudf/cugraph import ..."
"${PYTHON_BIN}" - <<'PY'
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
  timeout "${TIMEOUT_HOURS}h" "${PYTHON_BIN}" skitter_gpu_h200.py --input as-skitter.txt --top 20 --output "${OUT_FILE}" 2>&1 | tee "${LOG_FILE}"
else
  "${PYTHON_BIN}" skitter_gpu_h200.py --input as-skitter.txt --top 20 --output "${OUT_FILE}" 2>&1 | tee "${LOG_FILE}"
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
