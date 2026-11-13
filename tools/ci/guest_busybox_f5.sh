#!/usr/bin/env bash
# Real QEMU VGA + PS/2 acceptance for BusyBox on manifest console slot 4/F5.
set -euo pipefail
if [ "$#" -ne 3 ]; then
  echo "usage: guest_busybox_f5.sh KERNEL_SOURCE SEEDED_GE_O_IMAGE REPORT_DIR" >&2
  exit 2
fi
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL=$(cd "$1" && pwd)
IMAGE=$(realpath "$2")
REPORTS=$(cd "$3" && pwd)
OUT="$REPORTS/qemu-busybox-f5"
mkdir -p "$OUT"
[ -f "$IMAGE" ] || { echo "Missing seeded GE/O image" >&2; exit 1; }

export QEMU_O_BOOT_LIB=1
# shellcheck disable=SC1091
source "$KERNEL/tools/qemu-o-boot.sh"
QOB_OUT="$OUT"
QOB_ACCEL=kvm:tcg
QOB_TIMEOUT=240
QOB_NO_REPL=1
QOB_BUILD=never
QOB_CPUS=2
QOB_INI_NCPU=2
qob_prepare_outdir
qob_preflight
trap qob_cleanup EXIT

puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
qob_write_ini "$OUT/plan9.ini" "$puuid" /9pc64.elf 0
printf 'bootmanifest=/processes.conf\n' >> "$OUT/plan9.ini"
cat > "$OUT/processes.conf" <<EOF
version=1
[process.replA]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000003
owner=replA
cpu=0
args=-R
required=true
[process.replB]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000003
owner=replB
cpu=1
args=-R
required=true
[process.replC]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000003
owner=replC
args=-R
required=true
[process.busyboxF5]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000005
owner=busyboxF5
args=busybox
required=true
EOF
qob_make_esp "$OUT/plan9.ini" "$OUT/esp.img"
mcopy -i "$OUT/esp.img" "$OUT/processes.conf" ::/processes.conf
qob_make_disk "$IMAGE" "$OUT/disk.img" "$OUT/esp.img" "$puuid" "$OUT/disk.layout"
cp "$QOB_OVMF_VARS" "$OUT/vars.fd"

mon="$OUT/monitor.sock"
serial="$OUT/serial-console.log"
: > "$serial"
rm -f "$mon"
"$QOB_QEMU" -name plan2001-busybox-f5 -nodefaults \
  -m "$QOB_MEM" -smp "$QOB_CPUS" -machine "q35,accel=$QOB_ACCEL" -cpu max \
  -drive "if=pflash,format=raw,readonly=on,file=$QOB_OVMF_CODE" \
  -drive "if=pflash,format=raw,file=$OUT/vars.fd" \
  -drive "file=$OUT/disk.img,format=raw,if=none,id=d0" \
  -device "virtio-blk-pci-non-transitional,drive=d0,bootindex=0" \
  -device VGA -nic none -display none \
  -serial "file:$serial" -monitor "unix:$mon,server=on,wait=off" \
  -no-reboot > "$OUT/qemu.log" 2>&1 &
QOB_CURRENT_PID=$!

timeout 240 python3 "$ROOT/tools/ci/f5_console_accept.py" \
  "$mon" "$serial" "$OUT/screens" "$KERNEL/kernel/core/fbconsolefont.h"

cat > "$REPORTS/busybox-f5-guest.md" <<'EOF'
## BusyBox native F5 console in real QEMU

- **PASS:** Kernel boot manifest starts BusyBox in independent slot 4 (Ctrl+Alt+F5).
- **PASS:** Original upstream BusyBox echo_main emits the READY marker on real VGA via a capability-region stdout ring.
- **PASS:** A QEMU PS/2 key sequence is delivered to the BusyBox process through its read-only stdin ring; userland reads it and prints the verified response on F5.
- **PASS:** Returning to F1 keeps BusyBox output isolated from the kernel SystemConsole, and switching to F5 restores its history.
- **LIMITATION:** The interactive read loop is a userland I/O acceptance launcher, NOT BusyBox ash, a POSIX TTY, or a complete multicall port.
- **NOT RUN:** Full sh, file descriptors for arbitrary processes, physical keyboard/hardware.
EOF
echo "BusyBox F5 real VGA + keyboard guest test PASS"
