# Compiler and basic settings
CC         := clang++
CXXFLAGS   := -O3 -g -std=c++20 -fno-omit-frame-pointer
LINK_FLAGS := -g -O3 -std=c++20 -lpmemobj

# Include directories
INC_DIRS := -Iinclude/ -Izipf_base/src

# Executable and object files
EXE  := ./bin/ycsb
OBJS := src/main.o src/ycsb_benchmark.o zipf_base/src/ycsbutils.o zipf_base/src/zipfian_generator.o

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
	$(CC) -c $(CXXFLAGS) $(INC_DIRS) $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJS) $(EXE)
