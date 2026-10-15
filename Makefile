# F-51A CVE-2023-33107 reproduction — build one Android arm64 binary.
#
# Everything the reproduction needs lives in that single executable:
#   ./out/cve-2023-33107-f51a                 interactive root shell (--apply)
#   ./out/cve-2023-33107-f51a --self-test     read-only physical-window check
#   ./out/cve-2023-33107-f51a --probe-only    read-only selinux_state search
#   ./out/cve-2023-33107-f51a --mempressure N auxiliary memory pressure
#
# Use build.sh / run.sh for the normal flow; the targets here are what they call.

DEVICE  ?= f51a
ARTIFACT := out/cve-2023-33107-$(DEVICE)

ANDROID_NDK_HOME ?=
API              ?= 31
HOST_TAG         := $(shell uname -s | tr 'A-Z' 'a-z')-x86_64
NDK_BIN          := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/$(HOST_TAG)/bin
CC               := $(NDK_BIN)/aarch64-linux-android$(API)-clang

SRCS   := source/src/ex.c source/src/mempressure.c
HDRS   := source/src/mempressure.h
# The validated binary was built unoptimised with debug info. The exploit is a
# race whose timing and memory-ordering behaviour the device run confirmed at
# -O0, so the optimisation level is pinned rather than raised: a -O0 -g build of
# this tree reproduces the validated .text byte count plus exactly the added
# --mempressure code.
CFLAGS := -O0 -g -std=gnu11 -Wall -Isource/src
LDFLAGS :=

.PHONY: all check-ndk clean info sha256

all: $(ARTIFACT)

check-ndk:
ifeq ($(strip $(ANDROID_NDK_HOME)),)
	@echo 'Set ANDROID_NDK_HOME to Android NDK r26d or newer.' >&2; false
endif
	@test -x '$(CC)' || { \
	  echo 'Missing compiler: $(CC)' >&2; \
	  echo 'ANDROID_NDK_HOME does not look like an NDK r26d+ install.' >&2; \
	  false; }

$(ARTIFACT): $(SRCS) $(HDRS) Makefile | check-ndk
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)
	@echo '-> $@'

sha256: $(ARTIFACT)
	@sha256sum $(ARTIFACT)

info:
	@echo 'DEVICE   = $(DEVICE)'
	@echo 'ARTIFACT = $(ARTIFACT)'
	@echo 'CC       = $(CC)'
	@echo 'SRCS     = $(SRCS)'

clean:
	rm -rf out
