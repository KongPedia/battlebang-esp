#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PIO="${PIO:-${REPO_ROOT}/.venv-pio/bin/pio}"
if [[ ! -x "${PIO}" ]]; then
  PIO="${PIO:-pio}"
fi

port="${1:-${UPLOAD_PORT:-}}"
if [[ -z "${port}" ]]; then
  shopt -s nullglob
  candidates=(/dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial* /dev/ttyUSB* /dev/ttyACM*)
  shopt -u nullglob
  if [[ ${#candidates[@]} -eq 1 ]]; then
    port="${candidates[0]}"
  else
    echo "[upload] Could not auto-select one ESP serial port." >&2
    echo "[upload] Candidates: ${candidates[*]:-(none)}" >&2
    echo "[upload] Device list:" >&2
    "${PIO}" device list >&2 || true
    echo "[upload] Usage: $0 /dev/cu.usbserial-0001" >&2
    exit 2
  fi
fi

echo "[upload] project=${SCRIPT_DIR}"
echo "[upload] port=${port}"
cd "${SCRIPT_DIR}"
"${PIO}" run -e esp32dev -t upload --upload-port "${port}"
