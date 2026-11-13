#!/usr/bin/env bash
# Build and execute the exact freestanding PVD implementation on the host.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
CC=${CC:-gcc-14}
BASE=(-std=c11 -Wall -Wextra -Werror -pedantic
      -fsanitize=address,undefined -fno-omit-frame-pointer
      -I"$ROOT/ports/unix" -I"$ROOT/ports/busybox")
"$CC" "${BASE[@]}" "$ROOT/tests/host/test_pvd.c" \
    "$ROOT/ports/unix/pvd.c" -o "$OUT/test-pvd"
"$CC" "${BASE[@]}" "$ROOT/tests/host/test_pvd_cli.c" \
    "$ROOT/ports/unix/pvd.c" "$ROOT/ports/busybox/pvd_cli.c" \
    -o "$OUT/test-pvd-cli"
"$OUT/test-pvd"
"$OUT/test-pvd-cli"
