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
        NR == 1 { if ($1 != "rule" || NF != 26) exit 1; next }
        NF != 26 || $1 != rule || $2 != 42 || $3 != NR-1 ||
        $4 != "stored" || $5 != 0 || $6 != 100 || $7 < 1 ||
        $14 != "ok" || $8 != "yes" || $9 < -1 || $9 > 1 || $10 < 0 || $10 > $6 ||
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
# Every rule receives bit-for-bit identical inputs, even after Daydreaming.
"$BIN" "$ROOT/data/hopf01.dat" --compare --pattern 2,1 --noise 20 --seed 42 --verbose --csv compare.csv > compare.out
awk '
    /^Comparison input:/ {
        split($3, rule, "="); split($4, pattern, "=");
        file = "input." rule[2] "." pattern[2]; remaining = 10; next
    }
    remaining > 0 { print > file; remaining-- }
' compare.out
for rule in storkey pseudo-inverse daydreaming modern; do
    cmp "input.hebbian.1" "input.$rule.1"
    cmp "input.hebbian.2" "input.$rule.2"
done
awk -F, '
    BEGIN { split("hebbian storkey pseudo-inverse daydreaming modern", rules, " ") }
    NR == 1 { next }
    NF != 26 || $1 != rules[int((NR-2)/2)+1] || $2 != 42 ||
    $3 != (NR%2 == 0 ? 2 : 1) || $4 != "stored" || $5 != 20 ||
    $7 < 1 || $14 != "ok" { exit 1 }
    END { if (NR != 11) exit 1 }
' compare.csv
test "$(grep -c '^Comparison .*mean_overlap=' compare.out)" -eq 5
# Summary averages must agree with the exported per-pattern metrics.
awk '
    FNR == NR {
        if (FNR == 1) next;
        split($0, c, ","); n[c[1]]++; overlap[c[1]] += c[9];
        hamming[c[1]] += c[10]; iterations[c[1]] += c[7]; next
    }
    /^Comparison .*mean_overlap=/ {
        rule = $2; sub(/:$/, "", rule);
        split($3, o, "="); split($4, h, "="); split($7, i, "=");
        if ((o[2]-overlap[rule]/n[rule])^2 > 1e-8 ||
            (h[2]-hamming[rule]/n[rule])^2 > 1e-8 ||
            (i[2]-iterations[rule]/n[rule])^2 > 1e-8) exit 1
    }
' compare.csv compare.out
"$BIN" "$ROOT/data/hopf01.dat" --compare --pattern 2,1 --noise 20 --seed 42 --quiet --csv again.csv > out
test ! -s out
cut -d, -f1-11,14 compare.csv > first
cut -d, -f1-11,14 again.csv > second
cmp first second
"$BIN" "$ROOT/data/hopf01.dat" --compare --noise 0 --seed 42 --quiet --csv all.csv
test "$(wc -l < all.csv)" -eq 36
"$BIN" "$ROOT/data/hopf01.dat" "$ROOT/data/hopf01noisy.dat" --compare --seed 42 --quiet --csv mode2.csv
awk -F, 'NR > 1 && ($4 != "noisy" || $5 != -1) { exit 1 } END { if (NR != 21) exit 1 }' mode2.csv
expect_failure dependent.dat --compare --noise 0 --seed 42 --quiet --csv dependent.csv
awk -F, '
    $1 == "pseudo-inverse" { if (NF != 26 || $14 != "failed" || $9 != "") exit 1; failed++ }
    $1 == "modern" && $14 == "ok" { modern++ }
    END { if (failed != 1 || modern != 2) exit 1 }
