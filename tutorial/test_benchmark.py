import csv
import importlib.util
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).with_name("test.py")
SPEC = importlib.util.spec_from_file_location("tutorial_test", SCRIPT)
assert SPEC and SPEC.loader
tutorial_test = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tutorial_test)


class BenchmarkTest(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.tutorial = self.root / "tutorial"
        self.tutorial.mkdir()
        self.kvlang = self.root / "kvlang"
        self.kvlang.write_text("#!/bin/sh\nprintf 'answer 42\\n'\n", encoding="utf-8")
        self.kvlang.chmod(0o755)
        self.flush_redis = tutorial_test._flush_redis
        self.patchers = [
            mock.patch.object(tutorial_test, "ROOT", self.root),
            mock.patch.object(tutorial_test, "KV", str(self.kvlang)),
            mock.patch.object(tutorial_test, "BENCH_CSV", self.tutorial / "benchmark.csv"),
            mock.patch.object(tutorial_test, "_flush_redis"),
            mock.patch("builtins.print"),
        ]
        for patcher in self.patchers:
            patcher.start()

    def tearDown(self):
        for patcher in reversed(self.patchers):
            patcher.stop()
        self.tempdir.cleanup()

    def write_fixture(self, python_output="answer 42", include_c=True):
        source = self.tutorial / "case.kv"
        source.write_text("# 期望输出:\n#   answer 42\n", encoding="utf-8")
        source.with_suffix(".py").write_text(
            f"print({python_output!r})\n", encoding="utf-8",
        )
        if include_c:
            source.with_suffix(".c").write_text(
                '#include <stdio.h>\nint main(void) { puts("answer 42"); return 0; }\n',
                encoding="utf-8",
            )
        return source

    def read_rows(self):
        with open(tutorial_test.BENCH_CSV, newline="", encoding="utf-8") as fh:
            return list(csv.DictReader(fh))

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_writes_timings_for_matching_outputs(self):
        source = self.write_fixture()

        self.assertEqual(tutorial_test.run_benchmarks([source]), 0)

        rows = self.read_rows()
        self.assertEqual(len(rows), 1)
        self.assertEqual(
            list(rows[0]), ["file", "kvlang_ms", "python_ms", "c_ms"],
        )
        self.assertEqual(rows[0]["file"], "tutorial/case.kv")
        for field in ("kvlang_ms", "python_ms", "c_ms"):
            self.assertGreaterEqual(float(rows[0][field]), 0)

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_marks_output_mismatch_invalid(self):
        source = self.write_fixture(python_output="wrong")

        self.assertEqual(tutorial_test.run_benchmarks([source]), 1)

        row = self.read_rows()[0]
        self.assertEqual(
            [row["kvlang_ms"], row["python_ms"], row["c_ms"]],
            ["invalid", "invalid", "invalid"],
        )

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_marks_different_outputs_invalid(self):
        source = self.write_fixture(python_output="answer 42 ")

        self.assertEqual(tutorial_test.run_benchmarks([source]), 1)

        row = self.read_rows()[0]
        self.assertEqual(row["python_ms"], "invalid")

    def test_skips_missing_counterpart(self):
        source = self.write_fixture(include_c=False)

        self.assertEqual(tutorial_test.run_benchmarks([source]), 0)
        self.assertEqual(self.read_rows(), [])

    def test_marks_missing_expected_output_invalid(self):
        source = self.write_fixture()
        source.write_text("println(\"answer 42\")\n", encoding="utf-8")

        self.assertEqual(tutorial_test.run_benchmarks([source]), 1)
        self.assertEqual(self.read_rows()[0]["kvlang_ms"], "invalid")

    def test_output_validation_only_uses_stdout(self):
        result = tutorial_test.subprocess.CompletedProcess(
            [], 1, stdout="answer 42\n", stderr="warning\n",
        )

        self.assertEqual(
            tutorial_test._benchmark_error(
                {"kvlang": result, "python": result, "c": result}, ["answer 42"],
            ),
            "",
        )

    def test_bench_honors_filter_and_no_build(self):
        keep = self.tutorial / "keep.kv"
        drop = self.tutorial / "drop.kv"
        with (
            mock.patch.object(tutorial_test, "discover", return_value=[drop, keep]),
            mock.patch.object(tutorial_test, "run_benchmarks", return_value=0) as bench,
            mock.patch.object(tutorial_test.subprocess, "run") as run,
            mock.patch.object(
                tutorial_test.sys, "argv",
                ["test.py", "--bench", "--no-build", "--filter", "keep"],
            ),
            self.assertRaises(SystemExit) as exit_context,
        ):
            tutorial_test.main()

        self.assertEqual(exit_context.exception.code, 0)
        bench.assert_called_once_with([keep], False)
        run.assert_not_called()

    def test_redis_timeout_is_not_ignored(self):
        timeout = tutorial_test.subprocess.TimeoutExpired(["redis-cli"], 5)
        with (
            mock.patch.object(tutorial_test.subprocess, "run", side_effect=timeout),
            self.assertRaises(tutorial_test.subprocess.TimeoutExpired),
        ):
            self.flush_redis()


if __name__ == "__main__":
    unittest.main()
