#!/usr/bin/env bash
# Rebuilds the REVERSA sample binaries from source. Reproducible on Ubuntu 24.04 with:
#   apt-get install gcc gcc-aarch64-linux-gnu gcc-mingw-w64-x86-64 gcc-mingw-w64-i686
# All binaries are built with -O0 so the tutorials stay easy to follow.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p bin
for s in hello login; do
  gcc -O0 -fno-pie -no-pie -o "bin/${s}_x64.elf" "src/$s.c"
  aarch64-linux-gnu-gcc -O0 -o "bin/${s}_arm64.elf" "src/$s.c"
  x86_64-w64-mingw32-gcc -O0 -o "bin/${s}_x64.exe" "src/$s.c"
  i686-w64-mingw32-gcc -O0 -o "bin/${s}_x86.exe" "src/$s.c"
done
cp bin/login_x64.elf bin/login_x64_stripped.elf
strip bin/login_x64_stripped.elf
x86_64-w64-mingw32-strip -o bin/login_x64_stripped.exe bin/login_x64.exe
sha256sum bin/* > bin/SHA256SUMS
echo "Samples built:"; ls -l bin