' dependent.csv
expect_failure "$ROOT/data/hopf01.dat" --compare --noise 0 --rule hebbian
expect_failure "$ROOT/data/hopf01.dat" --compare --noise 0 --load-weights missing.bin
expect_failure "$ROOT/data/hopf01.dat" --compare --noise 0 --save-weights weights.bin
expect_failure "$ROOT/data/hopf01.dat" --compare --noise 0 --output patterns.dat
expect_failure "$ROOT/data/hopf01.dat" --compare
if test -e /dev/full; then
    expect_failure "$ROOT/data/hopf01.dat" --compare --pattern 1 --noise 0 --csv /dev/full
fi
for list in ',' '1,' ',1' '1,,2' '0' '-1' '184467440737095516160'; do
    expect_failure "$ROOT/data/hopf01.dat" --compare --pattern "$list" --noise 0
done
# Repeated selections remain separate trials for each rule.
"$BIN" "$ROOT/data/hopf01.dat" --compare --pattern 1,1 --noise 20 --seed 42 --quiet --csv repeated.csv
awk -F, 'NR > 1 && ($3 != 1 || $14 != "ok") { exit 1 } END { if (NR != 11) exit 1 }' repeated.csv
# Noise sweeps: level/trial labels, aggregation and deterministic results.
printf '1 8 1\n*.*.*.*.\n' > small.dat
"$BIN" small.dat --compare --sweep 0:100:50 --trials 2 --seed 42 --csv sweep.csv > sweep.out
awk -F, '
    NR == 1 { if ($15 != "trial") exit 1; next }
    NF != 26 || $5 != int((NR-2)/10)*50 ||
    $15 != int((NR-2)%10/5)+1 || $14 != "ok" { exit 1 }
    END { if (NR != 31) exit 1 }
' sweep.csv
test "$(grep -c '^Sweep ' sweep.out)" -eq 15
grep -q '^Sweep hebbian noise=0: samples=2 correct=2 correct_percent=100.00' sweep.out
"$BIN" small.dat --compare --sweep 0:100:50 --trials 2 --seed 42 --quiet --csv sweep2.csv
cut -d, -f1-11,14-15 sweep.csv > first
cut -d, -f1-11,14-15 sweep2.csv > second
cmp first second
"$BIN" small.dat --rule modern --sweep 0:25:10 --trials 3 --seed 42 --quiet --csv single-sweep.csv
awk -F, 'NR > 1 && ($1 != "modern" || $5 != int((NR-2)/3)*10 || $15 != (NR-2)%3+1) { exit 1 } END { if (NR != 10) exit 1 }' single-sweep.csv
for range in '0:100:0' '100:0:10' '-1:10:1' '0:101:1' '0:100' '0:100:10junk' '0:100:999999999999999999'; do
    expect_failure small.dat --sweep "$range"
done
for trials in 0 -1 junk 999999999999999999999; do
    expect_failure small.dat --sweep 0:10:10 --trials "$trials"
done
expect_failure small.dat --trials 2
expect_failure small.dat --sweep 0:10:10 --noise 20
expect_failure small.dat small.dat --sweep 0:10:10
expect_failure dependent.dat --compare --sweep 0:10:10 --trials 2 --seed 42 --quiet --csv sweep-failed.csv
awk -F, '$14 == "failed" { if (NF != 26 || $1 != "pseudo-inverse") exit 1; n++ } END { if (n != 4) exit 1 }' sweep-failed.csv
printf '4 4 1\n*.*.\n.*.*\n*.*.\n.*.*\n' > grid.dat
for corruption in flip erase block left right top bottom; do
    "$BIN" grid.dat --compare --corruption "$corruption" --noise 50 --seed 42 --csv masks.csv --verbose > masks.out
    awk -F, -v kind="$corruption" '
        NR > 1 && (NF != 26 || $16 != kind || $17 < 1 || $17 > 16) { exit 1 }
        NR == 2 { affected = $17 }
        NR > 2 && $17 != affected { exit 1 }
        END { if (NR != 6) exit 1 }
    ' masks.csv
    awk '
        /^Comparison input:/ { split($3, r, "="); file="mask." r[2]; rows=4; next }
        rows > 0 { print > file; rows-- }
    ' masks.out
    for rule in storkey pseudo-inverse daydreaming modern; do
        cmp mask.hebbian "mask.$rule"
    done
    if test "$corruption" != flip; then
        grep -q '?' mask.hebbian
    fi
