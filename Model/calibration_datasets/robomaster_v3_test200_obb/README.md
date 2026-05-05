# RoboMaster v3 OBB Calibration Subset

This folder contains an OBB-compatible calibration subset derived from:

- [robomaster_v3_test200_detect](C:/Users/saysa/Documents/Robomaster_CodeStuff/stm32n6-sample/STM32N6-YOLO-Deploy/Model/calibration_datasets/robomaster_v3_test200_detect/README.md)

What was adapted:

- the same `200` images were kept
- the same `171` positive images were kept
- the same `29` negative/background-only images were kept
- each detect label `class x_center y_center width height` was converted into an axis-aligned OBB polygon:
  `class x1 y1 x2 y2 x3 y3 x4 y4`

Why this exists:

- your current quantization/export flow uses `task="obb"`
- Ultralytics OBB datasets require four-corner labels rather than 5-column detect labels

Important caveat:

- these are **converted axis-aligned OBB boxes**, not original rotated annotations
- that is acceptable for calibration compatibility, because calibration quality is driven mainly by representative images
- if you later reuse this dataset for training or evaluation, remember it does **not** preserve true object rotation

Files:

- `images/val`: selected images
- `labels/val`: converted OBB labels
- `selection_manifest.tsv`: source selection manifest
- `data.yaml`: YOLO OBB dataset config

Suggested use:

- point Ultralytics `data=` to this folder's `data.yaml` when exporting an INT8 OBB engine for calibration experiments
