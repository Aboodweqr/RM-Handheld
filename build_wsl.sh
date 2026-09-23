#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v idf.py >/dev/null 2>&1; then
    idf_export=""
    if [[ -n "${IDF_PATH:-}" && -f "${IDF_PATH}/export.sh" ]]; then
        idf_export="${IDF_PATH}/export.sh"
    elif [[ -f "${HOME}/esp/esp-idf/export.sh" ]]; then
        idf_export="${HOME}/esp/esp-idf/export.sh"
    else
        idf_export="$(find "${HOME}" -maxdepth 5 -type f \
            -path '*/esp-idf/export.sh' -print -quit 2>/dev/null || true)"
    fi

    if [[ -z "${idf_export}" ]]; then
        echo "ERROR: ESP-IDF export.sh was not found inside WSL."
        echo "Expected it at ~/esp/esp-idf/export.sh"
        exit 2
    fi

    # shellcheck disable=SC1090
    source "${idf_export}"
fi

cd "${project_root}/firmware"
idf.py set-target esp32s3
idf.py build

cd build
if command -v esptool.py >/dev/null 2>&1; then
    esptool.py --chip esp32s3 merge_bin \
        -o rm_handheld_v0.1.5_merged.bin @flash_args
else
    python -m esptool --chip esp32s3 merge_bin \
        -o rm_handheld_v0.1.5_merged.bin @flash_args
fi

echo "Created firmware/build/rm_handheld_v0.1.5_merged.bin"
