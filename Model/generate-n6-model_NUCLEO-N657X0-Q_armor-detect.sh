#!/bin/bash

set -eu # Exit on any error, Exit on unset variable

MODEL_FILE="armor_yolo11n_detect_640.onnx"
BOARD_DIR="NUCLEO-N657X0-Q"
PROFILE_JSON="user_neuralart_NUCLEO-N657X0-Q.json"

stedgeai generate --model "${MODEL_FILE}" --target stm32n6 --st-neural-art "default@${PROFILE_JSON}" --input-data-type uint8 --output-data-type int8

cp st_ai_output/network.c "${BOARD_DIR}/"
cp st_ai_output/network_ecblobs.h "${BOARD_DIR}/"
cp st_ai_output/stai_network.c "${BOARD_DIR}/"
cp st_ai_output/stai_network.h "${BOARD_DIR}/"
cp st_ai_output/network_atonbuf.xSPI2.raw "${BOARD_DIR}/network_data.xSPI2.bin"
arm-none-eabi-objcopy -I binary "${BOARD_DIR}/network_data.xSPI2.bin" --change-addresses 0x70380000 -O ihex "${BOARD_DIR}/network_data.hex"
