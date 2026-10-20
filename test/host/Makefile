# SC-13: make test   (host unit tests; needs g++, python3, mbedtls headers+lib)
CXX      ?= g++
SRC      := ../../common/src
GEN      := gen/vectors.h
CXXFLAGS := -std=c++11 -O1 -g -Wall -Wextra -Wpedantic -Werror -DPPE_HOST_TEST -DPPE_TEST_HOOKS -I$(SRC) -I. $(EXTRA_CXXFLAGS)
MBEDLIBS ?= -lmbedcrypto
LIBSRC   := $(SRC)/ppe_proto.cpp $(SRC)/ppe_crypto.cpp $(SRC)/ppe_filter.cpp $(SRC)/ppe_counter.cpp $(SRC)/ppe_log.cpp \
            $(SRC)/ppe_sense.cpp $(SRC)/ppe_settle.cpp $(SRC)/ppe_confirm.cpp $(SRC)/ppe_config.cpp $(SRC)/ppe_rtcctx.cpp $(SRC)/ppe_hooks.cpp
TESTSRC  := ppe_test_main.cpp sha256_portable.cpp test_proto.cpp test_filter.cpp test_counter.cpp test_log.cpp \
            test_sense.cpp test_confirm.cpp test_rtcctx.cpp test_hooks.cpp
BIN      := build/ppe_host_tests

.PHONY: test list clean release_check
test: $(BIN)
	./$(BIN)
list: $(BIN)
	./$(BIN) --list
$(GEN): ../../tools/gen_vectors.py
	python3 ../../tools/gen_vectors.py $(GEN)
$(BIN): $(GEN) $(LIBSRC) $(TESTSRC) $(wildcard $(SRC)/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(LIBSRC) $(TESTSRC) $(MBEDLIBS) -o $(BIN)
# TC-78: a build without -DPPE_TEST_HOOKS must contain no hook code.
release_check:
	@mkdir -p build
	$(CXX) -std=c++11 -Wall -Wextra -Wpedantic -Werror -I$(SRC) -c $(SRC)/ppe_hooks.cpp -o build/ppe_hooks_release.o
	@if nm build/ppe_hooks_release.o | grep -q -E "ppe_hook_parse|ppe_mark_fmt|ppe_cfg_fmt"; then echo "RELEASE CHECK FAILED: hook symbols present"; exit 1; else echo "release_check: no hook symbols"; fi
clean:
	rm -rf build gen
