#!/bin/bash

readonly IMAGE_NAME="ghcr.io/times-z/devcontainer:latest"
readonly PIO_DIR=".pio/build/esp12e"
# Both images are collected here so a release is built out of one directory
# rather than dug out of the platformio tree. Gitignored.
readonly BUILD_DIR="build"
readonly PIO_BIN="/home/debian/.platformio/penv/bin/pio"

mkdir -p .pio "${BUILD_DIR}"

docker run --rm \
  -v "$(pwd):/workspace" \
  -w /workspace \
  -v "$(pwd)/.pio:/tmp/.platformio" \
  -e PLATFORMIO_CORE_DIR=/tmp/.platformio \
  -u "$(id -u):$(id -g)" \
  "$IMAGE_NAME" ${PIO_BIN} run

docker run --rm \
  -v "$(pwd):/workspace" \
  -w /workspace \
  -v "$(pwd)/.pio:/tmp/.platformio" \
  -e PLATFORMIO_CORE_DIR=/tmp/.platformio \
  -u "$(id -u):$(id -g)" \
  "$IMAGE_NAME" ${PIO_BIN} run --target buildfs

for image in firmware.bin littlefs.bin; do
  if [ -f "${PIO_DIR}/${image}" ]; then
    cp "${PIO_DIR}/${image}" "${BUILD_DIR}/${image}"
  else
    echo "warning: ${PIO_DIR}/${image} was not produced" >&2
  fi
done

echo "Done! Binaries in ${BUILD_DIR}/"

ls -la ${BUILD_DIR}/*.bin 2>/dev/null || echo "No .bin files found :("
