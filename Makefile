# SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

# Explicit default goal
.DEFAULT_GOAL := all

# Build output directory
BUILD ?= build


# Build all targets
.PHONY: all
all: $(BUILD)/Makefile
	@$(MAKE) -C $(BUILD)

# Wipe build directory
.PHONY: distclean
distclean:
	@rm -rf $(BUILD)

# Build and run tests
.PHONY: test
test: # Test preset to run
test: #
test: # - all
test: # - default (no hardware)
test: # - hardware
test: PRESET ?= default
test:
test: tests
	@ctest --preset $(PRESET) --output-on-failure

# Forward to cmake
%: $(BUILD)/Makefile
	@$(MAKE) -C $(BUILD) $@

# Configure build
$(BUILD)/Makefile:
	@cmake -B $(BUILD)
