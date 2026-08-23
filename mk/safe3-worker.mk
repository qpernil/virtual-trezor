SHELL := /usr/bin/env bash

PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
SAFE3_BUILD_DIR := $(PROJECT_ROOT)/build/safe3-t3b1
SAFE3_UNIX_DIR := $(SAFE3_BUILD_DIR)/unix
SAFE3_BASELINE := $(SAFE3_UNIX_DIR)/trezor-emu-core
SAFE3_DISPLAY_BUILD_DIR := $(PROJECT_ROOT)/build/safe3-t3b1-display
SAFE3_DISPLAY := $(SAFE3_DISPLAY_BUILD_DIR)/virtual-trezor-safe3-display
SAFE3_INPUT_BUILD_DIR := $(PROJECT_ROOT)/build/safe3-t3b1-input
SAFE3_INPUT := $(SAFE3_INPUT_BUILD_DIR)/virtual-trezor-safe3-input

.DEFAULT_GOAL := baseline

.PHONY: baseline clean display input

# This first target proves the genuine pinned T3B1 Core source graph and keeps
# its complete generated/object tree separate from Trezor One. It is still the
# upstream SDL/UDP Unix emulator; later targets in this makefile replace its
# platform sources while keeping the pinned submodule clean after each build.
baseline:
	TREZOR_MODEL=T3B1 SAFE3_BUILD_DIR="$(SAFE3_BUILD_DIR)" \
		"$(PROJECT_ROOT)/scripts/build-safe3-baseline.sh"
	@test -x "$(SAFE3_BASELINE)"

# First genuine platform substitution: no SDL is linked. Core's native Mono8
# framebuffer goes to display-backends; input remains intentionally inert and
# UDP USB/timing/lifecycle remain upstream until their own stages.
display:
	SAFE3_DISPLAY_BUILD_DIR="$(SAFE3_DISPLAY_BUILD_DIR)" \
		"$(PROJECT_ROOT)/scripts/build-safe3-display.sh"
	@test -x "$(SAFE3_DISPLAY)"

# Second platform substitution: retain the SDL-free Mono8 display boundary and
# replace the inert shim with Core's genuine poller reading Pi GPIO resources.
# USB/timing/lifecycle remain upstream until their own stages.
input:
	SAFE3_DISPLAY_BUILD_DIR="$(SAFE3_INPUT_BUILD_DIR)" \
		"$(PROJECT_ROOT)/scripts/build-safe3-input.sh"
	@test -x "$(SAFE3_INPUT)"

clean:
	@if [[ -L "$(PROJECT_ROOT)/upstream/trezor-firmware/core/build/unix" ]]; then \
		rm "$(PROJECT_ROOT)/upstream/trezor-firmware/core/build/unix"; \
	fi
	rm -rf "$(SAFE3_BUILD_DIR)"
	rm -rf "$(SAFE3_DISPLAY_BUILD_DIR)"
	rm -rf "$(SAFE3_INPUT_BUILD_DIR)"
