"""Read-only loaders for Hopfield JSONL replays and metrics CSV files."""
import csv
import json
import math
from collections import defaultdict


def number(value, name, default=None):
    if value is None or value == "":
        return default
    try:
        result = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"Invalid {name}") from exc
    if not math.isfinite(result):
        raise ValueError(f"Non-finite {name}")
    return result


def integer(value, name, minimum=0):
    result = number(value, name)
    if result is None or result < minimum or not result.is_integer():
        raise ValueError(f"Invalid {name}")
    return int(result)


def pixels(value, size, binary=False):
    allowed = (-1, 1) if binary else (-1, 0, 1)
    if not isinstance(value, list) or len(value) != size:
        raise ValueError("Raster dimensions do not match session")
    if any(type(v) not in (int, float) or v not in allowed for v in value):
        raise ValueError("Invalid pixel (expected -1, 0 or +1)")
    return value


def settings(event):
    event = dict(event)
    for key, default in (("stored_patterns", 0), ("noise_percent", -1),
                         ("trial", 1), ("pattern", 0)):
        if event.get(key) in (None, ""):
            event[key] = default
    return {
        "rule": str(event.get("rule", "unknown")),
        "stored_patterns": integer(event.get("stored_patterns", 0), "stored_patterns"),
        "noise_percent": integer(event.get("noise_percent", -1), "noise_percent", -1),
        "trial": integer(event.get("trial", 1), "trial", 1),
        "pattern": integer(event.get("pattern", 0) or 0, "pattern"),
        "corruption": str(event.get("corruption", "flip")),
        "reference": str(event.get("reference", "stored")),
        "training_seconds": number(event.get("training_seconds"), "training_seconds"),
        "steps": [], "status": "unfinished",
    }


