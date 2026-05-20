CC := gcc
CFLAGS := -std=gnu11 -Wall -Wextra -Werror -Iinclude
LDFLAGS :=
BUILD_DIR := build
BIN_DIR := bin
TARGET := $(BIN_DIR)/tarsau
SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))
.PHONY: all clean run-b run-a test
all: $(TARGET)
$(TARGET): $(OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)
$(BUILD_DIR)/%.o: src/%.c include/tarsau.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR) $(BIN_DIR):
	mkdir -p $@
run-b: all
	$(TARGET) -b tests/sample/t1 tests/sample/t2 tests/sample/t3 tests/sample/t4.txt tests/sample/t5.dat -o s1.sau
run-a: all
	rm -rf d1
	$(TARGET) -a s1.sau d1
test: all
	sh tests/run_smoke.sh
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) d1 s1.sau a.sau tests/out