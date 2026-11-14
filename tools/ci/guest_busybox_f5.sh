#!/usr/bin/env bash
# Two real Plan2001 AMD64 QEMU boots: trusted Unix supervisor at manifest F5; Unix init is SPAWNed.
# Run twice with the SAME disposable GE/O disk. Session view is RAM-only.
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
[process.unixInitF5]
source=geo:PARTUUID=$puuid:OID=0x4f45584543000005
owner=unixSupervisorF5
args=unix-supervisor 0x4f45584543000006
required=true
EOF
qob_make_esp "$OUT/plan9.ini" "$OUT/esp.img"
mcopy -i "$OUT/esp.img" "$OUT/processes.conf" ::/processes.conf
qob_make_disk "$IMAGE" "$OUT/disk.img" "$OUT/esp.img" "$puuid" "$OUT/disk.layout"

# The original disk image is built once. Between boots only the QEMU
# process and its OVMF variable overlay are reset, not the GE/O disk.
for phase in initial reboot; do
  mon="$OUT/monitor-$phase.sock"
  serial="$OUT/serial-$phase.log"
  vars="$OUT/vars-$phase.fd"
  : > "$serial"
  rm -f "$mon"
  cp "$QOB_OVMF_VARS" "$vars"
  "$QOB_QEMU" -name "plan2001-unix-init-$phase" -nodefaults \
    -m "$QOB_MEM" -smp "$QOB_CPUS" -machine "q35,accel=$QOB_ACCEL" -cpu max \
    -drive "if=pflash,format=raw,readonly=on,file=$QOB_OVMF_CODE" \
    -drive "if=pflash,format=raw,file=$vars" \
    -drive "file=$OUT/disk.img,format=raw,if=none,id=d0" \
    -device "virtio-blk-pci-non-transitional,drive=d0,bootindex=0" \
    -device VGA -nic none -display none \
    -serial "file:$serial" -monitor "unix:$mon,server=on,wait=off" \
    -no-reboot > "$OUT/qemu-$phase.log" 2>&1 &
  QOB_CURRENT_PID=$!

  timeout 240 python3 "$ROOT/tools/ci/f5_console_accept.py" \
    "$mon" "$serial" "$OUT/screens/$phase" \
    "$KERNEL/kernel/core/fbconsolefont.h" "$phase"
  qob_cleanup
done

cat > "$REPORTS/busybox-f5-guest.md" <<'EOF'
## Native trusted supervisor -> separate Unix init -> F5 shell in real QEMU

- **PASS:** Manifest F5 slot boots a trusted userland Unix supervisor, which retains the kernel-owned NON-delegable session rings.\n- **PASS:** The supervisor creates six fresh region capabilities, delegates one RW data / RO ACK channel for each direction using Oinheritv, and sends only Vexecute to the independent Unix init child.\n- **PASS:** Kernel console capabilities are NOT inherited by the child, and no kernel VFS, Pipe or Unix API is introduced.
- **PASS:** The separate child starts Unix init, constructs a fresh volatile Unix root PVD and enters its embedded F5 evaluator.
- **PASS:** First prompt displays unix:/$; changing cwd updates the path in the prompt.
- **PASS:** Real PS/2 input and kernel VGA verify pwd, cd and ls, errors, root confinement, F1 isolation and F5 focus restore.
- **PASS:** First boot ends in /usr/bin. Second distinct QEMU boot reuses the exact same scratch GE/O disk, executes init again, and enters / (pwd confirms).
- **LIMITATION:** Supervisor and Unix init are separate native processes, but the F5 evaluator within init is NOT BusyBox ash/sh or a POSIX TTY.
- **NOT RUN:** Writable GE/O user files, hardware, crash recovery; this manifest enables Vexecute but does not supply a storage manifest selector.
EOF
echo "Native unix-init / F5 shell real VGA + cold-boot reset QEMU test PASS"
