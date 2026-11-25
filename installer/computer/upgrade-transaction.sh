#!/bin/sh
# Detached bounded managed-runtime upgrade with verified PC backups.
set -eu
umask 077
PATH=/bin:/sbin:/usr/bin:/usr/sbin:/root/bin
export PATH
stage=$1
action=$2
base=/root/zigma-free
case "$stage" in /tmp/zigma-upgrade.*) :;; *) exit 2;; esac
sha() { sha256sum "$1" | cut -d ' ' -f 1; }
sameboot() { test "$(cat /proc/sys/kernel/random/boot_id)" = "$(cat "$stage/boot")"; }
lock() { exec 7>"$stage/lock"; flock 7; }
unlock() { flock -u 7; exec 7>&-; }
identity() {
 for name in wifi_app_server_grit demo_rplidar; do
  p=$(pidof "$name"); case "$p" in ''|*[!0-9]*) return 1;; esac
  printf '%s %s %s\n' "$name" "$p" "$(awk '{print $22}' /proc/$p/stat)"
 done
}
checkpath() {
 case "$path" in
 /root/bin/zigmactl|/root/bin/zigma-time-sync|\
 "$base/free-zigma-agent-arm"|"$base/zigma-observe-arm"|"$base/zigma-core-arm"|"$base/zigma-network-arm"|\
 "$base/zigma-area-arm"|"$base/zigma-zones-arm"|"$base/zigma-maps-arm"|"$base/zigma-options-arm"|"$base/zigma-curl-arm"|\
 "$base/zigma-mode"|"$base/zigma-mode-backend"|"$base/zigma-free-service"|"$base/zigma-cron"|"$base/zigma-settings"|\
 "$base/activate"|"$base/firmware-profile.sh"|"$base/telegram-ca.pem"|"$base/licenses/transport.txt"|"$base/release.json"|\
 "$base/src/app.js"|"$base/src/index.html"|"$base/src/style.css"|"$base/src/nor.js"|"$base/src/favicon.svg"|\
 /root/etc/init.d/S04zigma-timezone|/root/etc/init.d/S51hoslam) :;; *) return 1;; esac
 case "$path" in *..*|*[!a-zA-Z0-9/._-]*) return 1;; esac
 test ! -L "$path" || return 1
 if test "$oldsha" = absent; then
  case "$path" in
   "$base/src/app.js"|"$base/src/index.html"|"$base/src/style.css"|"$base/src/nor.js"|"$base/src/favicon.svg"|"$base/telegram-ca.pem"|"$base/licenses/transport.txt"|"$base/activate"|"$base/firmware-profile.sh"|/root/etc/init.d/S04zigma-timezone) :;;
   *) return 1;;
  esac
  test ! -e "$path" || test -f "$path" || return 1
 else
  test -f "$path" || return 1
 fi
 parent=$(dirname "$path")
 while test "$parent" != /; do
  if test ! -d "$parent"; then
   # Only this package-owned directory may be absent on older installations.
   test "$parent" = "$base/licenses" && test "$path" = "$base/licenses/transport.txt" &&
    test "$oldsha" = absent && test ! -e "$parent" && test ! -L "$parent" || return 1
  else
   test ! -L "$parent" || return 1
  fi
  parent=$(dirname "$parent")
 done
}
current_sha() {
 if test -f "$path"; then sha "$path"; else echo absent; fi
}
restart_daemon() {
 case "$path" in
 "$base/zigma-core-arm") record=/tmp/zigma-mode/core.identity;;
 "$base/zigma-network-arm") record=/tmp/zigma-mode/network.identity;;
 *) return 0;; esac
 read -r own tick < "$record" || return 1
 case "$own:$tick" in *[!0-9:]*|:*|*:) return 1;; esac
 test "$(awk '{print $22}' /proc/$own/stat 2>/dev/null || :)" = "$tick" || return 0
 actual=$(readlink /proc/$own/exe)
 case "$actual" in "$path"|"$path (deleted)") :;; *) return 1;; esac
 kill -TERM "$own"
 n=0
 while test "$(awk '{print $22}' /proc/$own/stat 2>/dev/null || :)" = "$tick"; do
  test "$(awk '{print $3}' /proc/$own/stat 2>/dev/null || :)" != Z || break
  n=$((n+1)); if test "$n" -ge 8; then kill -KILL "$own"; break; fi
  sleep 1
 done
}
replace_files() {
 direction=$1
 sameboot || return 1
 identity > "$stage/native.current" || return 1
 cmp -s "$stage/native.before" "$stage/native.current" || return 1
 while read -r index path oldmode newmode oldsha newsha; do
  checkpath || return 1
  actual=$(current_sha)
  test "$actual" = "$oldsha" || test "$actual" = "$newsha" || return 1
  if test "$direction" = new; then expected=$newsha; mode=$newmode; else expected=$oldsha; mode=$oldmode; fi
  if test "$actual" != "$expected"; then
   if test "$expected" = absent; then
    rm "$path" || return 1
    continue
   fi
   if test "$direction" = new && test "$path" = "$base/licenses/transport.txt" && test ! -d "$base/licenses"; then
    # Parent checks above reject links. mkdir also refuses concurrent creation.
    mkdir "$base/licenses" || return 1
    : > "$stage/created-licenses"
    chmod 755 "$base/licenses" || return 1
   fi
   gzip -dc "$stage/$index.$direction.gz" > "$path.zigma-upgrade-new" || return 1
   test "$(sha "$path.zigma-upgrade-new")" = "$expected" || return 1
   chmod "$mode" "$path.zigma-upgrade-new" || return 1
   mv -f "$path.zigma-upgrade-new" "$path" || return 1
   restart_daemon || return 1
  fi
 done < "$stage/files.list"
 if test "$direction" = old && test -f "$stage/created-licenses"; then
  # Never recursively remove a directory: preserve any concurrent additions.
  rmdir "$base/licenses" || return 1
  rm "$stage/created-licenses" || return 1
 fi
 "$base/zigma-free-service" repair-observer 7>&- 9>&- || return 1
 "$base/zigma-free-service" repair-web 7>&- 9>&- || return 1
 sync
}
rollback() {
 echo rolling-back > "$stage/state"
 if replace_files old; then echo rolled-back > "$stage/state"; else echo rollback-failed > "$stage/state"; fi
}
case "$action" in
 accept)
  lock
  sameboot
  test "$(cat "$stage/state")" = ready
  test ! -f "$stage/rollback.now"
  while read -r index path oldmode newmode oldsha newsha; do checkpath; test "$(sha "$path")" = "$newsha"; done < "$stage/files.list"
  identity > "$stage/native.current"
  cmp -s "$stage/native.before" "$stage/native.current"
  # Write only after verification succeeds. The installed label is not part of
  # files.list/release.json and its previous contents are never inspected.
  version_tmp=$(mktemp "$base/.installer-version.XXXXXX")
  cat "$stage/installer-version.txt" > "$version_tmp"
  chmod 644 "$version_tmp"
  mv -f "$version_tmp" "$base/installer-version.txt"
  sync
  echo accepted > "$stage/state"
 ;;
 run)
  # Publish preflight failures even when set -e exits before the first write.
  echo starting > "$stage/state"
  phase=initialization; path=
  trap 'rc=$?; if test "$rc" -ne 0 && test "$(cat "$stage/state" 2>/dev/null)" = starting; then printf "Preflight failed: %s %s (exit %s)\n" "$phase" "${path:-}" "$rc" >&2; echo failed > "$stage/state"; fi' 0
  phase=installer-lock
  test ! -L /root/.zigma-install
  mkdir -p /root/.zigma-install
  test -d /root/.zigma-install
  test ! -L /root/.zigma-install/lock
  test ! -d /root/.zigma-install/lock
  exec 9>/root/.zigma-install/lock
  flock -n 9 || { echo "Preflight failed: installer lock is held" >&2; echo failed > "$stage/state"; exit 1; }
  phase=pending-installation
  test ! -e /root/.zigma-install/pending
  phase=transaction-lock
  lock
  phase=boot-identity
  sameboot
  phase=native-process-identity
  identity > "$stage/native.before"
  # Validate every original and both compressed versions before the first write.
  while read -r index path oldmode newmode oldsha newsha; do
   phase=target-path
   checkpath
   phase=original-file-hash
   test "$(current_sha)" = "$oldsha"
   phase=temporary-target
   test ! -e "$path.zigma-upgrade-new" && test ! -L "$path.zigma-upgrade-new"
   phase=backup-hash
   if test "$oldsha" != absent; then
    test "$(gzip -dc "$stage/$index.old.gz" | sha256sum | cut -d ' ' -f1)" = "$oldsha"
   fi
   phase=incoming-hash
   test "$(gzip -dc "$stage/$index.new.gz" | sha256sum | cut -d ' ' -f1)" = "$newsha"
   case "$path" in
    "$base/zigma-mode"|"$base/zigma-mode-backend"|"$base/zigma-free-service"|"$base/zigma-cron"|"$base/zigma-settings"|\
    "$base/activate"|"$base/firmware-profile.sh"|/root/bin/zigma-time-sync|\
    /root/etc/init.d/S04zigma-timezone|/root/etc/init.d/S51hoslam)
     phase=shell-syntax
     gzip -dc "$stage/$index.new.gz" > "$stage/script-syntax.sh"
     sh -n "$stage/script-syntax.sh"
     rm "$stage/script-syntax.sh";;
   esac
  done < "$stage/files.list"
  echo installing > "$stage/state"
  if ! replace_files new; then rollback; exit 1; fi
  echo ready > "$stage/state"
  unlock
  n=0
  while test "$n" -lt 240; do
   lock
   if test "$(cat "$stage/state")" = accepted; then echo done > "$stage/done"; exit 0; fi
   if test -f "$stage/rollback.now"; then rollback; exit 1; fi
   unlock
   sleep 1; n=$((n+1))
  done
  lock
  if test "$(cat "$stage/state")" = accepted; then echo done > "$stage/done"; exit 0; fi
  rollback
 ;;
 *) exit 2;;
esac
