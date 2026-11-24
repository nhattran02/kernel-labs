# SPDX-License-Identifier: GPL-2.0
# QEMU: an RTL8139 on QEMU's user-mode network ("slirp"); its built-in TFTP
# server serves test/tftp/ at 10.0.2.2.  The lab kernel has no 8139too.
QEMU_OPTS = --net rtl8139 --tftp $(CURDIR)/test/tftp
include ../../common/Makefile.module

all: test/tftp/blob.bin
test/tftp/blob.bin:
	mkdir -p test/tftp
	head -c 1048576 /dev/urandom > $@
	cd test/tftp && md5sum blob.bin > blob.md5
clean: clean-blob
clean-blob:
	rm -rf test/tftp
.PHONY: clean-blob
