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
# CSV schema, numeric metrics and reproducibility (timings excluded).
for rule in hebbian storkey pseudo-inverse daydreaming modern; do
    "$BIN" "$ROOT/data/hopf01.dat" --rule "$rule" --pattern 1,2 --noise 0 --seed 42 --quiet --verbose --csv first.csv > out
    test ! -s out
    awk -F, -v rule="$rule" '
        NR == 1 { if ($1 != "rule" || NF != 13) exit 1; next }
        NF != 13 || $1 != rule || $2 != 42 || $3 != NR-1 ||
        $4 != "stored" || $5 != 0 || $6 != 100 || $7 < 1 ||
        $8 != "yes" || $9 < -1 || $9 > 1 || $10 < 0 || $10 > $6 ||
        ($9 - (1 - 2*$10/$6))^2 > 1e-20 || $12 < 0 || $13 < 0 { exit 1 }
        END { if (NR != 3) exit 1 }
    ' first.csv
    "$BIN" "$ROOT/data/hopf01.dat" --rule "$rule" --pattern 1,2 --noise 0 --seed 42 --quiet --csv second.csv
    cut -d, -f1-11 first.csv > first
    cut -d, -f1-11 second.csv > second
    cmp first second
done
"$BIN" "$ROOT/data/hopf01.dat" "$ROOT/data/hopf01noisy.dat" --pattern 1 --seed 42 --quiet --csv noisy.csv
awk -F, 'NR == 2 { if ($4 != "noisy" || $5 != -1) exit 1 } END { if (NR != 2) exit 1 }' noisy.csv
expect_failure() {
    local code=0
    "$BIN" "$@" > out 2>&1 || code=$?
    test "$code" -eq 1
}
expect_failure "$ROOT/data/hopf01.dat" --csv missing.csv
expect_failure "$ROOT/data/hopf01.dat" --pattern 1 --noise 0 --csv missing/out.csv
expect_failure "$ROOT/data/hopf01.dat" --pattern 1 --noise 0 --csv "$ROOT/data/hopf01.dat"
expect_failure "$ROOT/data/hopf01.dat" --pattern 1 --noise 0 --csv same --output same
expect_failure "$ROOT/data/hopf01.dat" --pattern 1 --noise 0 --load-weights missing.bin
expect_failure "$ROOT/data/hopf01.dat" --pattern 1 --noise 0 --output missing/out.dat
printf '1 2 2\n**\n**\n' > dependent.dat
expect_failure dependent.dat --rule pseudo-inverse --pattern 1 --noise 0 --csv failed.csv
if test -e /dev/full; then
    expect_failure "$ROOT/data/hopf01.dat" --pattern 1 --noise 0 --csv /dev/full
fi
echo 'Experiment contracts passed.'
