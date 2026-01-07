BUILD_DIR = build
BUILD_TYPE ?= Debug
EXECUTABLE = polybench
RUN_CONFIG ?= config.example.json

.PHONY: all configure configure-test build build-test run test clean

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTS=OFF -G Ninja

configure-test:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTS=ON -G Ninja

build: configure
	cmake --build $(BUILD_DIR) --parallel $(shell nproc)

build-test: configure-test
	cmake --build $(BUILD_DIR) --parallel $(shell nproc)

run: build
	./$(BUILD_DIR)/$(EXECUTABLE) --config $(RUN_CONFIG)

test: build-test
	cd $(BUILD_DIR) && ctest --output-on-failure

clean:
	rm -rf $(BUILD_DIR)
