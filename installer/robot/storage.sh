#!/bin/sh
# S02zigma-storage V1.1 (modular installer adapter)
# Storage adapter for the reviewed 32 MiB overlay layouts: 8 MiB rootfs +
# 16 MiB firmware (original), or 4 MiB rootfs + 20 MiB firmware (firmware 4.5.56).
set -eu
case "${1:-}" in start) :;;stop) exit 0;;*) exit 2;;esac
test "$(cat /sys/class/mtd/mtd3/name)" = rootfs
test "$(cat /sys/class/mtd/mtd5/name)" = firmware
case "$(cat /sys/class/mtd/mtd3/size):$(cat /sys/class/mtd/mtd5/size)" in
 8388608:16777216|4194304:20971520) :;;
 *) exit 1;;
esac
awk '$1=="/dev/mtdblock5" && $2=="/root/firmware/arobot" && $3=="jffs2" {ok=1} END{exit !ok}' /proc/mounts
store=/root/firmware/arobot/zigma-free
test -d "$store" && test ! -L "$store"
mount -o remount,rw /root/firmware/arobot
bind_dir() {
 test -d "$1" && test ! -L "$1" && test ! -L "$2"
 mkdir -p "$2"
 test ! "$1" -ef "$2" || return 0
 ! awk -v p="$2" '$2==p {found=1} END{exit !found}' /proc/mounts
 test -z "$(ls -A "$2")"
 mount -o bind "$1" "$2"
 test "$1" -ef "$2"
}
bind_dir "$store" /root/zigma-free
bind_dir "$store/data/master-maps" /root/zigma-free-master-maps
