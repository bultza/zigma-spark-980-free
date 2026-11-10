#!/bin/sh
# Local-mode activation for a clean modular installation; never flashes or reboots.
set -eu
umask 077
PATH=/bin:/usr/bin:/sbin:/usr/sbin
export PATH
base=/root/zigma-free
state=$base/activation
marker=$base/local-mode.lock
runtime=/tmp/zigma-mode
fail() { echo "ERROR: $*" >&2;exit 1; }
boot=$(cat /proc/sys/kernel/random/boot_id)
# Called before vendor init after a power interruption during an unaccepted trial.
if test "${1:-}" = boot-check;then
 if test -f "$state/pending-boot" && test "$(cat "$state/pending-boot")" != "$boot";then
  rm -f "$marker"
  mv "$state/pending-boot" "$state/interrupted-boot"
 fi
 exit 0
fi
# A bounded, private record survives Wi-Fi loss and power interruption.
# Do not take activation lock: the timer and status checks must stay independent.
if test "${1:-}" = capture;then
 test "${2:-}" = "$(cat "$state/trial-id" 2>/dev/null || true)" || exit 0
 exec >"$state/trial-diagnosis.log" 2>&1
 n=0
 while test "$n" -lt 55;do
  echo "SNAPSHOT epoch=$(date +%s) uptime=$(cut -d ' ' -f 1 /proc/uptime)"
  for file in "$runtime/state" /tmp/zigma-network/status.json /tmp/zigma-core/health.json;do
   echo "FILE $file"; head -c 4096 "$file" 2>/dev/null || true;echo
  done
  cat /proc/net/wireless
  sync
  test -f "$state/pending-boot" || exit 0
  n=$((n+1));sleep 5
 done
 exit 0
fi
test -f "$base/installer-bundle.sha256" || fail 'Not a modular installation'
test ! -L "$state" || fail 'Conflicting activation directory'
mkdir -p "$state" "$runtime"
exec 6>"$state/lock";flock 6
abort_trial() {
 test -f "$state/pending-boot" || return 0
 rm -f "$marker"
 "$base/zigma-mode-backend" recover "$base" "$runtime" "$marker" 6>&-
 mv "$state/pending-boot" "$state/aborted-boot"
 sync
}
case "${1:-}" in
 watch)
  # The child closes its inherited lock before sleeping.
  fail 'watch is internal; use expire after the bounded timer'
 ;;
 expire)
  test "${2:-}" = "$(cat "$state/trial-id" 2>/dev/null || true)" || exit 0
  abort_trial
 ;;
 abort) abort_trial;echo 'Vendor mode requested; SSH and staged files retained.';;
 status)
  if test -f "$state/pending-boot";then echo 'Trial pending; accept only after verifying SSH and web from your computer.'
  elif test -f "$state/accepted" && test -f "$marker";then echo 'Local mode accepted; cold boot still requires manual validation.'
  else echo 'Local mode not selected.';fi
 ;;
 start)
  test ! -e "$marker" && test ! -L "$marker" || fail 'A mode selector already exists'
  test ! -e "$state/pending-boot" || fail 'A trial is already pending'
  test ! -e "$state/accepted" || fail 'Already accepted; this is not an updater'
  test -s /root/yg/etc/wpa_supplicant.conf || fail 'Complete vendor Wi-Fi pairing first'
  /root/etc/init.d/S03zigma-hostname start
  /root/etc/init.d/S96ntpsync start 6>&-
  "$base/zigma-mode-backend" native-health "$base" "$runtime" "$marker" 6>&- || fail 'Native processes are not healthy'
  cmp -s /etc/inittab "$base/vendor-init/inittab.original" || cmp -s /etc/inittab "$base/inittab.local" || fail 'Inittab changed since staging'
  for name in S70yg S99daemon;do
   cmp -s "/root/etc/init.d/$name" "$base/vendor-init/$name" || grep -qx '# zigma-modular-mode-v1' "/root/etc/init.d/$name" || fail 'Vendor startup changed since staging'
  done
  cat "$base/vendor-init/inittab.original" >"$base/inittab.local"
  printf '\nnull::respawn:/root/zigma-free/zigma-mode boot --supervise 1 --core-data-dir /root/zigma-free/data/core-records\n' >>"$base/inittab.local"
  # Persist pending state before installing any startup guards. A cold boot
  # returns to vendor while the independent timer covers a lost SSH connection.
  printf '%s\n' "$boot" >"$state/pending-boot"
  trial="$boot:$$:$(date +%s)"
  printf '%s\n' "$trial" >"$state/trial-id"
  awk '{printf "%.0f\n",$1+240}' /proc/uptime >"$state/deadline"
  sync
  nohup "$base/activate" capture "$trial" >/dev/null 2>&1 </dev/null 6>&- &
  nohup sh -c 'sleep 240; exec /root/zigma-free/activate expire "$1"' sh "$trial" >"$state/watchdog.log" 2>&1 </dev/null 6>&- &
  for name in S70yg S99daemon;do
   cat >"/root/etc/init.d/$name.zigma-new" <<GUARD
