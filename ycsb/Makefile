# Compiler and basic settings
CC         := clang++
CXXFLAGS   := -O3 -g -std=c++20 -pthread -fno-omit-frame-pointer -D_NODE_HPP=1 -DPIN_INIT=1 #-DMAX_NODE=$(MAX_NODE_ID) -DNUMA_NODE_NUM=$(NUM_NUMA_NODES)
LINK_FLAGS := -lnuma -g -O3 -std=c++20 -pthread
  
$(info Compiling with Max node set to $(MAX_NODE_ID))
$(info Number of numa nodes is set to $(NUM_NUMA_NODES))
# ROOT_DIR: Use provided value, otherwise default to $HOME/NUMATyping
ROOT_DIR   ?= $(HOME)/NUMATyping

# Include directories
INC_DIRS := -Iinclude/ -Izipf_base/src -I$(ROOT_DIR)/numaLib
UMF_INC_DIRS :=

# Executable and object files
EXE  := ./bin/ycsb
OBJS := src/main.o src/ycsb_benchmark.o zipf_base/src/ycsbutils.o zipf_base/src/zipfian_generator.o

# Unified Memory Framework (optional)
# Usage: make UMF=1 JEMALLOC_ROOT=/path/to/jemalloc
ifeq ($(UMF), 1)
    LINK_FLAGS += -lhwloc -lnuma -lrt -ldl -ljemalloc \
        $(ROOT_DIR)/unified-memory-framework/build/lib/libumf.a \
        $(ROOT_DIR)/unified-memory-framework/build/lib/libjemalloc_pool.a \
        -DUMF

    UMF_INC_DIRS += -I$(ROOT_DIR)/unified-memory-framework/src/utils \
        -I$(ROOT_DIR)/unified-memory-framework/include \
        -I$(ROOT_DIR)/unified-memory-framework/examples/common \
        -I$(ROOT_DIR)/unified-memory-framework/src \
        -I$(ROOT_DIR)/unified-memory-framework/src/ravl \
        -I$(ROOT_DIR)/unified-memory-framework/src/critnib \
        -I$(ROOT_DIR)/unified-memory-framework/src/provider \
        -I$(ROOT_DIR)/unified-memory-framework/src/memspaces \
        -I$(ROOT_DIR)/unified-memory-framework/src/memtargets \
        -DUMF

    # Only add Jemalloc-specific flags if JEMALLOC_ROOT is defined
    ifdef JEMALLOC_ROOT
        UMF_INC_DIRS += -I$(JEMALLOC_ROOT)/include
        LINK_FLAGS   += -L$(JEMALLOC_ROOT)/lib -Wl,-rpath,$(JEMALLOC_ROOT)/lib
    endif
endif

# Enable DEBUG if defined
ifdef DEBUG
    CXXFLAGS += -DDEBUG
endif

# Default target
all: $(EXE)

# Linking
$(EXE): $(OBJS)
	@mkdir -p bin
	$(CC) $(OBJS) $(LINK_FLAGS) -o $(EXE)

# Generic rule for compiling .cpp to .o
%.o: %.cpp
	$(CC) -c $(CXXFLAGS) $(INC_DIRS) $(UMF_INC_DIRS) $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJS) $(EXE)

