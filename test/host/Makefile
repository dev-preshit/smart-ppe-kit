# SC-13: make test   (host unit tests; needs g++, python3, mbedtls headers+lib)
CXX      ?= g++
SRC      := ../../common/src
GEN      := gen/vectors.h
CXXFLAGS := -std=c++11 -O1 -g -Wall -Wextra -Wpedantic -Werror -DPPE_HOST_TEST -I$(SRC) -I. $(EXTRA_CXXFLAGS)
MBEDLIBS ?= -lmbedcrypto
LIBSRC   := $(SRC)/ppe_proto.cpp $(SRC)/ppe_crypto.cpp $(SRC)/ppe_filter.cpp $(SRC)/ppe_counter.cpp $(SRC)/ppe_log.cpp
TESTSRC  := ppe_test_main.cpp sha256_portable.cpp test_proto.cpp test_filter.cpp test_counter.cpp test_log.cpp
BIN      := build/ppe_host_tests

.PHONY: test list clean
test: $(BIN)
	./$(BIN)
list: $(BIN)
	./$(BIN) --list
$(GEN): ../../tools/gen_vectors.py
	python3 ../../tools/gen_vectors.py $(GEN)
$(BIN): $(GEN) $(LIBSRC) $(TESTSRC) $(wildcard $(SRC)/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(LIBSRC) $(TESTSRC) $(MBEDLIBS) -o $(BIN)
clean:
	rm -rf build gen
