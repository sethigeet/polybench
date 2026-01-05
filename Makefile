BUILD_DIR = build
BUILD_TYPE ?= Debug
EXECUTABLE = polybench
RUN_CONFIG ?= config.example.json

.PHONY: all configure build run clean

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -G Ninja

build: configure
	cmake --build $(BUILD_DIR) --parallel $(shell nproc)

run: build
	./$(BUILD_DIR)/$(EXECUTABLE) --config $(RUN_CONFIG)

clean:
	rm -rf $(BUILD_DIR)
