#!/usr/bin/env bash
# Each stage runs independently; a failure in one does not suppress another.
set -euo pipefail
[ "$#" -eq 2 ] || { echo "usage: test_unix_stages.sh 4|6|7 KERNEL_PATH" >&2; exit 2; }
STAGE=$1 KERNEL=$(realpath "$2")
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
CC=${CC:-gcc}
BASE=(-std=c11 -O1 -g -Wall -Wextra -Werror -pedantic
      -fsanitize=undefined -fno-omit-frame-pointer
      -I"$ROOT/ports/unix" -I"$ROOT/ports/busybox")
case "$STAGE" in
  4)
    "$CC" "${BASE[@]}" "$ROOT/tests/host/test_uxfs.c" \
      "$ROOT/ports/unix/pvd.c" "$ROOT/ports/unix/uxfs.c" -o "$OUT/uxfs"
    "$OUT/uxfs"
    ;;
  6)
    "$CC" "${BASE[@]}" -I"$KERNEL/runtime/abi" \
      "$ROOT/tests/host/test_uxspawn.c" "$ROOT/ports/unix/uxspawn.c" \
      -o "$OUT/uxspawn"
    "$OUT/uxspawn"
    "$CC" -std=gnu11 -m64 -O2 -ffreestanding -nostdinc -fno-builtin \
      -Wall -Wextra -Werror -I"$KERNEL/runtime/abi" \
      -I"$ROOT/ports/unix" -c "$ROOT/ports/unix/uxspawn.c" -o "$OUT/native-spawn.o"
    "$CC" "${BASE[@]}" -I"$KERNEL/runtime/abi" \
      "$ROOT/tests/host/test_ls_child.c" "$ROOT/ports/busybox/ls_child.c" \
      -o "$OUT/native-ls"
    "$OUT/native-ls"
    "$CC" -std=gnu11 -m64 -O2 -ffreestanding -nostdinc -fno-builtin \
      -Wall -Wextra -Werror -I"$KERNEL/runtime/abi" \
      -I"$ROOT/ports/busybox" \
      -c "$ROOT/ports/busybox/ls_child.c" -o "$OUT/native-ls.o"
    echo "PASS: native-target freestanding SPAWN and external ls translation units compile"
    ;;
  7)
    "$CC" "${BASE[@]}" "$ROOT/tests/host/test_uxshell.c" \
      "$ROOT/ports/unix/pvd.c" "$ROOT/ports/unix/uxfs.c" \
      "$ROOT/ports/unix/uxshell.c" -o "$OUT/uxshell"
    "$OUT/uxshell"
    ;;
  *) echo "Unknown stage $STAGE" >&2; exit 2;;
esac
