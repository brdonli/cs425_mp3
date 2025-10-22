# Makefile for MP3 - Alternative to CMake for VMs without cmake in PATH
# This creates a build/ directory and compiles everything

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -O3 -DNDEBUG -Iinclude -Ilibs/catch2
LDFLAGS = -pthread

# Directories
BUILD_DIR = build
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = tests

# Source files
CORE_SRCS = $(SRC_DIR)/socket.cpp \
            $(SRC_DIR)/logger.cpp \
            $(SRC_DIR)/message.cpp \
            $(SRC_DIR)/membership_list.cpp \
            $(SRC_DIR)/node.cpp \
            $(SRC_DIR)/shared.cpp \
            $(SRC_DIR)/file_block.cpp \
            $(SRC_DIR)/file_metadata.cpp \
            $(SRC_DIR)/consistent_hash_ring.cpp \
            $(SRC_DIR)/file_store.cpp \
            $(SRC_DIR)/client_tracker.cpp

CORE_OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(CORE_SRCS))

MAIN_SRC = $(SRC_DIR)/main.cpp
MAIN_OBJ = $(BUILD_DIR)/main.o

TEST_SRCS = $(TEST_DIR)/test_main.cpp \
            $(TEST_DIR)/test_message.cpp

CATCH_SRC = libs/catch2/catch_amalgamated.cpp

TEST_OBJS = $(patsubst $(TEST_DIR)/%.cpp,$(BUILD_DIR)/tests/%.o,$(TEST_SRCS)) \
            $(BUILD_DIR)/libs/catch2/catch_amalgamated.o

# Targets
MAIN_TARGET = $(BUILD_DIR)/main
TEST_TARGET = $(BUILD_DIR)/test_runner

.PHONY: all clean

all: $(MAIN_TARGET) $(TEST_TARGET)

$(MAIN_TARGET): $(CORE_OBJS) $(MAIN_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_TARGET): $(CORE_OBJS) $(TEST_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/libs/%.o: libs/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

# Help target
help:
	@echo "MP3 Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make all          - Build everything (default)"
	@echo "  make clean        - Remove build directory"
	@echo "  make help         - Show this help"
	@echo ""
	@echo "Output:"
	@echo "  build/main        - Main executable"
	@echo "  build/test_runner - Test executable"
