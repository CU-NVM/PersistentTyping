# Build settings for the persistent Stack test program.
CXX        := clang++
CXXFLAGS   := -O3 -g -std=c++17

# Headers: local include/ for Stack.hpp / Node.hpp, persistentLib for the wrapper.
PERSIST_LIB ?= $(realpath $(CURDIR)/../persistentLib)
INC_DIRS   := -Iinclude -I$(PERSIST_LIB)

LINK_FLAGS := -lpmemobj

# Executable and object files
EXE  := bin/datastructures
OBJS := src/main.o

# Default target
all: $(EXE)

# Linking
$(EXE): $(OBJS)
	@mkdir -p bin
	$(CXX) $(OBJS) $(LINK_FLAGS) -o $(EXE)

# Generic rule for compiling .cpp to .o
%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $(INC_DIRS) $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJS) $(EXE)
