#!/bin/sh
set -eu
plugin=${1:?provide the path to the built plugin}
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
bin=${LLVM_BIN:-$(llvm-config --bindir)}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$bin/opt" -load-pass-plugin="$plugin" -passes='transition-split,verify' -S "$dir/check.ll" -o "$tmp/split.ll"
grep -q 'define internal .*@decode.transition.cold' "$tmp/split.ll"
group=$(sed -n 's/^define internal .*@decode.transition.cold(.*) #\([0-9][0-9]*\).*$/\1/p' "$tmp/split.ll")
grep -Eq "^attributes #${group} = .*null_pointer_is_valid" "$tmp/split.ll"
grep -q 'define internal .*@step.transition.cold' "$tmp/split.ll"
"$bin/clang" -O3 "$tmp/split.ll" "$dir/check.c" -o "$tmp/run"
"$tmp/run"
sed 's/%size = load i8/%size = load volatile i8/' "$dir/check.ll" > "$tmp/volatile.ll"
"$bin/opt" -load-pass-plugin="$plugin" -passes='transition-split,verify' -S "$tmp/volatile.ll" -o "$tmp/unchanged.ll"
if grep -q 'define internal .*@decode.transition.cold' "$tmp/unchanged.ll"; then exit 1; fi
"$bin/opt" -load-pass-plugin="$plugin" -passes='transition-runs,verify' -verify-each -S "$dir/test/runs.ll" -o "$tmp/runs.ll"
grep -q '^run.scan:' "$tmp/runs.ll"
sed 's/@scan(/@ref_scan(/' "$dir/test/runs.ll" > "$tmp/ref.ll"
"$bin/clang" -O3 "$tmp/runs.ll" "$tmp/ref.ll" "$dir/test/runs.c" -o "$tmp/test-runs"
"$tmp/test-runs"
"$bin/opt" -load-pass-plugin="$plugin" -passes='transition-runs,verify' -verify-each -S "$dir/test/effects.ll" -o "$tmp/effects.ll"
test "$(grep -c '^run.scan:' "$tmp/effects.ll")" = 2
grep -q '^run.write:' "$tmp/effects.ll"
sed -e 's/@scan(/@ref_scan(/' -e 's/@sentinel(/@ref_sentinel(/' "$dir/test/effects.ll" > "$tmp/ref-effects.ll"
"$bin/clang" -O3 "$tmp/effects.ll" "$tmp/ref-effects.ll" "$dir/test/effects.c" -o "$tmp/test-effects"
"$tmp/test-effects"
# Asking the pass about an unrelated module must also preserve its bitcode metadata.
sed '/^@threshold =/s/, !parser.transition.rows !0/, !example !0/' "$dir/test/effects.ll" > "$tmp/no-dfa.ll"
"$bin/opt" -passes=verify "$tmp/no-dfa.ll" -o "$tmp/no-dfa-ref.bc"
"$bin/opt" -load-pass-plugin="$plugin" -passes='transition-split,verify' "$tmp/no-dfa.ll" -o "$tmp/no-dfa-pass.bc"
cmp "$tmp/no-dfa-ref.bc" "$tmp/no-dfa-pass.bc"
