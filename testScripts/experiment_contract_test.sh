#!/usr/bin/env bash
set -euo pipefail
BIN="$(realpath "$1")"
ROOT="$(pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"
# A local config isolates these checks from user defaults.
printf '# test defaults\n' > .hopfieldrc
for rule in hebbian storkey pseudo-inverse daydreaming modern; do
    "$BIN" "$ROOT/data/hopf01.dat" --rule "$rule" --pattern 1,2 --noise 30 --seed 42 --verbose > first
    "$BIN" "$ROOT/data/hopf01.dat" --rule "$rule" --pattern 1,2 --noise 30 --seed 42 --verbose > second
    cmp first second
done
for seed in -1 +1 12junk 184467440737095516160 ''; do
    if "$BIN" "$ROOT/data/hopf01.dat" --seed "$seed" > out 2>&1; then
        echo "Invalid seed accepted: $seed"; exit 1
    fi
    grep -q 'invalid seed' out
done
printf 'seed = 17\n' > .hopfieldrc
"$BIN" "$ROOT/data/hopf01.dat" --pattern 1 --noise 0 > out
grep -q 'Random seed: 17' out
"$BIN" "$ROOT/data/hopf01.dat" --pattern 1 --noise 0 --seed 0 > out
grep -q 'Random seed: 0' out
printf 'seed = 184467440737095516160\n' > .hopfieldrc
if "$BIN" "$ROOT/data/hopf01.dat" --pattern 1 --noise 0 > out 2>&1; then
    echo 'Invalid config seed accepted'; exit 1
fi
grep -q 'invalid config seed' out
printf '# test defaults\n' > .hopfieldrc
echo 'Experiment contracts passed.'
