"""Integration tests use the real CLI, but need neither Tk nor a display."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from model import load, load_record, recognition_curves

BINARY = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else None


class ViewerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / ".hopfieldrc").write_text("# isolated defaults\n")
        (self.root / "patterns.dat").write_text("2 4 2\n****\n****\n****\n....\n")

    def run_cli(self, *args, code=0):
        result = subprocess.run([str(BINARY), "patterns.dat", *args],
                                cwd=self.root, stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, code, result.stderr + result.stdout)
        return result

    def record(self, *args):
        self.run_cli("--compare", "--noise", "50", "--seed", "42",
                     "--record", "run.jsonl", "--csv", "run.csv", "--quiet", *args)
        return load(self.root / "run.jsonl")

    def test_replay_and_csv_agree(self):
        data = self.record("--corruption", "left")
        csv = load(self.root / "run.csv")
        self.assertTrue(data["complete"])
        self.assertEqual(data["seed"], 42)
        self.assertEqual(len(data["runs"]), 10)
        for run, row in zip(data["runs"], csv["runs"]):
            self.assertEqual(run["hamming"], row["hamming"])
            self.assertAlmostEqual(run["overlap"], row["overlap"])
            self.assertEqual(run["attractor"], row["attractor"])
            self.assertEqual(run["output"], run["steps"][-1]["pixels"])
            self.assertEqual(run["input"].count(0), 4)
            self.assertEqual(run["original"], data["memories"][run["pattern"] - 1])
            self.assertTrue(all(len(step["pixels"]) == 8 for step in run["steps"]))
        for offset in range(2):
            self.assertTrue(all(data["runs"][offset]["input"] == data["runs"][i]["input"]
                                for i in range(offset, 10, 2)))
        self.assertEqual(recognition_curves(data, "noise_percent"),
                         recognition_curves(csv, "noise_percent"))

    def test_capacity_replay(self):
        data = self.record("--capacity", "--trials", "2")
        self.assertEqual(len(data["runs"]), 30)
        self.assertEqual([r["stored_patterns"] for r in data["runs"]], [1] * 10 + [2] * 20)
        for run in data["runs"]:
            self.assertLessEqual(run["pattern"], run["stored_patterns"])
        for points in recognition_curves(data, "stored_patterns").values():
            self.assertEqual([x for x, _ in points], [1, 2])

    def test_noise_sweep_replay(self):
        self.run_cli("--sweep", "0:100:50", "--trials", "2", "--rule", "modern",
                     "--record", "sweep.jsonl", "--quiet", "--seed", "42")
        data = load(self.root / "sweep.jsonl")
        self.assertEqual(len(data["runs"]), 12)
        points = next(iter(recognition_curves(data, "noise_percent").values()))
        self.assertEqual([x for x, _ in points], [0, 50, 100])

    def test_failed_learning_and_noisy_reference(self):
        (self.root / "patterns.dat").write_text("1 4 2\n****\n****\n")
        self.run_cli("--compare", "--noise", "0", "--record", "fail.jsonl", "--quiet", code=1)
        data = load(self.root / "fail.jsonl")
        self.assertEqual(data["exit_code"], 1)
        self.assertEqual(sum(r["status"] == "failed" for r in data["runs"]), 1)
        self.run_cli("patterns.dat", "--pattern", "1", "--record", "noisy.jsonl", "--quiet")
        self.assertFalse(recognition_curves(load(self.root / "noisy.jsonl"), "noise_percent"))

    def test_incomplete_and_malformed_files(self):
        self.record()
        path = self.root / "run.jsonl"
        events = [json.loads(line) for line in path.read_text().splitlines()]
        path.write_text("\n".join(json.dumps(e) for e in events[:-1]))
        self.assertFalse(load_record(path)["complete"])
        bad = copy.deepcopy(events)
        bad[1]["input"] = [1]
        path.write_text("\n".join(json.dumps(e) for e in bad))
        with self.assertRaisesRegex(ValueError, "dimensions"):
            load_record(path)
        bad = copy.deepcopy(events)
        bad[0]["format"] = 99
        path.write_text("\n".join(json.dumps(e) for e in bad))
        with self.assertRaisesRegex(ValueError, "format"):
            load_record(path)
        path.write_text('{"type":')
        with self.assertRaises(ValueError):
            load_record(path)

    def test_output_validation_and_write_errors(self):
        self.run_cli("--record", "bad.jsonl", code=1)
        self.run_cli("--pattern", "1", "--noise", "0", "--record", "patterns.dat", code=1)
        self.run_cli("--pattern", "1", "--noise", "0", "--csv", "same", "--record", "same", code=1)
        self.run_cli("--pattern", "1", "--noise", "0", "--record", "missing/run", code=1)
        if Path("/dev/full").exists():
            self.run_cli("--pattern", "1", "--noise", "0", "--record", "/dev/full", code=1)

    def test_csv_rejects_incomplete_metrics_and_rankings(self):
        path = self.root / "bad.csv"
        path.write_text("rule,hamming,overlap,closest1,overlap1\n"
                        "hebbian,0,1,1,\n")
        with self.assertRaisesRegex(ValueError, "closest-memory"):
            load(path)
        path.write_text("rule,hamming,overlap\nhebbian,,1\n")
        with self.assertRaisesRegex(ValueError, "hamming"):
            load(path)

    def test_recording_does_not_change_results(self):
        self.run_cli("--rule", "daydreaming", "--pattern", "1,2", "--noise", "25",
                     "--seed", "42", "--csv", "a.csv", "--quiet")
        self.run_cli("--rule", "daydreaming", "--pattern", "1,2", "--noise", "25",
                     "--seed", "42", "--csv", "b.csv", "--record", "b.jsonl", "--quiet")
        a, b = load(self.root / "a.csv"), load(self.root / "b.csv")
        for left, right in zip(a["runs"], b["runs"]):
            for field in ("hamming", "overlap", "converged", "attractor", "iterations"):
                self.assertEqual(left[field], right[field])


if __name__ == "__main__":
    unittest.main()
