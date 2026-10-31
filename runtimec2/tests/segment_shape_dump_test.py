#!/usr/bin/env python3
import argparse
import ctypes
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command: list[str], *, cwd: Path, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[bytes]:
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True)
    if result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout.decode(errors='replace')}\n"
            f"stderr:\n{result.stderr.decode(errors='replace')}"
        )
    return result


def scaled_source(source: Path, scale: int) -> str:
    text = source.read_text(encoding="utf-8")
    if "__SCALE__" not in text:
        raise AssertionError(f"benchmark source has no __SCALE__: {source}")
    return text.replace("__SCALE__", str(scale))


def set_i64_bodies(library: Path, dsn: str, keys: list[str], value: int) -> None:
    kvspace = ctypes.CDLL(str(library))
    kvspace.kvspaceConnect.argtypes = [ctypes.c_char_p]
    kvspace.kvspaceConnect.restype = ctypes.c_void_p
    kvspace.kvspaceClose.argtypes = [ctypes.c_void_p]
    kvspace.kvspaceSetPart.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_uint32,
        ctypes.c_void_p,
        ctypes.c_uint32,
        ctypes.c_char_p,
        ctypes.c_uint32,
    ]
    kvspace.kvspaceSetPart.restype = ctypes.c_int
    handle = kvspace.kvspaceConnect(dsn.encode())
    if not handle:
        raise AssertionError("cannot connect to mutate real Segment targets")
    try:
        encoded = value.to_bytes(8, "little", signed=True)
        for key in keys:
            body = ctypes.create_string_buffer(encoded)
            error = ctypes.create_string_buffer(256)
            result = kvspace.kvspaceSetPart(
                handle, key.encode(), 8, body, 8, error, len(error)
            )
            if result != 0:
                raise AssertionError(
                    f"cannot set {key} to {value}: {error.value.decode(errors='replace')}"
                )
    finally:
        kvspace.kvspaceClose(handle)


