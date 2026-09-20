# VCV Library release build: ACID9 Voice only. No Faust installation required.
export COPYFILE_DISABLE=1
RACK_DIR ?= Rack-SDK
SOURCES += src/plugin.cpp src/modules/ACID9Voice/ACID9Voice.cpp
FLAGS += -Isrc/common -Isrc/modules/ACID9Voice -DHAS_ACID9VOICE=1
DISTRIBUTABLES += res/ACID9Voice.png res/ACID9Voice-labels.svg LICENSE docs/user/modules/ACID9Voice.md presets examples design/ACID9Voice/preview.png
include $(RACK_DIR)/plugin.mk
# Preserve finite-value checks even when the SDK enables fast math.
FLAGS += -fno-finite-math-only
CXXFLAGS += -std=c++17
.PHONY: dep
dep:
