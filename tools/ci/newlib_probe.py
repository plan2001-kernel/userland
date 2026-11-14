#!/usr/bin/env python3
"""Link unchanged Newlib sources into a real Plan2001 AMD64 ELF.

This is an intentionally LIMITED source/link/QEMU experiment, not a complete
Newlib port or replacement of the existing Plan2001 C runtime.
"""
import argparse
import hashlib
import json
import re
import shlex
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "ports/newlib"
SRC_NAMES = ("strlcpy", "strlcat", "strnlen")


def run(cmd, log):
    with log.open("a") as output:
        output.write("$ " + shlex.join(map(str, cmd)) + "\n")
        output.flush()
        p = subprocess.run(cmd, stdout=output, stderr=subprocess.STDOUT)
    if p.returncode:
        raise RuntimeError("exit %d running %s" % (p.returncode, shlex.join(map(str, cmd))))


def capture(*args):
    return subprocess.check_output(args, text=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--kernel", type=Path, required=True)
    p.add_argument("--newlib", type=Path, required=True)
    p.add_argument("--reports", type=Path, required=True)
    p.add_argument("--cc", default="gcc-14")
    a = p.parse_args()
    a.reports.mkdir(parents=True, exist_ok=True)
    dst = a.reports / "newlib-probe"
    dst.mkdir(exist_ok=True)
    report = a.reports / "newlib-probe.md"
    log = a.reports / "newlib-probe.log"
    rt = a.kernel / "build/gcc/mpy/mpyobj"
    libc = a.kernel / "build/gcc/mpy/lib/libc-user.a"
    ld = a.kernel / "micropython/gcc/user.ld"
    src = [a.newlib / "newlib/libc/string" / (n + ".c") for n in SRC_NAMES]
    deps = [*src, PORT / "probe.c", ROOT / "ports/busybox/compat.c", libc, ld]
    deps.extend(rt / n for n in ("rt_startup.o", "rt_start.o", "rt_misc.o",
                                  "rt_stubs.o", "libm_frexp.o", "libm_ldexp.o",
                                  "libm_scalbn.o"))
    missing = [str(f) for f in deps if not f.is_file()]
    if missing:
        raise SystemExit("Missing verified inputs: " + ", ".join(missing))
    flags = [
        a.cc, "-std=gnu11", "-m64", "-O2", "-ffreestanding", "-nostdinc",
        "-fno-builtin", "-fno-stack-protector", "-fno-pie", "-fno-pic",
        "-fno-strict-aliasing", "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables", "-fno-omit-frame-pointer",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror",
        "-I", str(PORT / "include"), "-I", str(ROOT / "ports/busybox"),
        "-I", str(a.kernel / "runtime/abi")
    ]
    try:
        objs = []
        for f in [*src, PORT / "probe.c", ROOT / "ports/busybox/compat.c"]:
            obj = dst / (f.stem + ".o")
            run(flags + ["-c", str(f), "-o", str(obj)], log)
            objs.append(obj)
        elf = dst / "newlib-plan2001.elf"
        cmd = [
            a.cc, "-nostdlib", "-nostartfiles", "-static", "-no-pie",
            "-Wl,-T," + str(ld), "-Wl,-e,_start", "-Wl,--no-dynamic-linker",
            "-Wl,-z,noexecstack", "-Wl,--gc-sections", "-Wl,--build-id=none",
            "-o", str(elf), str(rt / "rt_startup.o"),
            str(rt / "rt_start.o"), str(rt / "rt_misc.o"), *map(str, objs),
            str(rt / "rt_stubs.o"), str(rt / "libm_frexp.o"),
            str(rt / "libm_ldexp.o"), str(rt / "libm_scalbn.o"),
            str(libc), "-lgcc",
        ]
        run(cmd, log)
        h = capture("readelf", "-hW", str(elf))
        ph = capture("readelf", "-lW", str(elf))
        d = capture("readelf", "-dW", str(elf))
        syms = capture("nm", "--defined-only", str(elf))
        unres = capture("nm", "-u", str(elf)).strip()
        if not re.search(r"Type:\s+EXEC", h):
            raise RuntimeError("not ET_EXEC")
        if "Advanced Micro Devices X86-64" not in h:
            raise RuntimeError("not AMD64")
        if "INTERP" in ph or "DYNAMIC" in ph or "(NEEDED)" in d or unres:
            raise RuntimeError("unexpected dynamic dependency/undefined symbol: " + unres)
        if len(re.findall(r"(?m)^\s*LOAD\s+", ph)) != 2:
            raise RuntimeError("expected 2 PT_LOADs")
        for name in (*SRC_NAMES, "_start", "p9_start", "main", "full_write"):
            if not re.search(rf"(?m)^[0-9a-f]+\s+[Tt]\s+{name}$", syms):
                raise RuntimeError("missing target symbol " + name)
        (dst / "elf-audit.txt").write_text(h + "\n" + ph + "\n" + d + "\n" + syms)
        info = {
            "newlib_release": "4.6.0.20260123",
            "newlib_source_commit": "8ba4275b83ec27529f67e0d477611fa6d8d6e6bd",
            "unchanged_upstream_sources": list(SRC_NAMES),
            "kernel_sha": capture("git", "-C", str(a.kernel), "rev-parse", "HEAD").strip(),
            "native_elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
            "native_elf_size": elf.stat().st_size,
            "full_newlib_port": "NOT TESTED", "qemu": "SEPARATE STEP"
        }
        (dst / "build.json").write_text(json.dumps(info, indent=2) + "\n")
        report.write_text("### Newlib native build\n\n"
                          "- **PASS:** unchanged commit-pinned Newlib 4.6.0 strlcpy, strlcat, strnlen compiled.\n"
                          "- **PASS:** native static AMD64 ET_EXEC, two PT_LOADs, symbols present, no Linux libc/dynamic loader.\n"
                          "- **NOT RUN here:** real-kernel QEMU guest (separate CI step).\n"
                          "- **NOT RUN:** full Newlib, libc replacement, libgloss, POSIX I/O, BusyBox linked with Newlib.\n")
        print("Native Plan2001 Newlib source probe ELF: PASS", flush=True)
    except Exception as e:
        report.write_text("### Newlib native build\n\n**FAIL:** " + str(e) +
                          "\n\nQEMU: **NOT RUN**.\n")
        raise


if __name__ == "__main__":
    main()
