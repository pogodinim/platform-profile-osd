CC ?= cc
PKG_CONFIG ?= pkg-config

VERSION := 0.1.0-preview.1
CPPFLAGS += -D_GNU_SOURCE -DPPO_VERSION=\"$(VERSION)\" $(shell $(PKG_CONFIG) --cflags libsystemd)
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDLIBS += $(shell $(PKG_CONFIG) --libs libsystemd)

BUILD_DIR := build
TARGET := $(BUILD_DIR)/platform-profile-osd
SOURCES := src/main.c src/config.c src/profile.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)
TEST_TARGET := $(BUILD_DIR)/test-unit
RECOVERY_TARGET := $(BUILD_DIR)/test-notification-recovery
AUDIO_TEST_TARGET := $(BUILD_DIR)/test-audio-timeout
TEST_BASH_SCRIPTS := install.sh uninstall.sh \
	tests/test-install.sh tests/test-service.sh tests/test-uninstall.sh
TEST_SH_SCRIPTS := tests/test-cli.sh tests/test-systemd-unit.sh tests/test-audio-timeout.sh
TEST_DESKTOP_FILES := autostart/platform-profile-osd.desktop
TEST_SYSTEMD_UNITS := systemd/platform-profile-osd.service

.PHONY: all clean test test-recovery check

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(TEST_TARGET): tests/test-unit.c src/config.c src/profile.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc tests/test-unit.c src/config.c src/profile.c -o $@

$(RECOVERY_TARGET): tests/test-notification-recovery.c src/main.c src/config.c src/profile.c src/config.h src/profile.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc tests/test-notification-recovery.c src/config.c src/profile.c $(LDLIBS) -o $@

test-recovery: $(RECOVERY_TARGET)
	python3 tests/test-notification-recovery.py $(RECOVERY_TARGET)

$(AUDIO_TEST_TARGET): tests/test-audio-timeout.c src/main.c src/config.c src/profile.c src/config.h src/profile.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -Isrc tests/test-audio-timeout.c src/config.c src/profile.c $(LDLIBS) -o $@

test: $(TARGET) $(TEST_TARGET) $(RECOVERY_TARGET) $(AUDIO_TEST_TARGET)
	$(TEST_TARGET)
	sh tests/test-cli.sh $(TARGET)
	bash tests/test-uninstall.sh
	sh tests/test-audio-timeout.sh $(AUDIO_TEST_TARGET)
	python3 tests/test-notification-recovery.py $(RECOVERY_TARGET)
	@for script in $(TEST_BASH_SCRIPTS); do \
		printf 'Checking Bash syntax: %s\n' "$$script"; \
		bash -n "$$script" || exit $$?; \
	done
	@for script in $(TEST_SH_SCRIPTS); do \
		printf 'Checking POSIX shell syntax: %s\n' "$$script"; \
		sh -n "$$script" || exit $$?; \
	done
	desktop-file-validate $(TEST_DESKTOP_FILES)
	sh tests/test-systemd-unit.sh $(TARGET) $(TEST_SYSTEMD_UNITS)

check: test

clean:
	rm -f $(OBJECTS) $(TARGET) $(TEST_TARGET) $(RECOVERY_TARGET) $(AUDIO_TEST_TARGET)
