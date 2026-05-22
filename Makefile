# RunCam Thumb ESP32-C3 Controller — Host Test Runner

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -DUNIT_TEST -Isrc -Itests -Itests/stubs -Itests/unity
LDFLAGS  :=

SRC_DIRS := src/protocol src/camera src/storage src/flight
SRCS     := $(wildcard $(addsuffix /*.cpp,$(SRC_DIRS)))

BUILD_DIR := build

# Discover test files
TESTS ?= $(wildcard tests/test_*.cpp)
TEST_BINS := $(patsubst tests/test_%.cpp,$(BUILD_DIR)/run_test_%,$(TESTS))

.PHONY: test clean all

all: test

test: $(TEST_BINS)
	@echo ""
	@echo "=== Running all tests ==="
	@failed=0; \
	for bin in $(TEST_BINS); do \
		echo "--- $$bin ---"; \
		./$$bin || failed=$$((failed + 1)); \
	done; \
	echo ""; \
	if [ $$failed -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "$$failed TEST(S) FAILED"; exit 1; fi

$(BUILD_DIR)/run_test_%: tests/test_%.cpp $(SRCS) tests/mocks/mock_transport.cpp tests/unity/unity.c | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(SRCS) tests/mocks/mock_transport.cpp tests/unity/unity.c $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
