#!/usr/bin/env python3
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

RUNTIMEC2 = Path(__file__).resolve().parents[1]
PROBE_PATH = RUNTIMEC2 / "tools" / "binir_format_probe.py"
FIXTURE_PATH = RUNTIMEC2 / "tests" / "fixtures" / "binir" / "minimal-shapes.json"
SPEC = importlib.util.spec_from_file_location("binir_format_probe", PROBE_PATH)
assert SPEC and SPEC.loader
probe = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = probe
SPEC.loader.exec_module(probe)


class BinirFormatProbeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = probe.build_report(probe.load_corpus([FIXTURE_PATH]))

    def test_tagged_u8_boundary_has_only_128_indexes(self):
        fixed = self.report["candidates"]["fixed8"]["per_function"]
        boundary = next(
            item for item in fixed if item["function"] == "/fixture/tagged_index_boundary"
        )
        self.assertEqual(boundary["narrow_instructions"], 1)
        self.assertEqual(boundary["wide_instructions"], 2)
        self.assertEqual(boundary["index_overflows"]["constant"], 1)
        self.assertEqual(boundary["index_overflows"]["read_register"], 1)
        self.assertEqual(boundary["index_overflows"]["write_register"], 1)
        self.assertEqual(boundary["index_overflows"]["register"], 2)

        codeunit = self.report["candidates"]["codeunit16"]["per_function"]
        boundary16 = next(
            item for item in codeunit if item["function"] == "/fixture/tagged_index_boundary"
        )
        self.assertEqual(boundary16["narrow_instructions"], 3)
        self.assertEqual(boundary16["index_overflows"]["constant"], 0)
        self.assertEqual(boundary16["index_overflows"]["register"], 0)

    def test_shape_extension_reasons_are_separate(self):
        fixed = self.report["candidates"]["fixed8"][
            "extension_instructions_by_reason"
        ]
        self.assertEqual(fixed["three_operand_branch"], 1)
        self.assertEqual(fixed["array_operation"], 1)
        self.assertEqual(fixed["multi_argument_call"], 1)
        self.assertEqual(fixed["multi_return"], 1)

        opcode_specific = self.report["candidates"]["opcode_specific"]
        self.assertEqual(
            opcode_specific["extension_instructions_by_reason"]["three_operand_branch"], 0
        )
        self.assertEqual(
            opcode_specific["extension_instructions_by_reason"]["array_operation"], 0
        )

    def test_metadata_costs_are_nonzero_and_auditable(self):
        metadata = self.report["metadata"]
        self.assertEqual(metadata["source_pc_map"]["known_source_entries"], 13)
        self.assertEqual(metadata["source_pc_map"]["bytes"], 13 * 8)
        self.assertEqual(metadata["effect_map"]["bytes"], 4)
        self.assertEqual(metadata["effect_map"]["set_bits"], 3)
        self.assertEqual(metadata["safepoint_map"]["bytes"], 4)
        self.assertEqual(metadata["safepoint_map"]["set_bits"], 2)
        self.assertEqual(metadata["side_exit_map"]["entries"], 3)
        self.assertEqual(metadata["side_exit_map"]["bytes"], 36)
        self.assertEqual(metadata["total_bytes"], 148)

    def test_candidate_totals_and_per_function_distribution(self):
        self.assertEqual(self.report["totals"], {"functions": 4, "instructions": 13})
        for candidate in self.report["candidates"].values():
            self.assertEqual(candidate["instructions"], 13)
            self.assertEqual(len(candidate["per_function"]), 4)
            self.assertEqual(
                candidate["narrow_instructions"] + candidate["wide_instructions"],
                13,
            )
            self.assertEqual(
                candidate["code_plus_metadata_bytes"],
                candidate["code_bytes"] + self.report["metadata"]["total_bytes"],
            )

    def test_repeated_cli_output_is_byte_stable(self):
        command = [sys.executable, str(PROBE_PATH), str(FIXTURE_PATH)]
        first = subprocess.run(command, check=True, capture_output=True).stdout
        second = subprocess.run(command, check=True, capture_output=True).stdout
        self.assertEqual(first, second)
        parsed = json.loads(first)
        self.assertEqual(parsed["schema"], probe.REPORT_SCHEMA)
        self.assertTrue(parsed["corpus"]["fixture_only"])

    def test_declared_counts_are_validated(self):
        invalid = {
            "schema": probe.INPUT_SCHEMA,
            "corpus": "invalid",
            "provenance": "test",
            "fixture_only": True,
            "functions": [
                {
                    "name": "/invalid",
                    "register_count": 1,
                    "constant_count": 0,
                    "instructions": [
                        {
                            "op": "copy",
                            "family": "generic",
                            "reads": [{"kind": "reg", "index": 1}],
                            "writes": [0],
                        }
                    ],
                }
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.json"
            path.write_text(json.dumps(invalid), encoding="utf-8")
            with self.assertRaisesRegex(probe.ProbeError, "outside declared count"):
                probe.load_corpus([path])


if __name__ == "__main__":
    unittest.main()
