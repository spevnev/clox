SRC_DIR := src
OUT_DIR := build

BIN_NAME := clox
BIN_PATH := $(OUT_DIR)/$(BIN_NAME)

CFLAGS := -std=c17 -Wall -Wextra -pedantic -MMD -MP -O2

ifeq ($(DEBUG), 1)
	CFLAGS += -g3 -fsanitize=address,leak,undefined
endif

ifeq ($(TEST), 1)
	CFLAGS += -fsanitize=address,leak,undefined -D HIDE_STACKTRACE -D DEBUG_STRESS_GC
endif

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst %.c, $(OUT_DIR)/%.o, $(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean
all: $(BIN_PATH)

clean:
	rm -rf $(OUT_DIR)

$(BIN_PATH): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(OUT_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

-include $(DEPS)
