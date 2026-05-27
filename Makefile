.RECIPEPREFIX := >

CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -O3 -fPIC -std=c++17 -Wall -Wextra -fno-exceptions -fno-rtti -fno-stack-protector -fvisibility=hidden -Iinclude
LDFLAGS ?= -shared -Wl,--as-needed
LDLIBS ?= -lm

PLUGIN_BUNDLE := TT6.lv2
PLUGIN_SO := $(PLUGIN_BUNDLE)/TT6.so
BUILD_DIR := build
OBJ := $(BUILD_DIR)/TT6.o
SRC := src/TT6.cpp

all: $(PLUGIN_SO)

$(BUILD_DIR):
>mkdir -p $@

$(PLUGIN_BUNDLE):
>mkdir -p $@

$(OBJ): $(SRC) | $(BUILD_DIR)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(PLUGIN_SO): $(OBJ) | $(PLUGIN_BUNDLE)
>$(CC) $< -o $@ $(LDFLAGS) $(LDLIBS)

arm64:
>$(MAKE) CXX=g++ CC=gcc CXXFLAGS="$(CXXFLAGS) -march=armv8-a" all

check: all
>file $(PLUGIN_SO)
>nm -D $(PLUGIN_SO) | grep lv2_descriptor || true
>readelf -d $(PLUGIN_SO) | grep NEEDED || true
>strings -a $(PLUGIN_SO) | grep -E 'GLIBC_|GLIBCXX_|GCC_' | sort -V | uniq || true
>if command -v lv2_validate >/dev/null 2>&1 && command -v sord_validate >/dev/null 2>&1; then lv2_validate $(PLUGIN_BUNDLE)/manifest.ttl $(PLUGIN_BUNDLE)/TT6.ttl; else echo "lv2_validate or sord_validate not installed; skipping"; fi
>if command -v lv2lint >/dev/null 2>&1; then LV2_PATH=. lv2lint -Mpack "urn:asier:lv2:tt6"; else echo "lv2lint not installed; skipping"; fi

clean:
>rm -rf $(BUILD_DIR) $(PLUGIN_SO)

.PHONY: all arm64 check clean
