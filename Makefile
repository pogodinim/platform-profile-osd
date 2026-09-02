CC ?= cc
PKG_CONFIG ?= pkg-config

VERSION := 0.1.0
CPPFLAGS += -D_GNU_SOURCE -DPPO_VERSION=\"$(VERSION)\" $(shell $(PKG_CONFIG) --cflags libsystemd)
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDLIBS += $(shell $(PKG_CONFIG) --libs libsystemd)

BUILD_DIR := build
TARGET := $(BUILD_DIR)/platform-profile-osd
SOURCES := src/main.c src/config.c src/profile.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)
TEST_TARGET := $(BUILD_DIR)/test-unit

.PHONY: all clean test check

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(TEST_TARGET): tests/test-unit.c src/config.c src/profile.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc tests/test-unit.c src/config.c src/profile.c -o $@

test: $(TARGET) $(TEST_TARGET)
	$(TEST_TARGET)
	sh tests/test-cli.sh $(TARGET)

check: test

clean:
	rm -f $(OBJECTS) $(TARGET) $(TEST_TARGET)
