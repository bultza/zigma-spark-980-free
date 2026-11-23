#!/bin/sh
# Sourced by the journaled installer/repair; replace() records each changed file.
configure_support() {
 mkdir -p /root/etc/crontabs
 for name in S01time S03zigma-hostname S04zigma-timezone S96ntpsync S97crond; do
  replace "$support_dir/$name" "/root/etc/init.d/$name" 700
 done
 replace "$support_dir/zigma-time-sync" /root/bin/zigma-time-sync 700
 # Navigation does not source /etc/profile. Keep the native launcher intact
 # except for selecting the same persistent timezone before it starts children.
 nav=/root/etc/init.d/S51hoslam
 if test -f "$nav"; then
  test ! -L "$nav"
  if ! grep -qx '# zigma-selected-timezone-v1' "$nav"; then
   test "$(head -n 1 "$nav")" = '#!/bin/sh'
   { head -n 1 "$nav"
     printf '%s\n' '# zigma-selected-timezone-v1' 'if [ -f /root/etc/localtime ]; then TZ=:/root/etc/localtime; export TZ; fi'
     tail -n +2 "$nav"
   } > "$stage/hoslam-timezone"
   replace "$stage/hoslam-timezone" "$nav" 700
  fi
 fi
 # Keep jobs verbatim and add only a missing hourly time-sync job.
 if test -f /root/etc/crontabs/root; then cat /root/etc/crontabs/root > "$stage/crontab"; else : > "$stage/crontab"; fi
 if ! grep -q '^[^#].*zigma-time-sync daily' "$stage/crontab"; then
  printf '\n17 * * * * /root/bin/zigma-time-sync daily >/dev/null 2>&1\n' >> "$stage/crontab"
 fi
 replace "$stage/crontab" /root/etc/crontabs/root 600
 printf '%s\n' "$install_hostname" > "$stage/hostname"
 replace "$stage/hostname" /root/hostname 644
 # ash reads ~/.profile after /etc/profile for interactive login shells.
 if test -f /root/.profile; then cat /root/.profile > "$stage/profile"; else : > "$stage/profile"; fi
 if ! grep -qx '# zigma-modular-prompt-v1' "$stage/profile"; then
  cat >> "$stage/profile" <<'PROMPT'

# zigma-modular-prompt-v1
case "$-" in *i*) PS1='\u@\h:\w\$ '; export PS1;; esac
PROMPT
 fi
 replace "$stage/profile" /root/.profile 600
}
