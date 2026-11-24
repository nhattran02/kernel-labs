#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# build-sparse.sh - build sparse >= 0.6.4 for "make check".
#
# Linux v6.12 headers use C11 _Generic in ways sparse 0.6.1 (Ubuntu 20.04)
# cannot parse: every file then reports ~100 errors in seqlock.h and the
# real warnings drown.  This builds sparse v0.6.4 into $LFD_TOOLS/bin; the
# lab Makefiles use it automatically when it is there.
#
#   env/scripts/build-sparse.sh
set -euo pipefail
. "$(dirname "$0")/common.sh"

ver=v0.6.4
src=$LFD_IMAGES/src/sparse-$ver
if [ ! -d "$src" ]; then
	mkdir -p "$LFD_IMAGES/src"
	git clone --depth 1 --branch "$ver" \
		https://git.kernel.org/pub/scm/devel/sparse/sparse.git "$src"
fi
make -C "$src" -j"$(nproc)" sparse
mkdir -p "$LFD_TOOLS/bin"
install -m 0755 "$src/sparse" "$LFD_TOOLS/bin/sparse"
info "$("$LFD_TOOLS/bin/sparse" --version) installed in $LFD_TOOLS/bin"
