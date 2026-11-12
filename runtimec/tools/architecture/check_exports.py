
from __future__ import annotations

import argparse
import platform
import subprocess
from pathlib import Path

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    arguments = parser.parse_args()

    expected = {
        line.strip()
        for line in arguments.manifest.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    system = platform.system()
    command = (["nm", "-gU", str(arguments.library)] if system == "Darwin"
               else ["nm", "-D", "--defined-only", str(arguments.library)])
    output = subprocess.run(
        command, check=True, text=True, stdout=subprocess.PIPE,
    ).stdout
    actual = set()
    for line in output.splitlines():
        if line.strip():
            symbol = line.split()[-1]
            if system == "Darwin" and symbol.startswith("_"):
                symbol = symbol[1:]
            actual.add(symbol)

    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    for symbol in missing:
        print(f"missing export: {symbol}")
    for symbol in extra:
        print(f"unexpected export: {symbol}")
    if missing or extra:
        return 1
    print(f"check_exports: OK ({len(actual)} symbol(s))")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
