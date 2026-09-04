#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
arduino_libraries_dir="${ARDUINO_LIBRARIES_DIR:-${HOME}/Arduino/libraries}"
tft_espi_dir="${arduino_libraries_dir}/TFT_eSPI"
port="${1:-}"

if [[ ! -d "${tft_espi_dir}" ]]; then
  echo "TFT_eSPI was not found at ${tft_espi_dir}." >&2
  echo "Set ARDUINO_LIBRARIES_DIR to the directory containing TFT_eSPI." >&2
  exit 1
fi

if [[ -z "${port}" ]]; then
  port="$(arduino-cli board list | awk '/Adafruit QT Py RP2040/ { print $1; exit }')"
fi

if [[ -z "${port}" ]]; then
  echo "No QT Py RP2040 serial port found. Pass it explicitly, for example:" >&2
  echo "  ./flash.sh /dev/ttyACM1" >&2
  exit 1
fi

work_dir="$(mktemp -d /tmp/tiny-moon-build.XXXXXX)"
cleanup() {
  if [[ "${work_dir}" == /tmp/tiny-moon-build.* ]]; then
    rm -rf -- "${work_dir}"
  fi
}
trap cleanup EXIT

mkdir -p "${work_dir}/libraries"
cp -a "${tft_espi_dir}" "${work_dir}/libraries/TFT_eSPI"
patch --silent -d "${work_dir}/libraries/TFT_eSPI" -p1 \
  < "${project_dir}/patches/tft-espi-rp2040-shared-spi.patch"

arduino-cli compile \
  --fqbn rp2040:rp2040:adafruit_qtpy \
  --libraries "${work_dir}/libraries" \
  --build-path "${work_dir}/build" \
  --build-property \
    "build.extra_flags=-include${project_dir}/tiny_moon/qtpy_round_display_setup.h" \
  "${project_dir}/tiny_moon"

arduino-cli upload \
  --port "${port}" \
  --fqbn rp2040:rp2040:adafruit_qtpy \
  --build-path "${work_dir}/build" \
  "${project_dir}/tiny_moon"

echo "tiny-moon flashed to ${port}"
