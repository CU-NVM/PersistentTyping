# root-setup tests for persist-tool. Each case is a directory in cases/ with a
# CASE file of expectations; run_tests.py documents the directives.
#
#   make                 run every case
#   make single_global   run one case (any directory in cases/)
#   make clean           remove outputs/
#
# Pass V=1 to print the output of failing cases, E2E=1 to also run the
# recurse-persistent pass before compiling.

TESTS := $(notdir $(wildcard cases/*))
FLAGS := $(if $(V),-v) $(if $(E2E),--e2e)

.PHONY: all clean $(TESTS)

all:
	./run_tests.py $(FLAGS)

$(TESTS):
	./run_tests.py $(FLAGS) $@

clean:
	rm -rf outputs
