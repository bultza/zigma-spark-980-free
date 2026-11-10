#!/bin/sh
# Invoked by the Makefile with the pinned ARM musl compiler.
# Output stays outside source by default. Does not connect to any robot.
set -eu
robot_source=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build=$1
arm_prefix=$2
mkdir -p "$build"
cd "$build"
build=$(pwd)
python3 - <<'PY'
import hashlib,pathlib,urllib.request
sources=[
 ('https://musl.libc.org/releases/musl-1.2.6.tar.gz','d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a'),
 ('https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2','a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6'),
 ('https://curl.se/download/curl-8.22.0.tar.xz','f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7')]
for url,expected in sources:
 p=pathlib.Path(url.rsplit('/',1)[1])
 if not p.exists(): urllib.request.urlretrieve(url,p)
 if hashlib.sha256(p.read_bytes()).hexdigest() != expected:
  raise SystemExit(f'Source digest mismatch: {p}')
PY
test -d mbedtls-3.6.7 || tar -xf mbedtls-3.6.7.tar.bz2
test -d curl-8.22.0 || tar -xf curl-8.22.0.tar.xz
python3 "$robot_source/tools/generate-version-catalog.py"
python3 "$robot_source/telegram/patch-curl-version.py" "$build/curl-8.22.0"
export TZ=UTC
make -C mbedtls-3.6.7/library -j4 CC="$arm_prefix-gcc" AR="$arm_prefix-ar" CFLAGS='-Os -march=armv5te'
cd curl-8.22.0
./configure --host=arm-buildroot-linux-musleabi --disable-shared --enable-static --with-mbedtls \
 --without-libpsl --without-zlib --without-brotli --without-zstd --without-libidn2 \
 --without-libssh2 --without-nghttp2 --disable-ldap --disable-ldaps --disable-ftp \
 --disable-file --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
 --disable-imap --disable-smtp --disable-rtsp --disable-gopher --disable-smb \
 --disable-mqtt --disable-manual --disable-threaded-resolver \
 CC="$arm_prefix-gcc" AR="$arm_prefix-ar" RANLIB="$arm_prefix-ranlib" CFLAGS='-Os -march=armv5te -ffunction-sections -fdata-sections' \
 CPPFLAGS="-I$build/mbedtls-3.6.7/include" \
 LDFLAGS="-static -L$build/mbedtls-3.6.7/library -Wl,--gc-sections" CURL_LDFLAGS_BIN=-all-static
make -j4 CURL_LDFLAGS_BIN=-all-static
cp src/curl "$build/zigma-curl-arm"
"$arm_prefix-strip" "$build/zigma-curl-arm"
python3 "$robot_source/tools/record-build.py" "$build/zigma-curl-arm" zigma-curl-arm /root/zigma-free/zigma-curl-arm "$build/zigma-curl-build.json"
cd "$build"
python3 - <<'PY'
import hashlib,pathlib,urllib.request
urllib.request.urlretrieve('https://curl.se/ca/cacert.pem','telegram-ca.pem')
expected=urllib.request.urlopen('https://curl.se/ca/cacert.pem.sha256').read().decode().split()[0]
bundle = pathlib.Path('telegram-ca.pem').read_bytes()
if hashlib.sha256(bundle).hexdigest() != expected:
    raise SystemExit('CA bundle digest mismatch')
source_hash, = [line.split()[-1].decode() for line in bundle.splitlines()
                if line.startswith(b'## SHA256: ')]
urllib.request.urlretrieve('https://curl.se/ca/certdata.txt', 'certdata.txt')
if hashlib.sha256(pathlib.Path('certdata.txt').read_bytes()).hexdigest() != source_hash:
    raise SystemExit('CA bundle and Mozilla source do not match; retry the download')
pathlib.Path('SHA256SUMS').write_text(''.join(hashlib.sha256(pathlib.Path(n).read_bytes()).hexdigest()+'  '+n+'\n' for n in ['zigma-curl-arm','telegram-ca.pem']))
PY
# curl and Mbed TLS licenses must accompany redistributed binaries.
cp curl-8.22.0/COPYING curl-LICENSE
cp mbedtls-3.6.7/LICENSE mbedtls-LICENSE
file zigma-curl-arm
