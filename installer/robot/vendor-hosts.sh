#!/bin/sh
# zigma-vendor-hosts-v1: local DNS overrides, not a complete Internet firewall.
set -eu
umask 077
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
base=/root/.zigma-hosts
init=/root/etc/init.d/S03zigma-hosts
fail() { echo "ERROR: $*" >&2; exit 1; }
test "$(id -u)" = 0 || fail 'Root required'
test "$(sha256sum /etc/init.d/S70user | cut -d ' ' -f 1)" = dcfe47f7fda31f66bcef490719aff2eb9ac635bfd8a954162367e03acedad2d4 || fail 'Unreviewed boot dispatcher; SSH retained'
test -f /etc/hosts && test ! -L /etc/hosts || fail 'Unexpected hosts path; preserved'
test ! -L "$base" && test ! -L "$init" || fail 'Conflicting symlink; preserved'
work=$(mktemp -d /tmp/zigma-hosts.XXXXXX)
trap 'rm -rf "$work"' EXIT
cat >"$work/domains" <<'DOMAINS'
iot.grit-cloud.cn
iot.grit-cloud.com
usngiot.grit-cloud.com
eungiot.grit-cloud.com
apngiot.grit-cloud.com
redmond.iot.grit-cloud.com
weback.iot.grit-cloud.com
www.grit-tech.link
56mg2g2mi4.execute-api.cn-north-1.amazonaws.com.cn
www.grit-cs.com
5p4ao0lzg8.execute-api.cn-north-1.amazonaws.com.cn
a6vxdykle8oe1.ats.iot.cn-north-1.amazonaws.com.cn
a9vvblgf32b6o-ats.iot.us-east-1.amazonaws.com
a9vvblgf32b6o-ats.iot.ap-southeast-1.amazonaws.com
a9vvblgf32b6o-ats.iot.eu-central-1.amazonaws.com
DOMAINS
cat >"$work/init" <<'INIT'
#!/bin/sh
# zigma-vendor-hosts-v1
set -eu
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
case "${1:-}" in
 start|restart|reload)
  source=/root/.zigma-hosts/hosts
  test -f "$source" && test ! -L "$source"
  if awk '$2=="/etc/hosts" {found=1} END{exit !found}' /proc/mounts;then
   cmp -s "$source" /etc/hosts || { echo 'Conflicting hosts mount; preserved' >&2;exit 1; }
  else
   mount -o bind "$source" /etc/hosts
  fi
  cmp -s "$source" /etc/hosts
 ;;
 stop) :;;
 *) exit 2;;
esac
INIT
if test -e "$base" || test -e "$init";then
 test -d "$base" && test -f "$base/hosts.original" && test -f "$base/hosts" && test -f "$init" || fail 'Incomplete hosts setup; preserved'
 for file in "$base/hosts.original" "$base/hosts";do test ! -L "$file" || fail 'Conflicting hosts symlink';done
 cmp -s "$work/init" "$init" || fail 'Different startup hook; preserved'
 original=$base/hosts.original
else
 original=/etc/hosts
fi
# Remove aliases for these exact domains only, keeping unrelated names/comments.
awk 'NR==FNR {blocked[tolower($1)]=1;next}
 {line=$0;sub(/#.*/,"",line);n=split(line,a);hit=0
  for(i=2;i<=n;i++)if(tolower(a[i]) in blocked)hit=1
  if(!hit){print;next}
  result=a[1];kept=0
  for(i=2;i<=n;i++)if(!(tolower(a[i]) in blocked)){result=result " " a[i];kept++}
  if(kept)print result
 }' "$work/domains" "$original" >"$work/hosts"
echo '# BEGIN ZIGMA VENDOR LOOPBACK' >>"$work/hosts"
while read -r name;do
 printf '127.0.0.1 %s\n::1 %s\n' "$name" "$name" >>"$work/hosts"
done <"$work/domains"
echo '# END ZIGMA VENDOR LOOPBACK' >>"$work/hosts"
if test -e "$base";then
 cmp -s "$work/hosts" "$base/hosts" || fail 'Hosts contents differ from reviewed setup; preserved'
else
 awk '$2=="/etc/hosts" {found=1} END{exit found}' /proc/mounts || fail 'Existing hosts mount; preserved'
 mkdir -m 700 "$base"
 cp -p /etc/hosts "$base/hosts.original"
 cp "$work/hosts" "$base/hosts";chmod 644 "$base/hosts"
 cp "$work/init" "$init";chmod 700 "$init"
fi
"$init" start
cmp -s "$work/hosts" /etc/hosts || fail 'Hosts activation failed'
sync
echo 'HOSTS PASS: 15 vendor domains redirected to 127.0.0.1 and ::1; persistent startup configured.'
echo 'Direct IP 203.107.1.33 and existing connections are not blocked. No reboot or Wi-Fi change sent.'
