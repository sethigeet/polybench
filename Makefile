BUILD_DIR_DEBUG = build/debug
BUILD_DIR_RELEASE = build/release
BUILD_TYPE ?= Debug
BUILD_TYPE_LC := $(shell printf '%s' '$(BUILD_TYPE)' | tr A-Z a-z | cut -c 1-3)
EXECUTABLE = polybench
RUN_CONFIG ?= config.example.json
BENCH_FILTER ?= 

ifeq ($(BUILD_TYPE_LC),rel)
    BUILD_DIR = $(BUILD_DIR_RELEASE)
else
    BUILD_DIR = $(BUILD_DIR_DEBUG)
endif

.PHONY: all configure configure-test configure-bench build build-test bench-build run test bench clean coverage pgo-generate pgo-build pgo-clean

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -G Ninja

configure-test:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTS=ON -G Ninja

configure-bench:
#  Do not use debug build for benchmarks
  ifeq ($(BUILD_TYPE), Debug)
		cmake -S . -B $(BUILD_DIR_RELEASE) -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON -G Ninja
  else
		cmake -S . -B $(BUILD_DIR_RELEASE) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_BENCHMARKS=ON -G Ninja
  endif

build: configure
	cmake --build $(BUILD_DIR) --parallel $(shell nproc)

build-test: configure-test
	find $(BUILD_DIR) -iname '*.gcno' -delete && \
	cmake --build $(BUILD_DIR) --parallel $(shell nproc)

bench-build: configure-bench
	cmake --build $(BUILD_DIR_RELEASE) --parallel $(shell nproc)

run: build
	./$(BUILD_DIR)/$(EXECUTABLE) --config $(RUN_CONFIG)

test: build-test
	cd $(BUILD_DIR) && \
	find . -iname '*.gcda' -delete && \
	ctest --output-on-failure

coverage:
	mkdir -p coverage && \
	uvx gcovr -r . --filter src $(BUILD_DIR) --html --html-details -o coverage/coverage.html && \
	python3 -m http.server -d coverage 8000

bench: bench-build
ifneq ($(BENCH_FILTER),)
	./$(BUILD_DIR_RELEASE)/polybench_benchmarks --benchmark_filter="$(BENCH_FILTER)"
else
	./$(BUILD_DIR_RELEASE)/polybench_benchmarks
endif

clean:
	rm -rf build/
	rm -rf coverage/

# Profile-Guided Optimization (PGO) workflow
# Step 1: Build with instrumentation
pgo-generate:
	cmake -S . -B $(BUILD_DIR_RELEASE) -DCMAKE_BUILD_TYPE=Release -DPGO_GENERATE=ON -G Ninja
	cmake --build $(BUILD_DIR_RELEASE) --parallel $(shell nproc)
	@echo "PGO Step 1 complete. Run your workload, then run 'make pgo-build'"

# Step 2: Build optimized binary using profile data
pgo-build:
	cmake -S . -B $(BUILD_DIR_RELEASE) -DCMAKE_BUILD_TYPE=Release -DPGO_USE=ON -G Ninja
	cmake --build $(BUILD_DIR_RELEASE) --clean-first --parallel $(shell nproc)
	@echo "PGO build complete. Binary: $(BUILD_DIR_RELEASE)/$(EXECUTABLE)"

# Clean profile data
pgo-clean:
	rm -rf $(BUILD_DIR_RELEASE)/pgo
	@echo "PGO profile data cleaned"