def assert_canonical(raw: bytes) -> dict:
    document = json.loads(raw)
    canonical = (
        json.dumps(document, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode()
    if raw != canonical:
        raise AssertionError("stdout is deterministic but not canonical compact JSON")
    return document


def assert_virtual_exit_targets(
    document: dict, goto_source: int, branch_source: int
) -> None:
    function = next(item for item in document["functions"] if item["name"] == "/lib/iops")
    virtual_exit_index = len(function["instructions"])
    goto_seen = False
    br_seen = False
    for instruction_index, instruction in enumerate(function["instructions"]):
        expected = virtual_exit_index - instruction_index
        if instruction["op"] == "goto" and instruction["source_id"] == goto_source:
            if instruction["targets"] != [expected]:
                raise AssertionError("goto virtual-exit displacement is wrong")
            goto_seen = True
        if instruction["op"] == "br" and instruction["source_id"] == branch_source:
            if len(instruction["targets"]) != 2 or instruction["targets"][1] != expected:
                raise AssertionError("br virtual-exit displacement is wrong")
            br_seen = True
    if not goto_seen or not br_seen:
        raise AssertionError("virtual exit test did not observe both goto and br")


def assert_corpus(document: dict) -> None:
    if document.get("schema") != "kvlang.binir-probe.v1":
        raise AssertionError("unexpected exporter schema")
    if document.get("fixture_only") is not False:
        raise AssertionError("real Segment corpus must not be fixture_only")
    provenance = document.get("provenance", "")
    for phrase in ("build_segment()", "Segment::insns/regs/lit", "no effect"):
        if phrase not in provenance:
            raise AssertionError(f"missing provenance phrase: {phrase}")

    functions = document.get("functions")
    if [function.get("name") for function in functions] != ["/lib/fib", "/lib/iops"]:
        raise AssertionError("function order or names are not canonical")
    if not all(function.get("instructions") for function in functions):
        raise AssertionError("exported function has no real Segment instructions")

    saw_branch = False
    saw_call = False
    saw_const = False
    saw_reg = False
    for function in functions:
        register_count = function["register_count"]
        constant_count = function["constant_count"]
        source_ids = []
        for instruction in function["instructions"]:
            source_ids.append(instruction["source_id"])
            if instruction.get("effect") is not False:
                raise AssertionError("effect must be explicit false")
            if instruction.get("safepoint") is not False:
                raise AssertionError("safepoint must be explicit false")
            if instruction.get("side_exits") != []:
                raise AssertionError("side_exits must be explicit empty")
            for operand in instruction.get("reads", []):
                if operand["kind"] == "reg":
                    saw_reg = True
                    if not 0 <= operand["index"] < register_count:
                        raise AssertionError("RegId outside register_count")
                elif operand["kind"] == "const":
                    saw_const = True
                    if not 0 <= operand["index"] < constant_count:
                        raise AssertionError("ConstId outside constant_count")
                else:
                    raise AssertionError("unknown read operand kind")
            for register in instruction.get("writes", []):
                if not 0 <= register < register_count:
                    raise AssertionError("write RegId outside register_count")
            if instruction["family"] == "branch":
                saw_branch = True
                if not instruction.get("targets"):
                    raise AssertionError("branch lacks relative targets")
            if instruction["family"] == "call":
                saw_call = True
                if not isinstance(instruction.get("immediate"), int):
                    raise AssertionError("call lacks callee-pool immediate")
        if source_ids != sorted(source_ids) or len(source_ids) != len(set(source_ids)):
            raise AssertionError("source_id order is not stable")
    if not all((saw_branch, saw_call, saw_const, saw_reg)):
        raise AssertionError("real iops/fib corpus did not exercise required shapes")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exporter", required=True, type=Path)
    parser.add_argument("--byteseek", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    args = parser.parse_args()

    root = args.source_root.resolve()
    kvspace_library = root / "lib/libkvspace.so"
    for path in (args.exporter, args.byteseek, args.probe, kvspace_library):
        if not path.is_file():
            raise AssertionError(f"required tool does not exist: {path}")

    with tempfile.TemporaryDirectory(prefix="kvlang-segment-shape-") as directory:
        temporary = Path(directory)
        iops = temporary / "iops.kv"
        fib = temporary / "fib.kv"
        iops.write_text(
            scaled_source(root / "benchmark/cases/iops/iops.kv", 2), encoding="utf-8"
        )
        fib.write_text(
            scaled_source(root / "benchmark/cases/fib/fib.kv", 5), encoding="utf-8"
        )
        dsn = f"shm://{temporary}/segments.shm"
        env = {**os.environ, "KVSPACE": dsn}

        run([str(args.byteseek), "isa", "publish"], cwd=root, env=env)
        run([str(args.byteseek), "layout", str(iops)], cwd=root, env=env)
        run([str(args.byteseek), "layout", str(fib)], cwd=root, env=env)

        command = [str(args.exporter), dsn, "/lib/iops/", "/lib/fib"]
        first = run(command, cwd=root)
        second = run(command, cwd=root)
        if first.stdout != second.stdout:
            raise AssertionError("same store did not produce byte-identical JSON")
        if b"no effect/safepoint/side-exit metadata" not in first.stderr:
            raise AssertionError("missing metadata limitation diagnostic")

        document = assert_canonical(first.stdout)
        assert_corpus(document)

        iops = next(item for item in document["functions"] if item["name"] == "/lib/iops")
        goto_source = next(
            instruction["source_id"]
            for instruction in iops["instructions"]
            if instruction["op"] == "goto"
        )
        branch_source = next(
            instruction["source_id"]
            for instruction in iops["instructions"]
            if instruction["op"] == "br"
        )
        virtual_exit_source = len(iops["instructions"]) + 1
        set_i64_bodies(
            kvspace_library,
            dsn,
            [
                f"/lib/iops/[{goto_source},-1]",
                f"/lib/iops/[{branch_source},-3]",
            ],
            virtual_exit_source,
        )
        virtual_first = run(command, cwd=root)
        virtual_second = run(command, cwd=root)
        if virtual_first.stdout != virtual_second.stdout:
            raise AssertionError("virtual-exit export is not byte-identical")
        document = assert_canonical(virtual_first.stdout)
        assert_corpus(document)
        assert_virtual_exit_targets(document, goto_source, branch_source)

        corpus_path = temporary / "real-segments.json"
        corpus_path.write_bytes(virtual_first.stdout)
        probe = run(
            [sys.executable, str(args.probe), "--compact", str(corpus_path)], cwd=root
        )
        report = json.loads(probe.stdout)
        if report.get("corpus", {}).get("fixture_only") is not False:
            raise AssertionError("probe report incorrectly marked real corpus fixture-only")
        if report.get("totals", {}).get("functions") != 2:
            raise AssertionError("probe did not consume both real Segment functions")
        expected_instructions = sum(
            len(function["instructions"]) for function in document["functions"]
        )
        if report.get("totals", {}).get("instructions") != expected_instructions:
            raise AssertionError("probe instruction total differs from exporter")

        set_i64_bodies(
            kvspace_library,
            dsn,
            [f"/lib/iops/[{branch_source},-2]"],
            virtual_exit_source + 1,
        )
        invalid = subprocess.run(command, cwd=root, capture_output=True)
        # 目标缺失既可能在导出期（旧路径 targets missing source id）被发现，也可能
        # 在建段编码期（binir encode failed）被拒绝；两者都算「未被当成虚拟出口」。
        stderr = invalid.stderr
        if (
            invalid.returncode == 0
            or b"/lib/iops" not in stderr
            or (b"binir encode failed" not in stderr and b"targets missing" not in stderr)
        ):
            raise AssertionError("an arbitrary missing target was accepted as virtual exit")

    print(
        "OK real Segment exporter: iops/fib layout, goto/br virtual exit, "
        "stable JSON, probe accepted"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