done
"$BIN" grid.dat --rule modern --pattern 1 --corruption left --noise 50 --seed 42 --verbose --csv half.csv > half.out
grep -Fq '??*.' half.out
grep -Fq '??.*' half.out
awk -F, 'NR == 2 && ($16 != "left" || $17 != 8 || $9 != 1 || $10 != 0) { exit 1 }' half.csv
"$BIN" grid.dat --compare --corruption block --sweep 0:100:50 --trials 2 --seed 42 --quiet --csv block-sweep.csv
"$BIN" grid.dat --compare --corruption block --sweep 0:100:50 --trials 2 --seed 42 --quiet --csv block-sweep2.csv
cut -d, -f1-11,14-17 block-sweep.csv > first
cut -d, -f1-11,14-17 block-sweep2.csv > second
cmp first second
awk -F, 'NR > 1 && ($16 != "block" || ($5 == 0 && $17 != 0) || ($5 == 100 && $17 != 16)) { exit 1 } END { if (NR != 31) exit 1 }' block-sweep.csv
expect_failure grid.dat --pattern 1 --noise 50 --corruption unknown
expect_failure grid.dat --corruption erase
expect_failure grid.dat grid.dat --pattern 1 --corruption erase
# Unknown input pixels are experimental in-memory values, not a file format change.
printf '1 2 1\n*?\n' > unknown.dat
expect_failure unknown.dat --pattern 1 --noise 0
"$BIN" small.dat --rule hebbian --pattern 1 --noise 0 --seed 42 --quiet --csv attractor.csv
awk -F, 'NR == 2 && (NF != 26 || $18 != "correct" || $19 != 1 || $20 != 1 || $21 != 1 || $22 != "") { exit 1 }' attractor.csv
"$BIN" small.dat --rule hebbian --pattern 1 --noise 100 --seed 42 --quiet --csv inverse.csv
awk -F, 'NR == 2 && ($18 != "inverse" || $19 != 1 || $21 != -1) { exit 1 }' inverse.csv
printf '1 8 2\n********\n****....\n' > capacity.dat
"$BIN" capacity.dat --capacity --compare --noise 0 --trials 2 --seed 42 --csv capacity.csv > capacity.out
awk -F, 'NR > 1 && (NF != 26 || $26 != (NR <= 11 ? 1 : 2) || $3 > $26 || $18 != "correct") { exit 1 } END { if (NR != 31) exit 1 }' capacity.csv
test "$(grep -c '^Capacity ' capacity.out)" -eq 10
"$BIN" capacity.dat --capacity --compare --noise 0 --trials 2 --seed 42 --quiet --csv capacity2.csv
cut -d, -f1-11,14-26 capacity.csv > first
cut -d, -f1-11,14-26 capacity2.csv > second
cmp first second
expect_failure dependent.dat --capacity --compare --noise 0 --trials 1 --seed 42 --quiet --csv capacity-fail.csv
awk -F, '$14 == "failed" { if ($26 != 2 || NF != 26) exit 1; n++ } END { if (n != 1) exit 1 }' capacity-fail.csv
expect_failure capacity.dat --capacity --pattern 1 --noise 0
expect_failure capacity.dat --capacity --sweep 0:10:10
expect_failure capacity.dat capacity.dat --capacity --noise 0
"$BIN" capacity.dat --capacity --rule modern --corruption left --noise 50 --trials 1 --quiet --csv capacity-mask.csv
awk -F, 'NR > 1 && ($1 != "modern" || $16 != "left") { exit 1 } END { if (NR != 4) exit 1 }' capacity-mask.csv
echo 'Experiment contracts passed.'
