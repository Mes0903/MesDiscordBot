# This is a convenience wrapper around CMake

# Build directory
BUILD_DIR = build
BINARY_NAME = terry_aoe2_dcbot

# Default target
.PHONY: all
all: release

# Create build directory and configure
.PHONY: configure
configure:
	@echo "Configuring project..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake ..

# Clean build files
.PHONY: clean
clean:
	@echo "Cleaning build files..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete!"

# Run the bot (assumes build is complete)
.PHONY: run
run: build
	@echo "Starting bot..."
	@./$(BUILD_DIR)/bin/$(BINARY_NAME)

# Development build with debug symbols
.PHONY: debug
debug:
	@echo "Configuring debug build..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Debug ..
	@echo "Building debug version..."
	@cmake --build build -j
	@echo "Debug build complete!"

# Release build with optimizations
.PHONY: release
release:
	@echo "Configuring release build..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release ..
	@echo "Building release version..."
	@cmake --build build -j
	@echo "Release build complete!"
