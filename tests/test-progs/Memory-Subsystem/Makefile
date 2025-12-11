# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

CC  = gcc

# Path to the gem5 root (override with `make GEM5=/path/to/gem5`)
GEM5 ?= ../../..
GEM5_INC      = -I$(GEM5)/include
GEM5_LIB_DIR  = $(GEM5)/util/m5/build/x86/out
GEM5_M5_LIB   = -L$(GEM5_LIB_DIR) -lm5

# STREAM options:
# -DNTIMES control the number of times each stream kernel is executed
# -DOFFSET controls the number of bytes between each of the buffers
# -DSTREAM_TYPE specifies the data-type of elements in the buffers
# -DSTREAM_ARRAY_SIZE specifies the number of elements in each buffer
STREAM_CPP_OPTS   = -DNTIMES=5 -DOFFSET=0 -DSTREAM_TYPE=double 
# Size per array is approx. ~536 MiB. Delibrately using non-power of 2 elements
# 1024*1024 elements = 536870912 elements = 536MiB with FP64
STREAM_ARRAY_SIZE = 1048576

ifdef size
STREAM_ARRAY_SIZE = $(size)
endif
STREAM_CPP_OPTS  += -DSTREAM_ARRAY_SIZE=$(STREAM_ARRAY_SIZE) -Wno-unknown-pragmas -Wno-unused-but-set-variable

# Common compiler options for both workloads
COMMON_COPTS   = -Wall -O3 -static
BINS           = stream.bin mm.bin daxpy.bin

all: $(BINS)

stream.o: stream.c
	$(CC) $(COMMON_COPTS) $(STREAM_CPP_OPTS) $(GEM5_INC) -c $< -o $@

stream.bin: stream.o
	$(CC) -DDRAM $(COMMON_COPTS) $(STREAM_CPP_OPTS)  $^ $(GEM5_M5_LIB) -o $@
	$(CC) $(COMMON_COPTS) $(STREAM_CPP_OPTS) $^ $(GEM5_M5_LIB) -o nvm-$@

mm.o: mm.c
	$(CC) $(COMMON_COPTS) $(GEM5_INC) -c $< -o $@

mm.bin: mm.o
	$(CC) $(COMMON_COPTS) $^ $(GEM5_M5_LIB) -o $@

daxpy.o: daxpy.c
	$(CC) $(COMMON_COPTS) $(GEM5_INC) -c $< -o $@

daxpy.bin: daxpy.o
	$(CC) $(COMMON_COPTS) $^ $(GEM5_M5_LIB) -o $@


help:
	@echo -e "Running 'make' with no options would compile the STREAM benchmark with $(STREAM_ARRAY_SIZE) FP64 elements per array for following Intel CPU's:\n"
	@echo -e "\tstream_avx.bin        => Targeted for Intel CPU's that support AVX ISA"
	@echo -e "\tstream_avx2.bin       => Targeted for Intel CPU's that support AVX2 ISA"
	@echo -e "\tstream_avx512.bin     => Targeted for Intel CPU's that support AVX512 ISA"
	@echo -e "\nSee BUILDING_M5.md for instructions on compiling libm5 (in addition to gem5-opt)."
	@echo -e "\nThe following options are supported:"
	@echo -e "\tsize=<number_of_elements_per_array>"
	@echo ""
	@echo -e "\tcpu=<avx,avx2,avx512>"
	@echo ""
	@echo -e "\trfo=1 forces to use regular cached stores instead of non-temporal stores"
	@echo ""
	@echo -e "\nFew examples:"
	@echo -e "To compile STREAM benchmark only for Intel AVX512 CPU's, do:"
	@echo -e "\tmake cpu=avx512"
	@echo ""
	@echo -e "To compile STREAM benchmark with each buffer containing 67108864 elements, do:"
	@echo -e "\tmake size=67108864"
	

clean:
	rm -rf *.o *.bin 

.PHONY: all clean help