#!/bin/sh
# zigma-modular-mode-v1
test -x /root/zigma-free/activate || exit 1
/root/zigma-free/activate boot-check || exit 1
case "\${1:-}" in
 start|restart|reload)
  if test -e /root/zigma-free/local-mode.lock || test -L /root/zigma-free/local-mode.lock;then exit 0;fi
 ;;
esac
exec /root/zigma-free/vendor-init/$name "\$@"
GUARD
   chmod 700 "/root/etc/init.d/$name.zigma-new"
   mv "/root/etc/init.d/$name.zigma-new" "/root/etc/init.d/$name"
  done
  cat >"/root/etc/init.d/S98zigma-mode.zigma-new" <<'INIT'
#!/bin/sh
# zigma-modular-mode-v1
set -eu
base=/root/zigma-free
case "${1:-}" in
 start)
  "$base/activate" boot-check
  if ! cmp -s "$base/inittab.local" /etc/inittab;then
   cmp -s "$base/vendor-init/inittab.original" /etc/inittab
   mount -o bind "$base/inittab.local" /etc/inittab
  fi
  kill -HUP 1
 ;;
 stop) :;;
 *) exit 2;;
esac
INIT
  chmod 700 /root/etc/init.d/S98zigma-mode.zigma-new
  mv /root/etc/init.d/S98zigma-mode.zigma-new /root/etc/init.d/S98zigma-mode
  cp "$base/install-init/S98zigma-free" /root/etc/init.d/S98zigma-free.zigma-new
  chmod 700 /root/etc/init.d/S98zigma-free.zigma-new
  mv /root/etc/init.d/S98zigma-free.zigma-new /root/etc/init.d/S98zigma-free
  printf '%s\n' "$trial" >"$marker"
  sync
  /root/etc/init.d/S98zigma-mode start 6>&-
  /root/etc/init.d/S98zigma-free start 6>&-
  echo 'Trial started: 240 seconds. Verify SSH and web, then run /root/zigma-free/activate accept.'
 ;;
 accept)
  test -f "$state/pending-boot" && test "$(cat "$state/pending-boot")" = "$boot" || fail 'No trial in this boot'
  now=$(awk '{printf "%.0f",$1}' /proc/uptime)
  test "$now" -lt "$(cat "$state/deadline")" || fail 'Trial expired; recovery will run'
  test "$(cat "$marker")" = "$(cat "$state/trial-id")" || fail 'Selector changed'
  "$base/zigma-core-arm" --check-health /tmp/zigma-network/status.json --check-exe "$base/zigma-network-arm"
  "$base/zigma-core-arm" --check-health /tmp/zigma-core/health.json --check-exe "$base/zigma-core-arm"
  "$base/zigma-free-service" status
  test -z "$(pidof ygclient daemon || true)" || fail 'Vendor processes still running'
  wget -q -T 5 -O /dev/null http://127.0.0.1:8765/
  mv "$state/pending-boot" "$state/accepted"
  sync
  echo 'Local mode accepted. Reboot, pairing and physical behavior remain manual tests.'
 ;;
 *) fail 'Usage: activate start|accept|abort|status';;
esac
