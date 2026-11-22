#!/usr/bin/env sh
# Headless end-to-end check used by CI: analyze every sample, write reports, run the example script.
# Usage: scripts/headless_check.sh [output-dir]
set -eu
OUT="${1:-headless-out}"
mkdir -p "$OUT"
for f in samples/bin/*.elf samples/bin/*.exe; do
  name=$(basename "$f")
  reversa analyze "$f" --report "$OUT/$name.md" --project "$OUT/$name.rvproj" --no-plugins --fail-on-error > "$OUT/$name.txt"
done
reversa analyze samples/bin/login_x64.elf --report "$OUT/login.html" --no-plugins > /dev/null
reversa --demo --no-gui > "$OUT/demo.txt"
reversa script examples/scripts/strcmp_callers.py samples/bin/login_x64.elf | tee "$OUT/strcmp_callers.txt"
grep -q check_password "$OUT/strcmp_callers.txt"
grep -q check_user "$OUT/strcmp_callers.txt"
echo "headless check OK ($OUT)"
