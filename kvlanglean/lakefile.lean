import Lake
open Lake DSL

package kvlang_proof

-- 本批证明只用 Lean 4 核心库（List / Nat / Option），不需要 Mathlib。
-- #572 允许 Mathlib；这里保持零第三方依赖，保证离线可复现。
-- 若后续证明需要 Mathlib，在此加：
--   require mathlib from git "https://github.com/leanprover-community/mathlib4.git" @ "v4.8.0"

@[default_target]
lean_lib Proof where
  roots := #[`Proof]
