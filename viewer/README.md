# Offline Hopfield viewer

This is a separate, read-only Python/Tkinter application. It opens results;
it never starts simulations, trains networks, invokes a shell, or writes to
recordings. The C17 CLI has no Python or Tk dependency.

## Quick start

From the repository root:

```bash
./bin/hopfieldann data/hopf01.dat --compare --pattern 1,2 \
  --corruption left --noise 50 --seed 42 \
  --record /tmp/half-letter.jsonl --csv /tmp/half-letter.csv

python3 viewer/viewer.py /tmp/half-letter.jsonl
```

Requires Python 3 (3.8+) with Tkinter and a graphical desktop. On
Debian/Ubuntu, the optional GUI package is installed with
`sudo apt-get install python3-tk`. No pip dependencies are needed. Open the
viewer without an argument to select a recording or CSV in its file dialog.

- Select a row to inspect a run. Columns distinguish rule, learned memory
  count, noise, repetition, target and outcome. Failed and unfinished runs
  remain visible.
- The three rasters show the reference, corrupted input and selected recall
  step. Use the slider or arrow buttons to go forward/backward; zero is the
  initial input. Amber pixels represent missing information (neutral zero).
- The memory selector inspects any memory learned at that capacity level.
  Return it to **Reference** to see the run's target again. A noisy-file run
  explicitly labels its reference as noisy rather than as a clean target.
- The energy graph follows the selected run. Recognition graphs aggregate
  completed clean-target runs. Selecting a row fixes the other dimension:
  noise curves keep its memory count; capacity curves keep its noise level.
  Different rules/corruptions remain separate curves. Failed runs are not
  counted as successful measurements. Counts in these graphs use exact
  output equality, as in the CLI summaries, not convergence alone.
- CSV files provide statistics and recognition graphs only. They cannot
  recover pixel grids or energy histories that were never exported, and
  their completion status is unknown.

Modern Hopfield snapshots show thresholded binary states; their reported
energy belongs to the continuous state. Energies from different rules are
not directly comparable. Recording/display work affects measured CPU times.

## Recording contract

`--record FILE` is optional and works with `--pattern`, `--compare`,
`--sweep` and `--capacity`. It requires training from pattern files (no
`--load-weights`) and a destination separate from all other inputs/outputs.
Existing files are overwritten. It does not change random-number consumption
or the result of a run. Ordinary CLI usage and CSV export remain independent.

The versioned JSONL stream contains:

1. `session`: format version 1, application version, seed, dimensions and
   the loaded clean memories.
2. `run`: unique ID, settings, learned memory count, reference/input rasters
   and training CPU time.
3. `step`: run ID, iteration, energy and the binary display raster.
4. `result`: final output, convergence, quality, attractor classification,
   closest memories and recall CPU time.
5. `failure`: a failed training attempt and its experiment settings.
6. `end`: the CLI's exit status.

Runs can be repeated; the ID distinguishes identical settings. Pixel arrays
are in row-major order. Runs without results or recordings without an end
marker are marked unfinished. Malformed records are rejected with a line
number. A write error makes the CLI fail; do not treat a truncated file as a
successful experiment. Large sweeps with many neurons/iterations produce
large files: export traces selectively. The CLI streams records; the viewer
loads them into memory.

## Tests

```bash
python3 viewer/test_viewer.py bin/hopfieldann
# Optional GUI smoke check on Linux with Tkinter and Xvfb installed:
xvfb-run -a python3 viewer/test_gui.py bin/hopfieldann
```

The model/integration tests are registered as `ViewerContract` in CTest when
Python 3 is available, and require neither Tkinter nor a graphical display.

## Neuron-by-neuron playback

```bash
./bin/hopfieldann data/hopf01.dat --rule hebbian --pattern 1 \
  --noise 30 --seed 42 --record /tmp/neuron-replay.jsonl \
  --trace-detail neuron
python3 viewer/viewer.py /tmp/neuron-replay.jsonl
```

`--trace-detail sweep` is the default. `neuron` records each actual change
inside a classical asynchronous sweep as a compact delta (1-based sweep and
neuron index plus new value), including unknown zero pixels becoming binary.
It requires `--record`. Modern Hopfield continues to record whole-state
updates, even when neuron detail is requested. No random numbers are consumed
by the observer, and convergence still requires a complete unchanged sweep.

Use **Play/Pause**, the slider or arrow buttons. Playback defaults to 10 frames
per second; choose 1, 5, 10, 25 or 50 fps and enable Loop to repeat; selecting another run or opening a file stops playback. Starting
Play at the end replays from the beginning. Frame labels distinguish neuron
changes from completed sweeps. Energy graphs remain per sweep: no artificial
energy values are interpolated between changes.

Neuron recordings use format version 2; the viewer still reads version 1.
Each `change` event precedes its sweep's existing `step` snapshot. The reader
checks that the changes reconstruct that snapshot. Changes are stored as
compact deltas both on disk and in the viewer; only the visible raster is
reconstructed. Traces can still grow substantially on long/noisy runs.

## Difference overlay and graph guidance

**Show differences (red)** outlines pixels that differ from the currently
shown reference on both input and recall grids. The live counter includes
unknown zero pixels. Selecting a different learned memory changes this
visual comparison, not the run's recorded metrics. For a noisy-file reference,
these are differences from that noisy image, not clean-target errors.

The graph area has reserved space even in smaller windows; legends are outside
the plot. Single-x datasets render points and explain how to obtain a full
curve. Noise plots need multiple levels (`--sweep`); capacity plots need
multiple stored counts (`--capacity`). CSV supports both recognition graphs,
but cannot provide an energy trace. Empty plots give a specific explanation.

Follow the [step-by-step quick start for Jos](../docs/JOS_QUICKSTART.md)
for copyable commands covering animation, spatial corruption and every graph.
