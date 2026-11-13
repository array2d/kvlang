#!/usr/bin/env bash
# Type-check the compiled library with nanoda, an independent Lean 4 kernel
# written in Rust. Run from the package root after `lake build`.
#
# lean-action's built-in `nanoda: true` clones nanoda's `debug` branch, which
# reads only the old text export format; current lean4export writes JSON. This
# script pins versions that work together instead.
#
# Usage: scripts/check_nanoda.sh [tools-dir]   (default: .lake/tools)
set -euo pipefail

LEAN4EXPORT_REF="v4.35.0"   # lean4export tag matching lean-toolchain
NANODA_SHA="8de7e0c4ed3052745a7f8725164efd9e76d84c56"   # nanoda_lib master, 2026-10-29

ROOT="$(pwd)"
TOOLS="${1:-$ROOT/.lake/tools}"
mkdir -p "$TOOLS"
export PATH="$HOME/.elan/bin:$HOME/.cargo/bin:$PATH"

if ! command -v cargo >/dev/null; then
  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs |
    sh -s -- -y --default-toolchain stable --profile minimal --no-modify-path
fi

if [ ! -x "$TOOLS/lean4export/.lake/build/bin/lean4export" ]; then
  rm -rf "$TOOLS/lean4export"
  git -c advice.detachedHead=false clone -q --depth 1 --branch "$LEAN4EXPORT_REF" \
    https://github.com/leanprover/lean4export.git "$TOOLS/lean4export"
  cp "$ROOT/lean-toolchain" "$TOOLS/lean4export/"
  (cd "$TOOLS/lean4export" && lake build)
fi

if [ ! -x "$TOOLS/nanoda_lib/target/release/nanoda_bin" ]; then
  rm -rf "$TOOLS/nanoda_lib"
  git clone -q https://github.com/ammkrn/nanoda_lib.git "$TOOLS/nanoda_lib"
  git -C "$TOOLS/nanoda_lib" checkout -q "$NANODA_SHA"
  (cd "$TOOLS/nanoda_lib" && cargo build --release)
fi
test "$(git -C "$TOOLS/nanoda_lib" rev-parse HEAD)" = "$NANODA_SHA"

EXPORT="$TOOLS/export.ndjson"
trap 'rm -f "$EXPORT"' EXIT
lake env "$TOOLS/lean4export/.lake/build/bin/lean4export" Pathlang > "$EXPORT"
echo "export: $(wc -l < "$EXPORT") lines"

# Lean's three standard axioms, plus Lean.trustCompiler, which some core
# library declarations use; axiom-audit separately checks that no declaration
# under Pathlang uses it. An unpermitted axiom such as sorryAx is skipped where
# it is declared, and any declaration that uses it is a hard error.
cat > "$TOOLS/nanoda.json" <<EOF
{
  "export_file_path": "$EXPORT",
  "use_stdin": false,
  "num_threads": 4,
  "permitted_axioms": ["propext", "Classical.choice", "Quot.sound", "Lean.trustCompiler"],
  "unpermitted_axiom_hard_error": false,
  "nat_extension": true,
  "string_extension": true,
  "print_success_message": true,
  "pp_to_stdout": true
}
EOF

"$TOOLS/nanoda_lib/target/release/nanoda_bin" "$TOOLS/nanoda.json"
