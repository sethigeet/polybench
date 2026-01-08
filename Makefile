BUILD_DIR = build
BUILD_TYPE ?= Debug
EXECUTABLE = polybench
RUN_CONFIG ?= config.example.json
BENCH_FILTER ?= 

.PHONY: all configure configure-test configure-bench build build-test bench-build run test bench clean

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTS=OFF -G Ninja

configure-test:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTS=ON -G Ninja

configure-bench:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON -G Ninja

build: configure
	cmake --build $(BUILD_DIR) --parallel $(shell nproc)

build-test: configure-test
	cmake --build $(BUILD_DIR) --parallel $(shell nproc)

bench-build: configure-bench
	cmake --build $(BUILD_DIR) --parallel $(shell nproc)

run: build
	./$(BUILD_DIR)/$(EXECUTABLE) --config $(RUN_CONFIG)

test: build-test
	cd $(BUILD_DIR) && ctest --output-on-failure

bench: bench-build
ifneq ($(BENCH_FILTER),)
	./$(BUILD_DIR)/polybench_benchmarks --benchmark_filter="$(BENCH_FILTER)"
else
	./$(BUILD_DIR)/polybench_benchmarks
endif

clean:
	rm -rf $(BUILD_DIR)
