#!/usr/bin/env bash
# Run every check the CI runs. Usage, from anywhere: formal/scripts/check.sh
# The external tools are built once into .lake/tools (about 300 MB).
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.elan/bin:$HOME/.cargo/bin:$PATH"
TOOLS="$(pwd)/.lake/tools"
mkdir -p "$TOOLS"

echo "== 1/4 lake build --wfail: every proof, warnings are errors"
lake build --wfail

echo "== 2/4 axiom-audit: every declaration under Pathlang uses only propext, Classical.choice, Quot.sound"
AXIOM_AUDIT_REF="v0.1.2"
AXIOM_AUDIT_SHA="46024e005996495c65ef609368e11ab39c4222e3"
if [ ! -x "$TOOLS/axiom-audit/.lake/build/bin/axiom-audit" ]; then
  rm -rf "$TOOLS/axiom-audit"
  git -c advice.detachedHead=false clone -q --depth 1 --branch "$AXIOM_AUDIT_REF" \
    https://github.com/leanprover-community/axiom-audit.git "$TOOLS/axiom-audit"
  cp lean-toolchain "$TOOLS/axiom-audit/"
  (cd "$TOOLS/axiom-audit" && lake build)
fi
test "$(git -C "$TOOLS/axiom-audit" rev-parse HEAD)" = "$AXIOM_AUDIT_SHA"
lake env "$TOOLS/axiom-audit/.lake/build/bin/axiom-audit" \
  --allow propext,Classical.choice,Quot.sound

echo "== 3/4 leanchecker: replay every module into Lean's kernel"
lake env leanchecker

echo "== 4/4 nanoda: independent kernel"
scripts/check_nanoda.sh "$TOOLS"

echo "all checks passed"
