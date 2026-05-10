#!/bin/bash

set -eu

MODEL_PATH="${1:-nitish_red_blue_obb_224_robomaster_v3_qdq.onnx}"
OUTPUT_DIR="NUCLEO-N657X0-Q_SafalObb"

mkdir -p "${OUTPUT_DIR}"

if ! stedgeai generate --model "${MODEL_PATH}" --target stm32n6 --st-neural-art default@user_neuralart_NUCLEO-N657X0-Q.json --input-data-type uint8 --output-data-type int8; then
  if [ ! -f st_ai_output/network.c ] || [ ! -f st_ai_output/network_atonbuf.xSPI2.raw ] || [ ! -f st_ai_output/stai_network.c ] || [ ! -f st_ai_output/stai_network.h ] || [ ! -f st_ai_output/network_ecblobs.h ]; then
    exit 1
  fi
  echo "stedgeai returned non-zero after generating usable artifacts; packaging generated outputs anyway."
fi

cp st_ai_output/network.c "${OUTPUT_DIR}/"
cp st_ai_output/network_ecblobs.h "${OUTPUT_DIR}/"
cp st_ai_output/stai_network.c "${OUTPUT_DIR}/"
cp st_ai_output/stai_network.h "${OUTPUT_DIR}/"
cp st_ai_output/network_atonbuf.xSPI2.raw "${OUTPUT_DIR}/network_data.xSPI2.bin"
arm-none-eabi-objcopy -I binary "${OUTPUT_DIR}/network_data.xSPI2.bin" --change-addresses 0x70380000 -O ihex "${OUTPUT_DIR}/network_data.hex"
