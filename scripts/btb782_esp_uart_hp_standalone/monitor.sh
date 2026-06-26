#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
if [[ -z "${PIO:-}" ]]; then
  if [[ -x "${REPO_ROOT}/.venv-pio/bin/pio" ]]; then
    PIO="${REPO_ROOT}/.venv-pio/bin/pio"
  else
    PIO="pio"
  fi
fi

port="${1:-${UPLOAD_PORT:-}}"
if [[ -z "${port}" ]]; then
  shopt -s nullglob
  candidates=(/dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial* /dev/ttyUSB* /dev/ttyACM*)
  shopt -u nullglob
  if [[ ${#candidates[@]} -eq 1 ]]; then
    port="${candidates[0]}"
  else
    "${PIO}" device list >&2 || true
    echo "Usage: $0 /dev/cu.usbserial-0001" >&2
    exit 2
  fi
fi

cd "${SCRIPT_DIR}"
"${PIO}" device monitor -p "${port}" -b 115200
