# Constructor tests for persist-tool.
#
#   make                 transform + compile every test
#   make init_list       transform + compile one test (any <name>.cpp here)
#   make transform       only run the tool over every test
#   make results         build everything, then run ./results.py
#   make clean           remove outputs/
#
# For each <name>.cpp:
#   outputs/<name>.cpp   transformed by root-setup then recurse-persistent
#   outputs/<name>       the transformed program (persistent)
#   outputs/<name>.ref   the original compiled as plain C++, the reference

ROOT     := $(abspath ../..)
TOOL     := $(ROOT)/persist-clang-tool/build/bin/persist-tool
LIB      := $(ROOT)/persistentLib
OUT      := outputs

CXX      := clang++
CXXFLAGS := -std=c++17 -I$(LIB) -I$(OUT) -Wno-literal-conversion
LDLIBS   := -lpmemobj
TOOLARGS := -I $(LIB) -I $(abspath $(OUT)) -I /usr/local/lib/clang/20/include -std=c++17

TESTS    := $(basename $(wildcard *.cpp))

MAKEFLAGS += --no-builtin-rules
.SUFFIXES:
.DELETE_ON_ERROR:
.PRECIOUS: $(OUT)/%.cpp
.PHONY: all transform results clean $(TESTS)

all: $(TESTS)

$(TESTS): %: $(OUT)/% $(OUT)/%.ref

transform: $(addprefix $(OUT)/,$(addsuffix .cpp,$(TESTS)))

$(OUT)/harness.hpp: harness.hpp
	@mkdir -p $(OUT)
	cp $< $@

# The tool rewrites the file in place, so work on the copy.
$(OUT)/%.cpp: %.cpp $(OUT)/harness.hpp $(TOOL)
	cp $< $@
	$(TOOL) --pass=root-setup $(abspath $@) -- $(TOOLARGS) > $(OUT)/$*.tool.log 2>&1
	$(TOOL) --pass=recurse-persistent $(abspath $@) -- $(TOOLARGS) >> $(OUT)/$*.tool.log 2>&1
	@! grep -q "error:" $(OUT)/$*.tool.log || { grep "error:" $(OUT)/$*.tool.log; rm -f $@; exit 1; }

$(OUT)/%: $(OUT)/%.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS) > $(OUT)/$*.build.log 2>&1 || { cat $(OUT)/$*.build.log; exit 1; }

$(OUT)/%.ref: %.cpp $(OUT)/harness.hpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

# -k: one test failing to transform or compile must not hide the others.
results:
	-@$(MAKE) -k --no-print-directory all
	./results.py

clean:
	rm -rf $(OUT)
