**1\. Bouwen en tests draaien**

```
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Verwacht: alle drie tests slagen.

**2\. Reproduceerbaarheid controleren**

```
./bin/hopfieldann data/hopf01.dat --rule storkey \
  --pattern 1 --noise 20 --seed 42 --verbose > /tmp/run1.txt

./bin/hopfieldann data/hopf01.dat --rule storkey \
  --pattern 1 --noise 20 --seed 42 --verbose > /tmp/run2.txt

diff /tmp/run1.txt /tmp/run2.txt
```

Verwacht: `diff` geeft niets terug; beide runs zijn identiek.

**3\. CSV-export bekijken**

```
./bin/hopfieldann data/hopf01.dat --rule storkey \
  --pattern 1,3,5 --noise 20 --seed 42 \
  --quiet --csv /tmp/results.csv

column -s, -t /tmp/results.csv
```

Verwacht: een kopregel en drie resultaten, met onder andere overlap, Hamming-afstand, iteraties en rekentijden.

**4\. Alle vijf leerregels vergelijken**

```
./bin/hopfieldann data/hopf01.dat \
  --compare --noise 20 --seed 42 \
  --csv /tmp/comparison.csv
```

Verwacht: per leerregel een samenvatting en in de CSV **35 resultaatregels**: zeven patronen × vijf regels. Voor een kortere proef voeg je `--pattern 1,2` toe.

Bij de beoordeling: **overlap 1 en Hamming 0 betekenen perfecte herkenning**. `converged=yes` betekent alleen dat het netwerk tot rust kwam. Rekentijden mogen tussen identieke runs verschillen.