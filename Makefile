# SPDX-License-Identifier: GPL-2.0
#
# LFD430 labs - top level.  Kernel work only; the lab book PDF is built
# from the repository root (make labs).
#
#   make                build every lab (modules + test programs)
#   make test           build and run every acceptance test in QEMU
#   make test LABS="00-foundations/lab01-modules 07-network/lab20-lfdnet"
#   make check          checkpatch + sparse on every lab
#   make clean          clean every lab and the test logs
#
#   make env            x86_64 lab kernel, BusyBox, initramfs (Lab 00, T3-T4)
#   make env-arm64      the same for arm64 (Lab 15)
#   make env-bbb        BeagleBone Black kernel, BusyBox, NFS root (Lab 00, T7)
#   make sparse         sparse 0.6.4 for make check (Ubuntu 20.04)
#
# A single lab: cd into its directory and use make, make qemu,
# make qemu-test, make check (see common/Makefile.module).

ALL_LABS := $(patsubst %/,%,$(sort $(dir $(wildcard [0-9]*/lab*/Makefile))))
LABS     ?= $(ALL_LABS)
S        := env/scripts

all: build

build:
	@set -e; for l in $(LABS); do $(MAKE) --no-print-directory -C $$l; done

test:
	@tools/run-all-tests.sh $(LABS)

check:
	@for l in $(LABS); do echo "== $$l"; $(MAKE) --no-print-directory -C $$l check; done

clean:
	@for l in $(ALL_LABS); do $(MAKE) --no-print-directory -C $$l clean >/dev/null; done
	rm -rf .test-logs

env:
	$(S)/build-kernel.sh x86_64
	$(S)/build-busybox.sh x86_64
	$(S)/mk-initramfs.sh x86_64

env-arm64:
	$(S)/build-kernel.sh arm64
	$(S)/build-busybox.sh arm64
	$(S)/mk-initramfs.sh arm64

env-bbb:
	$(S)/build-kernel.sh bbb
	$(S)/build-busybox.sh bbb
	$(S)/mk-bbb-rootfs.sh

sparse:
	$(S)/build-sparse.sh

list:
	@printf '%s\n' $(ALL_LABS)

.PHONY: all build test check clean env env-arm64 env-bbb sparse list
