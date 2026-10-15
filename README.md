# F-51A temporary root — V71R051A

Build-specific CVE-2023-33107 port for FUJITSU arrows 5G F-51A. It obtains a
temporary uid-0 shell and sets SELinux to permissive. It writes no boot,
system, or userdata partition. The root credentials and the permissive state
disappear on reboot.

CVE-2023-33107 was fixed upstream in December 2023. This build is still
vulnerable, so the finding is the missing patch rather than a new bug.

## Supported build

- Fingerprint: `DOCOMO/F51A/F51A:12/V71R051A/F51A.20230213:user/release-keys`
- Android 12 (SDK 31), SPL 2022-12-01
- Kernel: `4.19.136+ #1 SMP PREEMPT Mon Feb 13 18:35:16 JST 2023`
- SoC: Qualcomm Snapdragon 865 (`kona` / SM8250), Adreno 650, 4 KiB pages
- Bootloader locked (`ro.boot.flash.locked=1`, `verifiedbootstate=green`)

`run.sh` rejects every other fingerprint.

## Build and run

Use Android NDK r26d or newer:

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk-r26d
./build.sh
./run.sh
```

The runner does not reboot the device. After verification it opens an
interactive root shell whose prompt is `F51A:/data/local/tmp #`. When stdout is
not a terminal, it prints the command needed to open that shell.

`/system/bin/sh` is mksh, which draws its prompt on the first read of the tty,
so handover used to look dead until the operator pressed Enter. The PoC now
queues that newline itself with `TIOCSTI` right after handing the tty over, and
the prompt appears on its own. If the ioctl is refused it says so and asks for
one Enter press instead.

Two read-only modes change no kernel state and are the right first step on a
new unit:

```sh
./run.sh --self-test    # switch the physical window only
./run.sh --probe-only   # also locate selinux_state, write nothing
```

The exploit is a kernel race. A run can miss or panic/reboot the device.
Repeated `errno=12` means KGSL is in a state the exploit cannot use; reboot and
retry. A reboot also clears every effect of a successful run.

## One artifact

`build.sh` produces a single arm64 executable, `out/cve-2023-33107-f51a`, and
that is the only file pushed to the device.

| Mode | Behaviour | Changes kernel state |
| --- | --- | --- |
| `--apply` | rewrite SELinux, install credentials, spawn a root shell | yes |
| `--probe-only` | locate `selinux_state` | no |
| `--self-test` | switch the physical window | no |
| `--mempressure N` | hold N MiB resident | no |

`--mempressure` was a second binary in earlier revisions. It raises the chance
that pages freed from KGSL are recycled as CPU page-table pages, and it never
opens `/dev/kgsl-3d0`. `run.sh` starts and stops it.

## How it works

The race pairs an `IOCTL_KGSL_MAP_USER_MEM` whose `hostptr + len` wraps in 64
bits (`len=0xffffffffffefd000`, CWE-190) with a `MAP_FIXED` mmap of a
`KGSL_MEMFLAGS_USE_CPU_MAP` object over the same virtual address. Winning it
leaves the IOMMU PTEs of a freed KGSL object live, and the pages behind them are
then recycled as CPU page-table pages, which yields a physical read/write
window.

Nothing afterwards dereferences a kernel virtual address from the CPU. The port
reuses user PTEs from the residual I/O-coherent KGSL mapping, signature-searches
the live `init_user_ns` and `selinux_state` to clear the SELinux enforcing byte,
then locates the sprayed `task_struct` and its `cred` slab object and updates
UID, GID and all capabilities with GPU commands only. The `addr_limit` route,
which rebooted this device under its PAN and vendor kernel hardening, is not
used.

## Port evidence

`/dev/kgsl-3d0` on this build is `u:object_r:gpu_device:s0`, mode 0666, and the
platform policy allows `ioctl` on `gpu_device` to `base_typeattr_192`, which
resolves to `appdomain` minus `isolated_app`. The vulnerable ioctl is therefore
reachable from a third-party app holding no Android permission. The policy was
read out of the device's own `super.img`; see
`evidence/kgsl-reachability.txt`.

The struct offsets in `source/src/ex.c` were generated from the device's live
`/proc/config.gz` and the FCNT 4.19 sources in `source/kernel`, through
`analysis/task_offsets.c`.

The first physical-device validation reached `uid=0`, `CapEff
0000003fffffffff`, `getenforce` `Permissive`, and dumped `boot_a` from the
resulting shell. See `VALIDATION.md`.

## Layout

```text
Makefile build.sh run.sh   build and run
source/src/                the F-51A port (ex.c, mempressure.c)
source/upstream/           reference PoC (keto0422/CVE-2023-33107, e82cee7)
source/kernel/ source/vendor/  FCNT 4.19 sources, used to derive offsets
out/                       build output (one file)
evidence/                  device logs and hashes
```

## Provenance

This port follows
[keto0422/CVE-2023-33107](https://github.com/keto0422/CVE-2023-33107)
(commit `e82cee7`), retained as `source/upstream/ex.c`. That repository
declares no license, so redistribution terms for the derived parts of
`source/src/ex.c` are unsettled; see `NOTICE`.
