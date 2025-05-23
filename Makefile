CC := gcc

CFLAGS  := -O2 -std=c17 -Wall -Wextra -pedantic -Isrc -MMD -MP
LDFLAGS :=

ifeq ($(DEBUG), 1)
	CFLAGS += -g3 -fsanitize=address,leak,undefined
endif

ifeq ($(TEST), 1)
	CFLAGS += -fsanitize=address,leak,undefined -D HIDE_STACKTRACE -D DEBUG_STRESS_GC
endif

SRC_DIR := src
OUT_DIR := build
OBJ_DIR := $(OUT_DIR)/$(SRC_DIR)

BIN_NAME := clox
BIN_PATH := $(OUT_DIR)/$(BIN_NAME)

SRCS := $(shell find $(SRC_DIR) -type f -name '*.c')
OBJS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
DEPS := $(patsubst %.o, %.d, $(OBJS))

.PHONY: all
all: build

.PHONY: build
build: $(BIN_PATH)

.PHONY: clean
clean:
	rm -rf $(OUT_DIR)

$(BIN_PATH): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ -c $<

-include $(DEPS)
