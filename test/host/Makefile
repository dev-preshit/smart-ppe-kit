# SC-13: make test   (host unit tests; needs g++, python3, mbedtls headers+lib)
CXX      ?= g++
SRC      := ../../common/src
GEN      := gen/vectors.h
HUB      := ../../firmware/hub/src
CXXFLAGS := -std=c++11 -O1 -g -Wall -Wextra -Wpedantic -Werror -DPPE_HOST_TEST -DPPE_TEST_HOOKS -I$(SRC) -I$(HUB) -I. $(EXTRA_CXXFLAGS)
MBEDLIBS ?= -lmbedcrypto
LIBSRC   := $(SRC)/ppe_proto.cpp $(SRC)/ppe_crypto.cpp $(SRC)/ppe_filter.cpp $(SRC)/ppe_counter.cpp $(SRC)/ppe_log.cpp
# S3 hub pure logic (no Arduino includes), see firmware/hub/README.md
HUBLIB   := $(HUB)/hub_pipeline.cpp $(HUB)/hub_persist.cpp $(HUB)/hub_scan_supervisor.cpp $(HUB)/hub_hooks.cpp $(HUB)/hub_dump.cpp
TESTSRC  := ppe_test_main.cpp sha256_portable.cpp test_proto.cpp test_filter.cpp test_counter.cpp test_log.cpp test_hub_pipeline.cpp test_hub_persist.cpp test_hub_scan.cpp test_hub_hooks.cpp
BIN      := build/ppe_host_tests

.PHONY: test list clean release_check
test: $(BIN) release_check
	./$(BIN)
# The release build (no -DPPE_TEST_HOOKS) must contain no hook code or hook strings (brief story 5, TC-78 idea).
release_check: $(wildcard $(HUB)/*.cpp) $(wildcard $(HUB)/*.h)
	@mkdir -p build
	$(CXX) -std=c++11 -Wall -Wextra -Werror -I$(SRC) -I$(HUB) -c $(HUB)/hub_hooks.cpp -o build/rel_hooks.o
	$(CXX) -std=c++11 -Wall -Wextra -Werror -I$(SRC) -I$(HUB) -c $(HUB)/hub_dump.cpp -o build/rel_dump.o
	@if strings build/rel_hooks.o build/rel_dump.o | grep -E "rxlog|PPE_HOOK|DUMP|inject|stall" ; then echo "release_check FAIL: hook strings in the release objects"; exit 1; else echo "release_check PASS: no hook strings in the release objects"; fi
list: $(BIN)
	./$(BIN) --list
$(GEN): ../../tools/gen_vectors.py
	python3 ../../tools/gen_vectors.py $(GEN)
$(BIN): $(GEN) $(LIBSRC) $(HUBLIB) $(TESTSRC) $(wildcard $(SRC)/*.h) $(wildcard $(HUB)/*.h) $(wildcard *.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(LIBSRC) $(HUBLIB) $(TESTSRC) $(MBEDLIBS) -o $(BIN)
clean:
	rm -rf build gen
