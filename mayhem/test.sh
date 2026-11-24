#!/usr/bin/env bash
#
# mayhem/test.sh — known-answer tests for libmdbx through its own CLI tools (built by
# mayhem/build.sh with normal flags into mayhem/out/tests). Upstream's amalgamated tree ships
# no test suite, so each case loads a fixed dump, then asserts the exact records / counts /
# verdicts the tools report back (round-trips through the B-tree, dupsort tables, compacting
# copy, and the checker's detection of a corrupted file).
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

T="$SRC/mayhem/out/tests"
S="$SRC/mayhem/mdbx_load/testsuite"
for b in mdbx_load mdbx_dump mdbx_chk mdbx_stat mdbx_copy; do
  [ -x "$T/$b" ] || { echo "missing $T/$b — run mayhem/build.sh first" >&2; emit_ctrf mdbx-kat 0 1; exit 1; }
done
W="$(mktemp -d "${TMPDIR:-/tmp}/mdbx-test.XXXXXX")"
trap 'rm -rf "$W"' EXIT
PASS=0 FAIL=0
ok()  { echo "PASS: $1"; PASS=$((PASS+1)); }
bad() { echo "FAIL: $1"; FAIL=$((FAIL+1)); }
# body of a dump = the lines between HEADER=END and DATA=END
body() { sed -n '/^HEADER=END$/,/^DATA=END$/p' | sed '1d;$d'; }

# 1) print-format load -> print dump: records come back sorted, byte-exact.
"$T/mdbx_load" -q -n -f "$S/plain.txt" "$W/p.db"
got="$("$T/mdbx_dump" -q -n -p "$W/p.db" | body)"
exp="$(printf ' apple\n banana\n key1\n val1\n key2\n val2')"
[ "$got" = "$exp" ] && ok "print round-trip" || bad "print round-trip: got [$got]"

# 2) mdbx_stat reports the entry count of the main table.
"$T/mdbx_stat" -n "$W/p.db" | grep -Eq '^ *Entries: 3$' && ok "stat entries=3" || bad "stat entries"

# 3) bytevalue (hex) load -> hex dump, incl. non-printable bytes.
"$T/mdbx_load" -q -n -f "$S/hex.txt" "$W/h.db"
got="$("$T/mdbx_dump" -q -n "$W/h.db" | body)"
exp="$(printf ' 6b6579\n 76616c7565\n 6b657932\n 00ff10')"
[ "$got" = "$exp" ] && ok "hex round-trip" || bad "hex round-trip: got [$got]"

# 4) named dupsort table: duplicates kept and sorted, header flags preserved.
"$T/mdbx_load" -q -n -f "$S/dup.txt" "$W/d.db"
dump="$("$T/mdbx_dump" -q -n -s tbl "$W/d.db")"
got="$(printf '%s\n' "$dump" | body)"
exp="$(printf ' 61\n 31\n 61\n 32\n 62\n 33')"
[ "$got" = "$exp" ] && ok "dupsort round-trip" || bad "dupsort round-trip: got [$got]"
printf '%s\n' "$dump" | grep -qx 'dupsort=1' && ok "dupsort header" || bad "dupsort header"

# 5) larger tree with an overflow value: 301 entries, compacting copy is identical.
"$T/mdbx_load" -q -n -f "$S/big.txt" "$W/b.db"
"$T/mdbx_stat" -n "$W/b.db" | grep -Eq '^ *Entries: 301$' && ok "big entries=301" || bad "big entries"
"$T/mdbx_copy" -q -n -c "$W/b.db" "$W/bc.db"
a="$("$T/mdbx_dump" -q -n -p "$W/b.db" | body | sha256sum)"
c="$("$T/mdbx_dump" -q -n -p "$W/bc.db" | body | sha256sum)"
n="$("$T/mdbx_dump" -q -n -p "$W/bc.db" | body | wc -l)"
[ "$a" = "$c" ] && [ "$n" -eq 602 ] && ok "compact copy identical ($n lines)" || bad "compact copy: n=$n"

# 6) checker: clean db passes; the same db with its meta pages zeroed is rejected.
"$T/mdbx_chk" -q -n "$W/b.db" >/dev/null 2>&1 && ok "chk clean db" || bad "chk clean db"
cp "$W/b.db" "$W/x.db"
dd if=/dev/zero of="$W/x.db" bs=4096 count=3 conv=notrunc status=none
if "$T/mdbx_chk" -q -n "$W/x.db" >/dev/null 2>&1; then bad "chk corrupt db not detected"; else ok "chk corrupt db rejected"; fi

emit_ctrf mdbx-kat "$PASS" "$FAIL"
