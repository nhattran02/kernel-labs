#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# hmp.sh [x86_64|arm64] "COMMAND" - send one command to the QEMU monitor of
# a running lab guest and print the answer.  Example:
#   env/scripts/hmp.sh "mouse_move 20 0"
set -euo pipefail
. "$(dirname "$0")/common.sh"
target=x86_64
case "${1:-}" in x86_64|arm64) target=$1; shift ;; esac
sock=$LFD_IMAGES/$target/monitor.sock
[ -S "$sock" ] || die "no monitor socket $sock (is QEMU running?)"
python3 - "$sock" "$*" <<'PY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX)
s.connect(sys.argv[1])
s.settimeout(1.0)
def drain():
    out = b''
    try:
        while True:
            d = s.recv(65536)
            if not d:
                break
            out += d
    except socket.timeout:
        pass
    return out
drain()
s.sendall(sys.argv[2].encode() + b'\n')
time.sleep(0.2)
out = drain().decode(errors='replace')
print('\n'.join(l for l in out.splitlines() if not l.startswith('(qemu)')))
PY
