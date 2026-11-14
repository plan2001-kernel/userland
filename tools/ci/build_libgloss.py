#!/usr/bin/env python3
"""Build two native Plan2001/libgloss ELFs for actual kernel QEMU testing.

This is a bridge probe against the existing Plan2001 native libc, not a
complete replacement with the upstream Newlib library.
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


def run(cmd, log):
    with log.open("a") as out:
        out.write("$ " + shlex.join(map(str, cmd)) + "\n")
        out.flush()
        result = subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT)
    if result.returncode:
        print("\n".join(log.read_text(errors="replace").splitlines()[-80:]))
        raise RuntimeError("command failed: " + shlex.join(map(str, cmd)))


def capture(*cmd):
    return subprocess.check_output(cmd, text=True)


def validate(elf, report, expected):
    h = capture("readelf", "-hW", str(elf))
    p = capture("readelf", "-lW", str(elf))
    d = capture("readelf", "-dW", str(elf))
    symbols = capture("nm", "--defined-only", str(elf))
    undefined = capture("nm", "-u", str(elf)).strip()
    if not re.search(r"Type:\s+EXEC", h):
        raise RuntimeError("not ET_EXEC")
    if "Advanced Micro Devices X86-64" not in h:
        raise RuntimeError("not AMD64")
    if "INTERP" in p or "DYNAMIC" in p or "(NEEDED)" in d or undefined:
        raise RuntimeError("unexpected dependency / undefined symbol: " + undefined)
    if len(re.findall(r"(?m)^\s*LOAD\s+", p)) != 2:
        raise RuntimeError("expected two Plan2001 PT_LOAD segments")
    for sym in expected:
        if not re.search(rf"(?m)^[0-9a-f]+\s+[Tt]\s+{re.escape(sym)}$", symbols):
            raise RuntimeError("missing target symbol: " + sym)
    (report / (elf.stem + ".audit.txt")).write_text(h + p + d + symbols)
    return {"name": elf.name, "size": elf.stat().st_size,
            "sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
            "symbols": expected, "dynamic": False}


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--kernel", required=True, type=Path)
    a.add_argument("--reports", required=True, type=Path)
    a.add_argument("--cc", default="gcc-14")
    args = a.parse_args()
    args.reports.mkdir(parents=True, exist_ok=True)
    build = args.reports / "libgloss"
    build.mkdir(exist_ok=True)
    log = args.reports / "libgloss-build.log"
    report = args.reports / "libgloss-build.md"
    lib = args.kernel / "build/gcc/mpy/lib/libc-user.a"
    rt = args.kernel / "build/gcc/mpy/mpyobj"
    ld = args.kernel / "micropython/gcc/user.ld"
    runtime = [rt / n for n in ("rt_startup.o", "rt_start.o",
                                "rt_misc.o", "rt_stubs.o", "libm_frexp.o",
                                "libm_ldexp.o", "libm_scalbn.o")]
    sources = [PORT / s for s in ("libgloss_plan2001.c",
                                   "libgloss_probe.c", "libgloss_exit_child.c")]
    missing = [str(p) for p in [*runtime, *sources, lib, ld] if not p.is_file()]
    if missing:
        raise SystemExit("missing native input(s): " + ", ".join(missing))
    common = [
        args.cc, "-std=gnu11", "-m64", "-O2", "-ffreestanding", "-nostdinc",
        "-fno-builtin", "-fno-stack-protector", "-fno-pie", "-fno-pic",
        "-fno-strict-aliasing", "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables", "-fno-omit-frame-pointer",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror",
        "-I", str(PORT), "-I", str(PORT / "include"),
        "-I", str(args.kernel / "runtime/abi"),
    ]
    try:
        objs = {}
        for src in sources:
            out = build / (src.stem + ".o")
            run(common + ["-c", str(src), "-o", str(out)], log)
            objs[src.stem] = out
        records = []
        for label, program, expected in (
            ("libgloss-boot", "libgloss_probe", [
                "_start", "p9_start", "_read", "_write", "_sbrk",
                "p9_gloss_bind_stdin", "p9_gloss_unbind_stdin"]),
            ("libgloss-exit", "libgloss_exit_child", [
                "_start", "p9_start", "_exit"]),
        ):
            elf = build / (label + ".elf")
            link = [
                args.cc, "-nostdlib", "-nostartfiles", "-static", "-no-pie",
                "-Wl,-T," + str(ld), "-Wl,-e,_start",
                "-Wl,--no-dynamic-linker", "-Wl,-z,noexecstack",
                "-Wl,--gc-sections", "-Wl,--build-id=none",
                "-o", str(elf),
                *map(str, runtime[:3]),
                str(objs[program]), str(objs["libgloss_plan2001"]),
                *map(str, runtime[3:]), str(lib), "-lgcc",
            ]
            run(link, log)
            records.append(validate(elf, build, expected))
        (build / "result.json").write_text(
            json.dumps({"pinned_kernel": capture("git", "-C", str(args.kernel),
                                                "rev-parse", "HEAD").strip(),
                        "native_plan2001_libc_used_for_link": True,
                        "full_upstream_newlib_libc_linked": False,
                        "artifacts": records}, indent=2) + "\n")
        report.write_text(
            "### Native Plan2001/libgloss bridge build\n\n"
            "- PASS: native read, write, sbrk and exit bridge compiled.\n"
            "- PASS: two static AMD64 ET_EXEC, exactly two PT_LOADs, no dynamic loader/undefined symbols.\n"
            "- NOT RUN here: QEMU boot and child-exit assertions (separate CI steps).\n"
            "- NOT RUN: full Newlib/libgloss build, stdio, full POSIX and physical hardware.\n")
        print("Plan2001 native libgloss bridge ELFs: PASS")
    except Exception as e:
        report.write_text(
            "### Native Plan2001/libgloss bridge build\n\n"
            "**FAIL:** " + str(e) + "\n\nQEMU: NOT RUN.\n")
        raise


if __name__ == "__main__":
    main()
