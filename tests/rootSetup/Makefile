# root-setup tests for persist-tool. Each case is a directory in cases/ with a
# CASE file of expectations; run_tests.py documents the directives.
#
#   make                 run every case
#   make single_global   run one case (any directory in cases/)
#   make clean           remove outputs/
#
# Pass V=1 to print the output of failing cases.

TESTS := $(notdir $(wildcard cases/*))
FLAGS := $(if $(V),-v)

.PHONY: all clean $(TESTS)

all:
	./run_tests.py $(FLAGS)

$(TESTS):
	./run_tests.py $(FLAGS) $@

clean:
	rm -rf outputs
