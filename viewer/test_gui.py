"""Optional GUI smoke check: xvfb-run -a python3 viewer/test_gui.py BIN."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tkinter as tk

from viewer import Viewer

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    (folder / ".hopfieldrc").write_text("# isolated defaults\n")
    (folder / "patterns.dat").write_text("4 4 2\n****\n****\n****\n****\n****\n****\n....\n....\n")
    subprocess.run([str(binary), "patterns.dat", "--capacity", "--compare",
                    "--noise", "25", "--trials", "1", "--seed", "42",
                    "--corruption", "block", "--record", "runs.jsonl",
                    "--trace-detail", "neuron", "--csv", "runs.csv", "--quiet"], cwd=folder, check=True,
                   stdin=subprocess.DEVNULL, timeout=30)
    root = tk.Tk()
    errors = []
    root.report_callback_exception = lambda *args: errors.append(args)
    try:
        app = Viewer(root)
        root.geometry("1280x900")
        app.open(folder / "runs.jsonl")
        root.update()
        assert len(app.table.get_children()) == 15
        for index in range(15):
            app.table.selection_set(str(index))
            app.select()
            app.position.set(0)
            app.draw_rasters()
            app.move(1)
            app.toggle_play()
            assert app.timer is not None
            app.pause()
            assert app.timer is None
            app.position.set(len(app.run["frames"]) - 1)
            app.tick()
            assert app.position.get() == len(app.run["frames"])
            assert app.timer is None
            app.memory.set("1")
            app.draw_rasters()
            for graph in range(3):
                app.graph_mode.current(graph)
                app.draw_graph()
                assert app.graph.find_withtag("point"), (index, graph)
                if graph == 2:
                    assert app.graph.find_withtag("series")
            root.update()
            assert all(canvas.find_all() for canvas in app.canvases)
        app.show_differences.set(True)
        app.position.set(0)
        app.draw_rasters()
        assert app.canvases[1].find_withtag("difference")
        app.speed.set("25 fps")
        assert app.playback_delay() == 40
        app.toggle_play()
        app.change_speed()
        assert app.timer is not None
        app.pause()
        app.loop.set(True)
        app.position.set(len(app.run["frames"]))
        app.tick()
        assert app.position.get() == 0
        assert app.timer is not None
        app.pause()
        app.loop.set(False)
        subprocess.run([str(binary), "patterns.dat", "--compare",
                        "--sweep", "0:100:50", "--trials", "1", "--seed", "42",
                        "--record", "sweep.jsonl", "--quiet"], cwd=folder,
                       check=True, stdin=subprocess.DEVNULL, timeout=30)
        app.open(folder / "sweep.jsonl")
        root.update()
        app.graph_mode.current(1)
        app.draw_graph()
        assert len(app.graph.find_withtag("point")) == 15
        assert len(app.graph.find_withtag("series")) == 5
        root.geometry("1050x760")
        root.update()
        assert app.graph.winfo_height() >= 150
        assert app.graph.winfo_rooty() + app.graph.winfo_height() <= root.winfo_rooty() + root.winfo_height()
        assert app.graph.find_withtag("point")
        if os.environ.get("HOPFIELD_SCREENSHOT"):
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "x11grab",
                            "-video_size", "1280x900", "-i", os.environ["DISPLAY"],
                            "-frames:v", "1", os.environ["HOPFIELD_SCREENSHOT"]],
                           check=True, timeout=15)
        app.open(folder / "runs.csv")
        root.update()
        assert app.data["complete"] is None
        for graph in (1, 2):
            app.graph_mode.current(graph)
            app.draw_graph()
            assert app.graph.find_withtag("point")
        app.graph_mode.current(0)
        app.draw_graph()
        assert app.graph.find_withtag("empty")
        assert not errors, errors
        print("GUI smoke passed: all runs, sliders, memories, graphs and CSV loading.")
    finally:
        root.destroy()