def load_record(path):
    data = {"runs": [], "memories": [], "complete": False, "exit_code": None}
    runs = {}
    session = False
    with open(path, encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                event = json.loads(line)
                if not isinstance(event, dict):
                    raise ValueError("Expected an event object")
                kind = event.get("type")
                if data["complete"]:
                    raise ValueError("Data after end event")
                if kind == "session":
                    if session or event.get("format") != 1:
                        raise ValueError("Duplicate session or unsupported format")
                    data.update(rows=integer(event.get("rows"), "rows", 1),
                                columns=integer(event.get("columns"), "columns", 1),
                                seed=integer(event.get("seed"), "seed"),
                                version=str(event.get("version", "unknown")))
                    size = data["rows"] * data["columns"]
                    memories = event.get("memories")
                    if not isinstance(memories, list) or not memories:
                        raise ValueError("Missing memories")
                    data["memories"] = [pixels(p, size, True) for p in memories]
                    session = True
                elif not session:
                    raise ValueError("Missing session header")
                elif kind == "run":
                    run_id = integer(event.get("id"), "id", 1)
                    if run_id in runs:
                        raise ValueError("Duplicate run ID")
                    run = settings(event)
                    if not 1 <= run["stored_patterns"] <= len(data["memories"]):
                        raise ValueError("Invalid stored memory count")
                    run.update(id=run_id,
                               original=pixels(event.get("original"), size, True),
                               input=pixels(event.get("input"), size))
                    runs[run_id] = run
                    data["runs"].append(run)
                elif kind in ("step", "result"):
                    run = runs.get(integer(event.get("id"), "id", 1))
                    if run is None or run["status"] != "unfinished":
                        raise ValueError("Missing run or result already recorded")
                    if kind == "step":
                        iteration = integer(event.get("iteration"), "iteration", 1)
                        if iteration != len(run["steps"]) + 1:
                            raise ValueError("Out-of-order iteration")
                        run["steps"].append({
                            "iteration": iteration,
                            "energy": number(event.get("energy"), "energy"),
                            "pixels": pixels(event.get("pixels"), size, True),
                        })
                    else:
                        if not isinstance(event.get("converged"), bool):
                            raise ValueError("Invalid convergence flag")
                        hamming = integer(event.get("hamming"), "hamming")
                        overlap = number(event.get("overlap"), "overlap")
                        if hamming > size or overlap is None or not -1 <= overlap <= 1:
                            raise ValueError("Invalid recall metrics")
                        closest = event.get("closest", [])
                        if not isinstance(closest, list) or len(closest) > 3:
                            raise ValueError("Invalid memory ranking")
                        ranking = []
                        for match in closest:
                            index = integer(match.get("pattern"), "match", 1)
                            score = number(match.get("overlap"), "overlap")
                            if index > run["stored_patterns"] or score is None or not -1 <= score <= 1:
                                raise ValueError("Invalid memory match")
                            ranking.append({"pattern": index, "overlap": score})
                        run.update(status="ok", hamming=hamming, overlap=overlap,
                                   converged=event["converged"],
                                   energy=number(event.get("energy"), "energy"),
                                   recall_seconds=number(event.get("recall_seconds"), "recall_seconds"),
                                   output=pixels(event.get("output"), size, True),
                                   attractor=str(event.get("attractor", "unknown")),
                                   matched_pattern=integer(event.get("matched_pattern", 0), "matched_pattern"),
                                   closest=ranking, iterations=len(run["steps"]))
                elif kind == "failure":
                    run = settings(event)
                    run.update(status="failed", error=str(event.get("error", "failed")))
                    data["runs"].append(run)
                elif kind == "end":
                    data["complete"] = True
                    data["exit_code"] = integer(event.get("exit_code"), "exit_code")
                else:
                    raise ValueError(f"Unknown event type: {kind}")
            except (ValueError, TypeError, AttributeError, KeyError) as exc:
                raise ValueError(f"Line {line_number}: {exc}") from exc
    if not session:
        raise ValueError("Empty recording")
    return data


def load_csv(path):
    data = {"runs": [], "memories": [], "complete": None, "rows": 0,
            "columns": 0, "version": "CSV", "seed": "see rows"}
    with open(path, newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or not {"rule", "hamming", "overlap"} <= set(reader.fieldnames):
            raise ValueError("Not a Hopfield metrics CSV")
        for line, row in enumerate(reader, 2):
            try:
                if None in row:
                    raise ValueError("Too many CSV fields")
                run = settings(row)
                run.update(status=row.get("status", "ok"),
                           hamming=number(row.get("hamming"), "hamming"),
                           overlap=number(row.get("overlap"), "overlap"),
                           energy=number(row.get("energy"), "energy"),
                           iterations=number(row.get("iterations"), "iterations"),
                           recall_seconds=number(row.get("recall_seconds"), "recall_seconds"),
                           converged=row.get("converged") == "yes",
                           attractor=row.get("attractor", "unavailable"),
                           matched_pattern=integer(row.get("matched_pattern") or 0, "matched_pattern"),
                           closest=[{"pattern": integer(row[f"closest{i}"], "closest", 1),
                                     "overlap": number(row.get(f"overlap{i}"), "overlap")}
                                    for i in range(1, 4) if row.get(f"closest{i}")])
                for match in run["closest"]:
                    if match["overlap"] is None or not -1 <= match["overlap"] <= 1:
                        raise ValueError("Invalid closest-memory overlap")
                if run["status"] == "ok":
                    integer(run["hamming"], "hamming")
                    if run["overlap"] is None or not -1 <= run["overlap"] <= 1:
                        raise ValueError("Invalid recall overlap")
                data["runs"].append(run)
            except ValueError as exc:
                raise ValueError(f"CSV line {line}: {exc}") from exc
    return data


def load(path):
    return load_csv(path) if str(path).lower().endswith(".csv") else load_record(path)


def recognition_curves(data, axis):
    """Keep corruption and the other experiment dimension separate."""
    groups = defaultdict(lambda: defaultdict(lambda: [0, 0]))
    other = "stored_patterns" if axis == "noise_percent" else "noise_percent"
    for run in data["runs"]:
        if run["status"] != "ok" or run["reference"] != "stored" or run.get("hamming") is None:
            continue
        key = (run["rule"], run["corruption"], run[other])
        counts = groups[key][run[axis]]
        counts[0] += run["hamming"] == 0
        counts[1] += 1
    return {key: [(x, 100 * n / total) for x, (n, total) in sorted(points.items())]
            for key, points in groups.items()}
