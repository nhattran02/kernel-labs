# SPDX-License-Identifier: GPL-2.0
# Helpers shared by the env scripts.  Source, do not execute.

LFD_REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

# Load env/lab.env: "NAME ?= value" sets NAME only if it is not set yet.
while IFS= read -r line; do
	case "$line" in ''|'#'*) continue ;; esac
	name=${line%%\?=*}; name=${name//[[:space:]]/}
	value=${line#*\?=}; value=${value#"${value%%[![:space:]]*}"}
	value=${value//\$(HOME)/$HOME}
	[ -z "${!name+x}" ] && export "$name=$value"
done < "$LFD_REPO/env/lab.env"

die()  { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }

# Map a target name to kernel ARCH and CROSS_COMPILE.
lfd_target() {
	case "$1" in
	x86_64) LFD_ARCH=x86;   LFD_CROSS= ;;
	arm64)  LFD_ARCH=arm64; LFD_CROSS=$LFD_CROSS_arm64 ;;
	bbb)    LFD_ARCH=arm;   LFD_CROSS=$LFD_CROSS_bbb ;;
	*) die "unknown target '$1' (use x86_64, arm64 or bbb)" ;;
	esac
	LFD_KOUT=$LFD_KBUILD/$1
}
