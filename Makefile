# WiggleRoom - VCV Rack Plugin
# Compatibility Makefile wrapping CMake build for VCV Library CI.
#
# The VCV Library build system expects a standard Makefile with specific
# targets (dep, all, dist, install, clean). This project uses CMake for
# its build system, so this Makefile delegates to CMake while producing
# output in the format the Library CI expects.
#
# Usage:
#   export RACK_DIR=/path/to/Rack-SDK
#   make dep      # (no-op -- CMake handles dependencies)
#   make          # Build the plugin
#   make dist     # Create distributable .vcvplugin package
#   make install  # Install to local Rack plugins folder
#   make clean    # Remove build artifacts

# RACK_DIR must be set by the VCV Library CI (or the developer).
ifndef RACK_DIR
$(error RACK_DIR is not defined. Set it to the path of the Rack SDK)
endif

# Read metadata from plugin.json (same approach as plugin.mk)
SLUG := $(shell jq -r .slug plugin.json)
VERSION := $(shell jq -r .version plugin.json)

BUILD_DIR := build

# Platform detection
MACHINE := $(shell uname -m)
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  PLUGIN_EXT := dylib
  ifeq ($(MACHINE),arm64)
    ARCH_TAG := mac-arm64
  else
    ARCH_TAG := mac-x64
  endif
else ifeq ($(UNAME_S),Linux)
  PLUGIN_EXT := so
  ARCH_TAG := lin-x64
else
  # Windows (MSYS2/MinGW)
  PLUGIN_EXT := dll
  ARCH_TAG := win-x64
endif

# Detect parallelism
JOBS := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# The compiled plugin binary
PLUGIN_BIN := $(BUILD_DIR)/plugin.$(PLUGIN_EXT)

# Files to include in the distribution (mirrors DISTRIBUTABLES in plugin.mk)
DISTRIBUTABLES := plugin.json res $(wildcard LICENSE*)

.PHONY: all dep build install dist clean cleandist

all: build

# dep target -- required by VCV Library CI.
# CMake handles dependency resolution (including downloading the SDK if needed),
# so this is a no-op.
dep:

build:
	cmake -B $(BUILD_DIR) -S . \
		-DCMAKE_BUILD_TYPE=Release \
		-DRACK_DIR=$(RACK_DIR) \
		-DRELEASE_MODULE_LIST=""
	cmake --build $(BUILD_DIR) -j $(JOBS)

install: dist
	mkdir -p $(RACK_DIR)/../plugins-$(ARCH_TAG)/
	cp dist/$(SLUG)-$(VERSION)-$(ARCH_TAG).vcvplugin $(RACK_DIR)/../plugins-$(ARCH_TAG)/

dist: build
	rm -rf dist/$(SLUG)
	mkdir -p dist/$(SLUG)
	cp $(PLUGIN_BIN) dist/$(SLUG)/
	$(STRIP) -S dist/$(SLUG)/plugin.$(PLUGIN_EXT) 2>/dev/null || true
	$(foreach f,$(DISTRIBUTABLES),cp -r $(f) dist/$(SLUG)/$(f);)
	cd dist && tar -c $(SLUG) | zstd -19 -o $(SLUG)-$(VERSION)-$(ARCH_TAG).vcvplugin

clean:
	rm -rf $(BUILD_DIR) dist

cleandist:
	rm -rf dist

# Use the platform strip command (STRIP may be overridden by cross-compilation toolchains)
STRIP ?= strip
