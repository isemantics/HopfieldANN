#!/usr/bin/env python3
"""Offline Hopfield viewer. Never launches or modifies the CLI."""
import sys
from pathlib import Path

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
except ImportError:
    raise SystemExit("The viewer requires Python 3 with Tkinter (python3-tk on Linux).")

from model import load, recognition_curves, replay_frame


class Viewer:
    def __init__(self, root):
        self.root = root
        self.data = None
        self.run = None
        self.timer = None
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.title("Hopfield — experiment viewer")
        root.geometry("1150x820")
        top = ttk.Frame(root, padding=8)
        top.pack(fill="x")
        ttk.Button(top, text="Open recording / CSV", command=self.open).pack(side="left")
        self.filename = ttk.Label(top, text="Run experiments in the CLI, then open their results here.")
        self.filename.pack(side="left", padx=12)
        self.session = ttk.Label(root, padding=(8, 0))
        self.session.pack(fill="x")
        pane = ttk.Panedwindow(root, orient="horizontal")
        pane.pack(fill="both", expand=True, padx=8, pady=8)
        left = ttk.Frame(pane)
        pane.add(left, weight=1)
        cols = ("rule", "stored", "noise", "trial", "pattern", "hamming", "status")
        self.table = ttk.Treeview(left, columns=cols, show="headings", selectmode="browse")
        for name in cols:
            self.table.heading(name, text=name.title())
            self.table.column(name, width=65 if name != "rule" else 105, stretch=True)
        scroll = ttk.Scrollbar(left, orient="vertical", command=self.table.yview)
        self.table.configure(yscrollcommand=scroll.set)
        self.table.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")
        self.table.bind("<<TreeviewSelect>>", self.select)
        right = ttk.Frame(pane)
        pane.add(right, weight=2)
        grids = ttk.Frame(right)
        grids.pack(fill="both", expand=True)
        self.canvases = []
        self.grid_titles = []
        for title in ("Reference", "Corrupted input", "Recall"):
            frame = ttk.Frame(grids)
            frame.pack(side="left", fill="both", expand=True)
            label = ttk.Label(frame, text=title, anchor="center")
            label.pack(fill="x")
            canvas = tk.Canvas(frame, width=190, height=220, bg="#f4f5f7", highlightthickness=0)
            canvas.pack(fill="both", expand=True)
            canvas.bind("<Configure>", lambda event: self.draw_rasters())
            self.grid_titles.append(label)
            self.canvases.append(canvas)
        self.position = tk.IntVar(value=0)
        playback = ttk.Frame(right)
        playback.pack(fill="x", pady=5)
        self.play_button = ttk.Button(playback, text="Play", command=self.toggle_play)
        self.play_button.pack(side="left")
        ttk.Button(playback, text="◀", command=lambda: self.move(-1)).pack(side="left")
        self.slider = tk.Scale(playback, from_=0, to=0, orient="horizontal", variable=self.position,
                               command=lambda _: self.draw_rasters(), showvalue=False)
        self.slider.pack(side="left", fill="x", expand=True)
        ttk.Button(playback, text="▶", command=lambda: self.move(1)).pack(side="left")
        self.step_label = ttk.Label(right, anchor="center")
        self.step_label.pack(fill="x")
        self.info = ttk.Label(right, wraplength=650, padding=6)
        self.info.pack(fill="x")
        memory_bar = ttk.Frame(right)
        memory_bar.pack(fill="x", pady=4)
        ttk.Label(memory_bar, text="Inspect learned memory:").pack(side="left")
        self.memory = ttk.Combobox(memory_bar, state="readonly", width=8)
        self.memory.pack(side="left", padx=5)
        self.memory.bind("<<ComboboxSelected>>", self.draw_rasters)
        controls = ttk.Frame(right)
        controls.pack(fill="x")
        self.graph_mode = ttk.Combobox(controls, state="readonly",
                                      values=("Energy / iteration", "Recognition / noise", "Recognition / stored memories"))
        self.graph_mode.current(0)
        self.graph_mode.pack(side="left", fill="x", expand=True)
        self.graph_mode.bind("<<ComboboxSelected>>", lambda event: self.draw_graph())
        self.graph = tk.Canvas(right, width=650, height=260, bg="white", highlightthickness=0)
        self.graph.pack(fill="both", expand=True, pady=5)
        self.graph.bind("<Configure>", lambda event: self.draw_graph())
        ttk.Label(root, text="Black = +1 · white = −1 · amber = unknown (0). Modern energy uses its continuous state; snapshots are thresholded.",
                  padding=(8, 4)).pack(fill="x")

    def pause(self):
        if self.timer is not None:
            self.root.after_cancel(self.timer)
            self.timer = None
        self.play_button.configure(text="Play")

    def close(self):
        self.pause()
        self.root.destroy()

    def toggle_play(self):
        if self.timer is not None:
            self.pause()
            return
        if not self.run or not self.run["frames"]:
            return
        if self.position.get() >= len(self.run["frames"]):
            self.position.set(0)
        self.play_button.configure(text="Pause")
        self.timer = self.root.after(100, self.tick)

    def tick(self):
        self.timer = None
        if not self.run:
            self.pause()
            return
        self.position.set(min(self.position.get() + 1, len(self.run["frames"])))
        self.draw_rasters()
        if self.position.get() < len(self.run["frames"]):
            self.timer = self.root.after(100, self.tick)
        else:
            self.pause()

    def open(self, path=None):
        self.pause()
        path = path or filedialog.askopenfilename(filetypes=[("Hopfield results", "*.jsonl *.csv"), ("All files", "*")])
        if not path:
            return
        try:
            data = load(path)
        except (OSError, ValueError) as exc:
            messagebox.showerror("Cannot open results", str(exc))
            return
        self.data = data
        self.run = None
        self.filename.configure(text=Path(path).name)
        state = "CSV: statistics only; completion unknown" if data["complete"] is None else (
            f"Finished (exit {data['exit_code']})" if data["complete"] else "Incomplete recording")
        self.session.configure(text=f"{state} · version {data['version']} · seed {data['seed']} · {len(data['runs'])} runs")
        self.table.delete(*self.table.get_children())
        for i, run in enumerate(data["runs"]):
            self.table.insert("", "end", iid=str(i), values=(run["rule"], run["stored_patterns"],
                run["noise_percent"], run["trial"], run["pattern"], run.get("hamming", "—"), run["status"]))
        if data["runs"]:
            self.table.selection_set("0")
            self.select()
        else:
            self.info.configure(text="No runs recorded.")
            self.draw_rasters()
            self.draw_graph()

    def select(self, event=None):
        selected = self.table.selection()
        if not selected or not self.data:
            return
        self.pause()
        self.run = self.data["runs"][int(selected[0])]
        self.slider.configure(to=len(self.run["frames"]))
        self.position.set(len(self.run["frames"]))
        count = min(self.run["stored_patterns"], len(self.data["memories"]))
        self.memory.configure(values=["Reference"] + list(range(1, count + 1)))
        self.memory.set("Reference")
        run = self.run
        matches = ", ".join(f"{m['pattern']}: {m['overlap']:.3f}" for m in run.get("closest", []))
        self.info.configure(text=(f"{run['rule']} · {run['corruption']} · {run['noise_percent']}% · stored {run['stored_patterns']}\n"
            f"{run.get('attractor', run['status'])} (match {run.get('matched_pattern', 0) or '—'}) · converged: {run.get('converged', '—')} · overlap: {run.get('overlap', '—')} · Hamming: {run.get('hamming', '—')}\n"
            f"Training CPU: {run.get('training_seconds', '—')} s · recall CPU: {run.get('recall_seconds', '—')} s\n"
            f"Closest: {matches or 'unavailable'}"))
        self.draw_rasters()
        self.draw_graph()

    def move(self, delta):
        self.pause()
        if self.run:
            self.position.set(max(0, min(len(self.run["frames"]), self.position.get() + delta)))
            self.draw_rasters()

    def draw_rasters(self, event=None):
        if not self.data or not self.run:
            for canvas in self.canvases:
                canvas.delete("all")
            return
        run = self.run
        step = min(self.position.get(), len(run["frames"]))
        reference = run.get("original")
        label = "Reference (noisy)" if run["reference"] == "noisy" else "Original memory"
        selected = self.memory.get()
        if selected.isdigit() and 1 <= int(selected) <= len(self.data["memories"]):
            reference = self.data["memories"][int(selected) - 1]
            label = f"Learned memory {selected}"
        self.grid_titles[0].configure(text=label)
        current = replay_frame(run, step)
        values = [reference, run.get("input"), current]
        for canvas, pixels in zip(self.canvases, values):
            canvas.delete("all")
            if pixels is None:
                canvas.create_text(95, 80, text="No raster data", fill="#657080")
                continue
            rows, cols = self.data["rows"], self.data["columns"]
            width, height = canvas.winfo_width(), canvas.winfo_height()
            cell = min((width - 12) / cols, (height - 12) / rows)
            if cell <= 0:
                continue
            x0, y0 = (width - cols * cell) / 2, (height - rows * cell) / 2
            for i, value in enumerate(pixels):
                x, y = x0 + (i % cols) * cell, y0 + (i // cols) * cell
                color = "#172338" if value == 1 else "#ffffff" if value == -1 else "#efb74c"
                canvas.create_rectangle(x, y, x + cell, y + cell, fill=color,
                                        outline="#c4cad3" if cell >= 6 else color)
        self.grid_titles[2].configure(text="Recall" if step else "Initial state")
        frame = run["frames"][step - 1] if step else {}
        energy = frame.get("energy")
        detail = f" · sweep {frame['iteration']}" if frame else " · initial input"
        if "neuron" in frame:
            detail += f" · neuron {frame['neuron']} changed"
        elif frame:
            detail += " complete"
        self.step_label.configure(text=f"Frame {step}/{len(run['frames'])}" + detail +
                                  (f" · energy {energy:.6g}" if energy is not None else ""))

    def draw_graph(self):
        canvas = self.graph
        canvas.delete("all")
        if not self.data:
            return
        mode = self.graph_mode.current()
        if mode == 0:
            points = [(s["iteration"], s["energy"]) for s in (self.run or {}).get("steps", []) if s["energy"] is not None]
            curves = {"Selected run": points} if points else {}
            xlabel, ylabel = "Iteration", "Energy"
        else:
            axis = "noise_percent" if mode == 1 else "stored_patterns"
            curves = {f"{rule} / {corruption} / {'stored' if mode == 1 else 'noise'}={other}": points
                      for (rule, corruption, other), points in recognition_curves(self.data, axis).items()
                      if not self.run or other == self.run["stored_patterns" if mode == 1 else "noise_percent"]}
            xlabel, ylabel = ("Noise (%)" if mode == 1 else "Stored memories"), "Exact clean recall (%)"
        if not curves:
            canvas.create_text(20, 35, anchor="w", text="No data for this graph (CSV has no iteration traces).")
            return
        width, height = canvas.winfo_width(), canvas.winfo_height()
        if width < 120 or height < 100:
            return
        legend_height = 15 * len(curves)
        left, top, right, bottom = 65, 25, width - 15, max(65, height - 45 - legend_height)
        all_points = [point for series in curves.values() for point in series]
        xmin, xmax = min(x for x, _ in all_points), max(x for x, _ in all_points)
        ymin, ymax = (0, 100) if mode else (min(y for _, y in all_points), max(y for _, y in all_points))
        if xmin == xmax:
            xmin, xmax = xmin - 0.5, xmax + 0.5
        if ymin == ymax:
            ymin, ymax = ymin - 1, ymax + 1
        def point(x, y):
            return left + (x - xmin) / (xmax - xmin) * (right - left), bottom - (y - ymin) / (ymax - ymin) * (bottom - top)
        canvas.create_text(left, 10, text=ylabel, anchor="w")
        for i in range(5):
            x, y = xmin + (xmax - xmin) * i / 4, ymin + (ymax - ymin) * i / 4
            px, py = point(x, y)
            canvas.create_line(left, py, right, py, fill="#e3e7ed")
            canvas.create_text(left - 6, py, text=f"{y:.3g}", anchor="e")
            canvas.create_text(px, bottom + 12, text=f"{x:.3g}")
        canvas.create_text((left + right) / 2, bottom + 28, text=xlabel)
        colors = ("#2563eb", "#15803d", "#c2410c", "#7e22ce", "#be185d", "#0e7490")
        for i, (label, series) in enumerate(curves.items()):
            color = colors[i % len(colors)]
            coords = [coordinate for x, y in series for coordinate in point(x, y)]
            if len(coords) >= 4:
                canvas.create_line(*coords, fill=color, width=2)
            for x, y in series:
                px, py = point(x, y)
                canvas.create_oval(px - 3, py - 3, px + 3, py + 3, fill=color, outline=color)
            canvas.create_text(left, bottom + 45 + 15 * i, anchor="w", text=label, fill=color)


def main():
    try:
        root = tk.Tk()
    except tk.TclError as exc:
        raise SystemExit(f"A graphical desktop is required: {exc}")
    viewer = Viewer(root)
    if len(sys.argv) > 1:
        viewer.open(sys.argv[1])
    root.mainloop()


if __name__ == "__main__":
    main()
