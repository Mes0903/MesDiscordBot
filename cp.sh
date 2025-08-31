#!/bin/bash
TARGET_DIR="output_dir"
mkdir -p "$TARGET_DIR"

cp include/*/*.hpp "$TARGET_DIR" 2>/dev/null
cp src/*.cpp "$TARGET_DIR" 2>/dev/null
cp src/*/*.cpp "$TARGET_DIR" 2>/dev/null
