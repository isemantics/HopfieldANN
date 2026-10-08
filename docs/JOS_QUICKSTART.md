# Voor Jos: van een beschadigd patroon naar een herinnering

De C-app voert alle simulaties uit. De losse viewer opent de resultaten
achteraf; hij start geen training en verandert geen bestanden.
Alle onderstaande commando's voer je uit **vanuit de projectmap**.

## 1. Eenmalig bouwen

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
mkdir -p /tmp/hopfield-demo
```

Voor de viewer heb je Python 3 met Tkinter en een grafische desktop nodig.
Op Ubuntu: `sudo apt install python3-tk`. Met WSL 2/WSLg verschijnt het
venster gewoon op het Windows-bureaublad. `python3 -m tkinter` opent een
klein testvenster. De CLI zelf heeft geen Python nodig.

## 2. Begin met een letter die zich pixel voor pixel herstelt

```bash
./bin/hopfieldann data/hopf01.dat --rule hebbian \
  --pattern 1 --noise 30 --seed 42 \
  --trace-detail neuron --record /tmp/hopfield-demo/letter.jsonl

python3 viewer/viewer.py /tmp/hopfield-demo/letter.jsonl
```

Klik **Play**. Kies **1 fps** om iedere verandering te volgen of **25 fps**
om sneller door de reconstructie te gaan. **Loop** herhaalt de opname.
De schuif en pijltjes werken ook achteruit. Selecteren van een andere run
stopt de animatie. Op dezelfde build/C-runtime geeft de seed dezelfde run.

Vink **Show differences (red)** aan: rode randen markeren pixels die afwijken
van de links getoonde referentie. Dit werkt op zowel de verstoorde invoer als
het huidige recallbeeld. De teller onder de beelden verandert mee tijdens
het afspelen. Kies een andere **learned memory** om te zien of het netwerk
misschien naar een andere opgeslagen herinnering toegaat. Terug naar
**Reference** herstelt het oorspronkelijke vergelijkingsbeeld.

Zwart is +1, wit is −1 en amber is ontbrekende informatie (0). Rode randen
geven verschillen aan zonder die pixelwaarden te veranderen. Een verschil
met een andere gekozen herinnering betekent niet automatisch een fout ten
opzichte van het oorspronkelijke doel.

## 3. Vergelijk vijf regels op hetzelfde ontbrekende beeldstuk

```bash
./bin/hopfieldann data/hopf01.dat --compare --pattern 1,2 \
  --corruption block --noise 30 --seed 42 --trace-detail neuron \
  --record /tmp/hopfield-demo/block.jsonl

python3 viewer/viewer.py /tmp/hopfield-demo/block.jsonl
```

Selecteer de regels links om de verschillende reconstructies te bekijken.
Iedere leerregel krijgt binnen een vergelijking exact dezelfde beschadigde
invoer. Probeer ook `--corruption left --noise 50`: dan ontbreekt de
linkerhelft. Modern Hopfield verandert de hele toestand tegelijk; daar zijn
er dus geen individuele neuronstapjes. Daydreaming kost meer trainingstijd.

## 4. Een echte grafiek van herkenning tegenover ruis

```bash
./bin/hopfieldann data/hopf01.dat --compare --pattern 1,2 \
  --sweep 0:60:10 --trials 3 --seed 42 \
  --record /tmp/hopfield-demo/noise.jsonl \
  --csv /tmp/hopfield-demo/noise.csv

python3 viewer/viewer.py /tmp/hopfield-demo/noise.jsonl
```

Kies **Recognition / noise**. De zeven x-waarden zijn 0, 10, …, 60 procent
ruis. Iedere regel krijgt zijn eigen kleur; de legenda koppelt kleuren aan
regels. Per punt zijn er hier zes metingen: twee patronen maal drie
herhalingen. Iedere herhaling traint opnieuw. Dit is een kleine demonstratie,
geen statistisch sluitend onderzoek; verhoog `--trials` voor meer metingen.

## 5. Het geheugen steeds voller stoppen

```bash
./bin/hopfieldann data/hopf01.dat --capacity --compare \
  --noise 20 --trials 2 --seed 42 \
  --record /tmp/hopfield-demo/capacity.jsonl \
  --csv /tmp/hopfield-demo/capacity.csv

python3 viewer/viewer.py /tmp/hopfield-demo/capacity.jsonl
```

Kies **Recognition / stored memories**. De CLI leert eerst alleen patroon 1,
dan de eerste twee, enzovoort, en test telkens alle op dat moment geleerde
patronen. De bestandsvolgorde en de gekozen patronen beïnvloeden de curve.
Selecteer een run om de details en de daar geleerde herinneringen te zien.

## Welke gegevens heeft iedere grafiek nodig?

| Grafiek | Invoer | Als er geen curve verschijnt |
|---|---|---|
| Energy / iteration | JSONL uit `--record` | CSV bewaart geen energieverloop; een mislukte training heeft geen recallstappen. |
| Recognition / noise | JSONL of CSV, liefst uit `--sweep` | Een gewone run heeft slechts één ruisniveau: dat is één punt, geen curve. |
| Recognition / stored memories | JSONL of CSV, liefst uit `--capacity` | Zonder capaciteitsexperiment is er maar één geheugengrootte. |

De viewer tekent ook afzonderlijke punten en legt uit hoe je meer x-waarden
krijgt. De ruisgrafiek houdt de geheugengrootte van de geselecteerde run vast;
de capaciteitsgrafiek houdt diens ruisniveau vast. Zo worden onvergelijkbare
metingen niet op één lijn samengevoegd. Runs met een ruisbestand als referentie
hebben geen bekend schoon doel en tellen niet mee in herkenningsgrafieken.

Je kunt bijvoorbeeld ook `python3 viewer/viewer.py /tmp/hopfield-demo/noise.csv`
gebruiken. Dan werken de statistiekgrafieken, maar ontbreken de pixelbeelden,
animatie en energiegeschiedenis.

## De uitkomst begrijpen

- **Hamming 0 / overlap 1**: exacte reconstructie van het referentiepatroon.
- **correct**: de bedoelde opgeslagen herinnering; **other**: een andere.
- **inverse**: het negatief van een opgeslagen herinnering.
- **unknown**: een geconvergeerde toestand die niet exact overeenkomt.
- **not_converged**, **failed** of **unfinished**: geen succesvolle afronding.
- `converged=yes` betekent tot rust gekomen, niet noodzakelijk juist herkend.

Energie wordt na een volledige sweep gemeten, ook bij neuronopnames.
Voor Modern Hopfield hoort energie bij de continue toestand en toont de viewer
binaire beelden. Vergelijk energiegetallen niet rechtstreeks tussen leerregels.
Een normaal netwerk kan in enkele sweeps klaar zijn; neuronopnames laten de
veranderingen *binnen* die sweeps zien. Opnamen voegen I/O-tijd toe en kunnen
bij grote experimenten groot worden. Voor overzichtsgrafieken is de standaard
sweep-opname daarom meestal voldoende.

Meer details: [viewerhandleiding](../viewer/README.md) en
[CLI-documentatie](../README.md).
