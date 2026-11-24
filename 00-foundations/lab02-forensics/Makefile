# SPDX-License-Identifier: GPL-2.0
# KASAN normally prints only its first report per boot; this lab needs all.
QEMU_OPTS = --append kasan_multi_shot
include ../../common/Makefile.module
