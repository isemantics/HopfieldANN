# Linux, Windows and WSL2

The C17 CLI generates runs; the independent Python 3.8+ / Tkinter viewer
opens JSONL replays or CSV statistics. No pip packages are needed.
You can copy these result files between operating systems. The viewer does
not need a compiler or the CLI executable to open existing results.
Run the commands below from the repository root.

## Linux (Debian/Ubuntu) and WSL2

```bash
sudo apt-get install cmake build-essential python3 python3-tk
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --parallel
mkdir -p /tmp/hopfield-demo
./bin/hopfieldann data/hopf01.dat --pattern 1 --noise 35 --seed 42 --trace-detail neuron --record /tmp/hopfield-demo/recall.jsonl
python3 viewer/viewer.py /tmp/hopfield-demo/recall.jsonl
```

For other Linux distributions install their C compiler, CMake, Python and
Tkinter packages. A graphical desktop is needed for the viewer. On WSL2,
use a working WSLg/X display (as in your current setup). The CLI and model
tests also run without a display. `python3 -m tkinter` checks GUI availability.
You can alternatively copy the replay to Windows and use the native viewer.

## Native Windows (PowerShell)

Install Python 3 with Tcl/Tk support, CMake, and Visual Studio 2022 Build
Tools with **Desktop development with C++** and a Windows SDK. Use a fresh
`build-windows` directory; don't reuse a Linux/WSL CMake build directory.

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=OFF
cmake --build build-windows --config Release --parallel
.\bin\hopfieldann.exe data\hopf01.dat --pattern 1 --noise 35 --seed 42 --trace-detail neuron --record "$env:TEMP\recall.jsonl"
py -3 viewer\viewer.py "$env:TEMP\recall.jsonl"
```

Both Debug and Release executables go directly into `bin`. If your Python
installation provides `python` instead of `py`, substitute `python` for
`py -3`. Check Tk using `py -3 -m tkinter`; it should open a small window.
If Tk is missing, modify/reinstall Python with Tcl/Tk support.
The viewer may also be launched without a filename to use its Open dialog.

[Microsoft's C17 toolchain requirements](https://learn.microsoft.com/en-us/cpp/overview/install-c17-support?view=msvc-170)
and [Python's Tkinter installation check](https://docs.python.org/3/library/tkinter.html)
provide further setup details.

## More interesting graphs

On Linux/WSL2:

```bash
./bin/hopfieldann data/hopf01.dat --compare --sweep 0:50:10 --trials 3 --seed 42 --record /tmp/hopfield-demo/sweep.jsonl
python3 viewer/viewer.py /tmp/hopfield-demo/sweep.jsonl
./bin/hopfieldann data/hopf01.dat --capacity --noise 25 --trials 3 --seed 42 --record /tmp/hopfield-demo/capacity.jsonl
python3 viewer/viewer.py /tmp/hopfield-demo/capacity.jsonl
```

On Windows PowerShell:

```powershell
.\bin\hopfieldann.exe data\hopf01.dat --compare --sweep 0:50:10 --trials 3 --seed 42 --record "$env:TEMP\sweep.jsonl"
py -3 viewer\viewer.py "$env:TEMP\sweep.jsonl"
.\bin\hopfieldann.exe data\hopf01.dat --capacity --noise 25 --trials 3 --seed 42 --record "$env:TEMP\capacity.jsonl"
py -3 viewer\viewer.py "$env:TEMP\capacity.jsonl"
```

Use the noise graph for sweep recordings, the stored-memory graph for
capacity recordings, and the energy graph for any recorded successful run.
See [Jos's tour](JOS_QUICKSTART.md) for playback and difference overlays.
Seeds reproduce runs on the same build/C runtime; different operating
systems can produce different random sequences with the same seed.

Options may appear before or after input filenames. Exact long names,
`--noise=35`, short options such as `-p1 -n35`, and `--` to end options
are supported. Quote paths containing spaces. Config is read first from
`./.hopfieldrc`, otherwise `$HOME/.hopfieldrc`; Windows falls back to
`%USERPROFILE%/.hopfieldrc` when HOME is unset.

## Developer checks

On Debian/Ubuntu install `libgtest-dev`, configure with
`-DBUILD_TESTING=ON`, build and run `ctest --test-dir build --output-on-failure`.
On Windows a CMake-discoverable Google Test installation is needed for the
C++ unit suite; Bash contract scripts are Unix-only. Independent of Google
Test, run `py -3 viewer/test_viewer.py bin/hopfieldann.exe` and
`py -3 viewer/test_gui.py bin/hopfieldann.exe` (the latter opens Tk briefly).

`.github/workflows/platforms.yml` defines Linux build/CTest/GUI checks and
native Windows MSVC build/export/model/GUI checks. Windows results must be
confirmed by running that workflow; local validation in this development
environment runs on Linux under WSL2.
