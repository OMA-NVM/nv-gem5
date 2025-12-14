# tests/test-progs/lab/makefile

# --- Host paths (resolved on host) ---
GEM5_ROOT_REL := ../../..
GEM5_ROOT_ABS := $(abspath $(GEM5_ROOT_REL))

# --- Container paths (fixed, because we mount GEM5_ROOT_ABS -> /work) ---
WORKDIR_CONT := /work/tests/test-progs/lab
M5_INC_CONT  := /work/include
M5_LIB_CONT  := /work/util/m5/build/arm64/out/libm5.a

# --- Docker image ---
DOCKCROSS_IMAGE ?= dockcross/linux-arm64:latest

# --- Build flags ---
CXXFLAGS := -Iinclude -I$(M5_INC_CONT) -std=gnu++17 -DinCIM=1 -DSTD_PRINT_OUTPUT
LDFLAGS  := -static
LDLIBS   := $(M5_LIB_CONT)

COMPILEFILE := src/desc_test.cpp

.PHONY: all clean m5lib

all: bin/hello64-static

# Build libm5.a inside container (ARM64)
m5lib:
	docker run --rm \
	  -v "$(GEM5_ROOT_ABS)":/work \
	  -w /work/util/m5 \
	  $(DOCKCROSS_IMAGE) \
	  bash -lc 'set -e; \
	    echo "[m5] building libm5.a..."; \
	    make -j$$(nproc) build/arm64/out/libm5.a'

bin/hello64-static: src/cim_api.cpp $(COMPILEFILE)
	docker run --rm \
	  -v "$(GEM5_ROOT_ABS)":/work \
	  -w $(WORKDIR_CONT) \
	  $(DOCKCROSS_IMAGE) \
	  bash -lc 'set -e; \
	    mkdir -p bin; \
	    echo "CXX=$$CXX"; \
	    if [ ! -f "$(M5_LIB_CONT)" ]; then \
	      echo "[m5] $(M5_LIB_CONT) not found, build it first..."; \
	      cd /work/util/m5; \
	      make -j$$(nproc) build/arm64/out/libm5.a; \
	      cd $(WORKDIR_CONT); \
	    fi; \
	    $$CXX $(CXXFLAGS) src/cim_api.cpp $(COMPILEFILE) -o bin/hello64-static $(LDFLAGS) $(LDLIBS)'

clean:
	rm -rf bin/hello64-static