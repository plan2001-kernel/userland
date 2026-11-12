# Reproducible build and test workspace

This is the **working source/build mechanism** for the Plan2001 userland project. It is not yet a complete Plan2001 BusyBox port. The workflow's currently executable output is a **statically linked Linux-host reference BusyBox** and an audit of native kernel dependencies.

## Separation of repositories

The CI and local development directory layout is:

```text
workspace/
  userland/   # writable: code, docs, CI; GitHub repo plan2001-kernel/userland
  kernel/     # pinned checkout, INPUT ONLY; never modified by userland build
  busybox/    # pinned upstream BusyBox checkout; host-control build writes here
reports/      # generated diagnostics, outside all source Git trees
```

The GitHub Actions job implements the same layout using runner temp directories and a userland checkout:

```text
$GITHUB_WORKSPACE/             userland checkout (the PR under test)
$RUNNER_TEMP/plan2001-sources/
  kernel/                     pinned exact kernel SHA
  busybox/                    pinned exact upstream BusyBox SHA
$RUNNER_TEMP/plan2001-reports/  reports/artifacts
```

**No Git submodule or copy of the kernel sources is committed into userland.** The kernel remains its own canonical repo. The kernel checkout is logically read-only; Git does not enforce OS file permissions, but the audit and build scripts do not write to it. The BusyBox checkout may contain build output; discard/recreate it between runs.

## Single source lock

[`config/sources.json`](../config/sources.json) is the **only authority for external repository URLs and exact 40-character commit SHAs**. The source fetcher verifies that the actual checked-out HEAD matches each pin. Dependencies are not silently updated by a moving `main`, `master` or tag.

The first reproducibility baseline uses:

- `plan2001-kernel/kernel`, pinned to the commit in the lockfile.
- `mirror/busybox`, pinned to the commit in the lockfile (upstream BusyBox **1.36.1**, release tag `1_36_1`).

The mirror is a fetch location; the software remains upstream BusyBox, GPLv2 licensed. A change to either pin must be reviewed as a userland PR with a corresponding compatibility check. A kernel SHA update does not imply a BusyBox API change, nor vice versa.

## Local Debian execution

Use Python 3, Git, GCC, GNU Make, binutils (`readelf`) and `file`. On a Debian host:

```sh
sudo apt-get update
sudo apt-get install -y git python3 build-essential binutils file

mkdir -p ~/Src/Plan2001
cd ~/Src/Plan2001
git clone https://github.com/plan2001-kernel/userland.git
cd userland

export PLAN2001_DEPS="$HOME/Src/Plan2001/deps"
export PLAN2001_REPORTS="$HOME/Src/Plan2001/reports"

# Fresh destination required; the scripts never overwrite a checkout.
python3 tools/ci/fetch_sources.py \
  --lock config/sources.json \
  --dest "$PLAN2001_DEPS" \
  --reports "$PLAN2001_REPORTS"

python3 tools/ci/audit_kernel.py \
  --kernel "$PLAN2001_DEPS/kernel" \
  --reports "$PLAN2001_REPORTS"

python3 tools/ci/host_control.py \
  --busybox "$PLAN2001_DEPS/busybox" \
  --reports "$PLAN2001_REPORTS"
```

For a fresh second run, remove/recreate **only the throwaway `deps` and `reports` paths you deliberately created**, never your kernel development checkout, O volume or userland working tree. The fetcher intentionally refuses to overwrite an existing checkout.

The source trees will look like sibling `kernel` and `busybox` dependencies relative to the `userland` checkout, but there is no assumption that they live inside the userland repo. On a development host with an existing kernel checkout, use its **exact pinned revision** when doing manual research; the automated fetcher keeps a clean, isolated copy.

## GitHub Actions

Workflow: [`.github/workflows/build.yml`](../.github/workflows/build.yml).

Triggers: `pull_request`, pushes to `main`, and manual `workflow_dispatch`. Runner: `ubuntu-24.04`. Token permission: `contents: read` only.

Steps:

1. Check out the `userland` commit under test.
2. Install host build tools.
3. Fetch kernel and BusyBox by immutable source pins into separate sibling directories; compare both HEAD values.
4. Audit `runtime/abi` and `runtime/libc`. Compile one native ObjectSpace ABI header as a **host syntax test**, and inventory the runtime source files.
5. Configure BusyBox via `make allnoconfig`, enable exactly `CONFIG_STATIC`, `CONFIG_TRUE`, `CONFIG_FALSE` and `CONFIG_ECHO`, build with **Linux host libc**, check ELF dynamic dependencies, run `true`, `false`, `echo`.
6. Upload logs, config, ELF inspection and source inventory as a 14-day diagnostic artifact. Summarize what was and was not run.

The static Linux executable is **not** uploaded as a Plan2001 deliverable. It is an upstream/toolchain sanity control, not evidence of working Plan2001 libc linking, native ABI execution, or QEMU.

## Expected status matrix

| Check | Current workflow |
| --- | --- |
| Exact source revision verification | Executed |
| Kernel `runtime/libc` source inventory | Executed |
| Kernel ObjectSpace C-header host syntax | Executed |
| BusyBox static link against Linux host libc | Executed as control |
| BusyBox Linux host applet smoke | Executed as control |
| BusyBox static link against Plan2001 libc | **NOT RUN: adapters not yet implemented** |
| Plan2001 QEMU/OVMF BusyBox execution | **NOT RUN: no target BusyBox yet** |
| Physical hardware | **NOT RUN** |

Host-control build logs and the test summary are available under the workflow run's **Artifacts** and **Step summary**. Failure of the host control is a real CI failure; success must not be advertised as target compatibility.

## Transition to a real Plan2001 target build

Once the missing compatibility interfaces have been audited and implemented:

1. Add a separate `tools/ci/build_target.py` or equivalent that explicitly invokes the verified Plan2001 user toolchain, target startup code, and the adapted native C runtime.
2. Ensure target objects use no Linux headers, Linux startup objects, Linux syscalls, or host libc.
3. Produce a statically linked ELF and audit its ABI, entry point, PT_INTERP/dynamic section and undefined symbols.
4. Add a distinct, clearly named GitHub Actions job that uses the pinned kernel's boot/install/QEMU fixture to prove execution on the **real Plan2001 AMD64 kernel**.
5. Only after the guest test passes add persistent ObjectSpace paths, shell pipes and wider POSIX functionality. Treat ARM64 as a separate future port.

Changes needed in native kernel ABI belong to [kernel](https://github.com/plan2001-kernel/kernel), with a separately reviewed PR. Do not introduce POSIX path syscalls into the kernel to satisfy this userspace goal.
