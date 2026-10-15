# F-51A V71R051A physical-device validation

Validation date: 2026-10-15
Fingerprint: `DOCOMO/F51A/F51A:12/V71R051A/F51A.20230213:user/release-keys`
Kernel: `4.19.136+ #1 SMP PREEMPT Mon Feb 13 18:35:16 JST 2023`

The run started in `uid=2000(shell)`, context `u:r:shell:s0`, SELinux
`Enforcing`, bootloader locked. The device was not rooted beforehand and no
partition was written.

- The physical window switched cleanly in the read-only self-test
  (`value=1` then `value=3`).
- `init_user_ns` was found at physical `0xa262a6e8` by signature.
- `selinux_state` was found at physical `0xa3055000`.
- The enforcing byte was cleared through the physical PTE window.
- The sprayed `task_struct` and its `cred` slab object were located and
  patched with GPU commands only.
- The child reported `uid=0 euid=0 gid=0` with full inheritable, permitted,
  effective, bounding and ambient capabilities.

Interactive PTY verification:

```text
F51A:/data/local/tmp # id; getenforce
uid=0(root) gid=0(root) groups=0(root),... context=u:r:shell:s0
Permissive
F51A:/data/local/tmp # dd if=/dev/block/by-name/boot_a of=boot_a.img bs=4194304
24+0 records in
24+0 records out
100663296 bytes (96 M) copied
F51A:/data/local/tmp # sha256sum boot_a.img
583890b03b71a96888711e704b6bb79ef347d2bbca0c9324b9b617669ba479b5
```

Credential state of the root child:

```text
CapInh: 0000003fffffffff
CapPrm: 0000003fffffffff
CapEff: 0000003fffffffff
CapBnd: 0000003fffffffff
CapAmb: 0000003fffffffff
NoNewPrivs: 0
Seccomp: 0
```

Both root and the permissive state were gone after a reboot.

Artifacts of that run, which predates the single-binary reorganisation:

```text
646f36484a425407335050724b0e2d7a4ccbdbbca89e259267134b1218d7eaa8  cve-2023-33107-f51a
afec41d278ae800af5f0e1169dbee34bd0c07758e305b3884f3d50cdf7616548  mempressure
6a821ed50253cd166ffb2c1c3afb05791c540fa9a77badd156d2e7f097e4a235  cve-2023-33107-f51a.verified
```

`cve-2023-33107-f51a.verified` is the binary that produced the first uid-0
confirmation. The reorganisation merged `mempressure` into the PoC as a
`--mempressure` subcommand; the only added code is the argument branch at the
top of `main()`, and the exploit path is byte-for-byte the same source. The
merged artifact that the device run below exercised is
`475ef11f19deead36ccafc783496f3edc1ad73a67e2c460edc5c0a0333788409`. The tree
has since gained the handover prompt fix described at the end of this file, so
the current build hashes
`b1effb1593c55812ecea57ace68c1ea12fe21e54664690c916cebe38351dc956`
(`evidence/current-artifact.sha256`). The difference is the handover newline
injection and its message; no exploit step changed.

Matching the validated build mattered: the validated binary was unoptimised
with debug info, and a `-O0 -g` build of this tree reproduces its `.text` size
(0x6e88) plus 740 bytes, which is exactly the added `--mempressure` code. The
Makefile pins `-O0 -g` for that reason rather than optimising a race.

## Re-validation of the single-binary build, 2026-10-15

`./run.sh --self-test` was run on the same unit after the reorganisation. The
race was won, the physical read/write window was established and read back
(`original=0x26e587000 value=1 mirror=0x26e588000 value=3`), and the merged
`--mempressure` mode reported `READY pid=14284 size=256 MiB`. Afterwards a
separate ordinary adb shell reported `Enforcing`, `uid 2000` and an uptime of
15 hours, so the read-only mode changed no kernel state and the kernel did not
panic. Log: `evidence/merged-artifact-selftest-2026-10-15.txt`.

The full `--apply` path has not been re-run since the reorganisation; the
uid-0 and permissive results above are from the pre-reorganisation binaries.

The exploit is a race, so a single attempt is not decisive. Failures show as
repeated `errno=12` or as a kernel panic and reboot. Success was reproduced
several times on the same unit on 2026-10-15.

## Handover prompt

The `--apply` handover used to look hung: after
`[*] Handing adb stdin/stdout to /system/bin/sh -i` the interactive prompt
often did not draw until the operator pressed Enter. The root shell was already
running, so this was a display artefact rather than a failed escalation.
`/system/bin/sh` is mksh, which prints its prompt on the first read of the tty.

The parent now queues that read itself, writing one newline into the terminal
input queue with `TIOCSTI` immediately after it sets the child to exec the
shell. The byte waits in the queue until the shell reads it, so this cannot
race against the child reaching `execl()`. A failure of the ioctl is reported
and falls back to asking for one Enter press.

`TIOCSTI` was confirmed on this device from `u:r:shell:s0`, uid 2000, SELinux
Enforcing, over a real adb PTY:

```text
isatty(0)=1
TIOCSTI OK: a newline is now queued in the tty input
```

The newline echoed back on the line above the result, which is the injected
byte reaching the input queue.

Raw log: `evidence/v71r051a-2026-10-15.txt`
Attack-surface reachability: `evidence/kgsl-reachability.txt`
