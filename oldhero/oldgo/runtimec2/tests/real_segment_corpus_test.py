#!/usr/bin/env python3
import importlib.util
import json
import os
import stat
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = ROOT / "runtimec2/tools/real_segment_corpus.py"
SPEC = importlib.util.spec_from_file_location("real_segment_corpus", RUNNER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import {RUNNER_PATH}")
runner = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = runner
SPEC.loader.exec_module(runner)


class RealSegmentCorpusTest(unittest.TestCase):
    def test_declaration_parser_ignores_comments_and_strings_and_keeps_unicode(self):
        source = r'''
            // rwfunc line_comment() -> () {}
            /* rwfunc outer_comment() -> () {
                 /* rwfunc nested_comment() -> () {} */
            } */
            rwfunc alpha_2() -> () {}
            rwfunc 计算二(n:int64) -> (结果:int64) { n -> 结果 }
            value = "rwfunc quoted() -> () {}"
            raw = r###"rwfunc raw_quoted() -> () {}"###
        '''
        self.assertEqual(
            runner.parse_rwfunc_names(source, "unicode.kv"),
            ("alpha_2", "计算二"),
        )

    def test_declaration_parser_rejects_duplicates(self):
        source = "rwfunc same() -> () {}\nrwfunc same(n:int64) -> () {}\n"
        with self.assertRaisesRegex(runner.CorpusError, "duplicate rwfunc"):
            runner.parse_rwfunc_names(source, "duplicate.kv")

    def test_declaration_parser_rejects_missing_declarations(self):
        source = '// rwfunc commented() -> () {}\nprintln("rwfunc quoted()")\n'
        with self.assertRaisesRegex(runner.CorpusError, "no rwfunc declarations"):
            runner.parse_rwfunc_names(source, "missing.kv")

    def test_minimum_scales_follow_benchmark_sweep(self):
        self.assertEqual(
            runner.load_min_scales(ROOT / "benchmark/run.py"),
            {
                "binary_search": 16,
                "binary_trees": 4,
                "fib": 8,
                "hash_table": 50,
                "iops": 500,
                "k_nucleotide": 3,
                "matmul": 4,
                "nqueens": 4,
                "prime_sieve": 50,
                "quicksort": 32,
            },
        )

    def test_noncanonical_json_is_rejected(self):
        raw = json.dumps({"b": 1, "a": 2}, indent=2).encode()
        with self.assertRaisesRegex(runner.CorpusError, "not canonical"):
            runner._canonical_document(raw, "pretty")

    def test_exporter_must_return_every_declared_function(self):
        document = {
            "schema": runner.EXPORT_SCHEMA,
            "fixture_only": False,
            "functions": [{"name": "/lib/test", "instructions": []}],
        }
        with self.assertRaisesRegex(runner.CorpusError, "differ from declarations"):
            runner._validate_exporter(
                document, ["/lib/helper", "/lib/test"], "missing-helper"
            )

    def test_ten_case_orchestration_is_isolated_and_byte_stable(self):
        with tempfile.TemporaryDirectory(prefix="real-segment-corpus-test-") as directory:
            temporary = Path(directory)
            log_path = temporary / "byteseek.jsonl"
            byteseek = temporary / "fake_byteseek.py"
            exporter = temporary / "fake_exporter.py"
            byteseek.write_text(
                textwrap.dedent(
                    """\
                    #!/usr/bin/env python3
                    import json
                    import os
                    import sys

                    with open(os.environ["FAKE_BYTESEEK_LOG"], "a", encoding="utf-8") as log:
                        log.write(json.dumps(
                            {"args": sys.argv[1:], "dsn": os.environ.get("KVSPACE")},
                            sort_keys=True,
                            separators=(",", ":"),
                        ) + "\\n")
                    """
                ),
                encoding="utf-8",
            )
            exporter.write_text(
                textwrap.dedent(
                    """\
                    #!/usr/bin/env python3
                    import json
                    import sys

                    functions = []
                    for name in sorted(sys.argv[2:]):
                        functions.append({
                            "constant_count": 0,
                            "instructions": [{
                                "effect": False,
                                "family": "generic",
                                "op": "copy",
                                "reads": [],
                                "safepoint": False,
                                "side_exits": [],
                                "source_id": 1,
                                "targets": [],
                                "writes": [],
                            }],
                            "name": name,
                            "register_count": 0,
                        })
                    document = {
                        "corpus": "fake-real-segments",
                        "fixture_only": False,
                        "functions": functions,
                        "provenance": "test build_segment() Segment::insns/regs/lit no effect",
                        "schema": "kvlang.binir-probe.v1",
                    }
                    print(json.dumps(
                        document,
                        ensure_ascii=False,
                        sort_keys=True,
                        separators=(",", ":"),
                    ))
                    """
                ),
                encoding="utf-8",
            )
            executable = stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR
            byteseek.chmod(executable)
            exporter.chmod(executable)
            toolchain = runner.Toolchain(
                source_root=ROOT,
                byteseek=byteseek,
                exporter=exporter,
                probe=ROOT / "runtimec2/tools/binir_format_probe.py",
                timeout=30,
            )
            with mock.patch.dict(
                os.environ, {"FAKE_BYTESEEK_LOG": str(log_path)}, clear=False
            ):
                first = runner.run_corpus(toolchain, temporary / "artifacts-a")
                second = runner.run_corpus(toolchain, temporary / "artifacts-b")

            self.assertEqual(runner.render_report(first), runner.render_report(second))
            self.assertEqual(first["totals"], {"cases": 10, "functions": 15, "instructions": 15})
            self.assertEqual(
                sum(
                    function["segment_path"] == "/lib/test"
                    for case in first["cases"]
                    for function in case["functions"]
                ),
                10,
            )
            for case in first["cases"]:
                self.assertNotIn(
                    "__SCALE__",
                    (temporary / "artifacts-a" / case["case"] / f"{case['case']}.kv").read_text(
                        encoding="utf-8"
                    ),
                )
                self.assertEqual(
                    sorted(function["logical_name"] for function in case["functions"]),
                    sorted(
                        runner.parse_rwfunc_names(
                            (
                                ROOT
                                / "benchmark/cases"
                                / case["case"]
                                / f"{case['case']}.kv"
                            ).read_text(encoding="utf-8")
                        )
                    ),
                )

            events = [json.loads(line) for line in log_path.read_text().splitlines()]
            self.assertEqual(len(events), 40)
            for run_events in (events[:20], events[20:]):
                dsns = {event["dsn"] for event in run_events}
                self.assertEqual(len(dsns), 10)
                self.assertTrue(
                    all(
                        sum(event["dsn"] == dsn for event in run_events) == 2
                        for dsn in dsns
                    )
                )


if __name__ == "__main__":
    unittest.main()
