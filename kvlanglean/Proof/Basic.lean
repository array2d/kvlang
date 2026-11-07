/-
  kvlang 崩溃恢复契约：抽象解释器模型。

  这里只建模 P2a/P2b 与三条引理需要的部分：
  - kvspace 数据面 (Store)；
  - 一条 BinRWIR 指令的读集、写集、inplace 标记与效果；
  - 指令边界上的执行、暂停/恢复、崩溃中断。

  线格式（head/body/attr 的字节布局）由 P1 负责，本模型把 Value 视为不透明字节串，
  并把「单 key 一次写原子」作为 P1 的结论直接使用（见 lemma1）。
-/

namespace Kvlang

/-- 持久键：kvspace 路径的抽象。 -/
abbrev Key := String

/-- 值：不透明字节串；线格式由 P1 展开。 -/
abbrev Value := ByteArray

/-- 持久数据面：key 到值的部分映射。 -/
abbrev Store := Key → Option Value

/-- 写集：key 到新值的部分映射。`none` 表示这条指令不写该 key。 -/
abbrev Writes := Key → Option Value

/-- 写一个 key（单 key 语义原语）。 -/
def writeKey (s : Store) (k : Key) (v : Value) : Store :=
  fun k' => if k' = k then some v else s k'

/-- 把写集应用到 store：写到的 key 取新值，没写到的保留旧值。
    逐 key 的匹配直接给出「单 key 写原子」——不存在半写值。 -/
def applyWrites (s : Store) (w : Writes) : Store :=
  fun k =>
    match w k with
    | some v => some v
    | none => s k

/-- 单 key 写集。 -/
def singleWrite (k : Key) (v : Value) : Writes :=
  fun k' => if k' = k then some v else none

/-- 引理 1：单 key 写原子（P1 的 wire 模型直接给出）。
    这里证明它在本模型里的列表/映射语义：单 key 写集与 `writeKey` 等价。 -/
theorem lemma1_single_key_write_atomic (s : Store) (k : Key) (v : Value) :
    applyWrites s (singleWrite k v) = writeKey s k v := by
  funext k'
  by_cases h : k' = k <;> simp [applyWrites, singleWrite, writeKey, h]

/-- `w'` 是 `w` 的子写集：`w'` 写到的每个 key，`w` 也写到同一个值。 -/
def PartialLe (w' w : Writes) : Prop :=
  ∀ k v, w' k = some v → w k = some v

/-- vthread 控制状态；词表与 spec 一致。 -/
inductive Status where
  | init
  | running
  | wait
  | paused
  | error
  deriving DecidableEq, Repr

/-- 一条指令的静态形状与效果。
    `effect` 是本次执行真正要提交的写集；
    读集与写集用于依赖图与 inplace 判定。 -/
structure Instr where
  reads : List Key
  writes : List Key
  inplace : Bool
  effect : Store → Writes

/-- 指令边界上的机器状态。
    PC 约定：`pc` 指向**当前尚未执行**的那条指令（下一条待执行指令）。
    崩溃落在指令 c 时，`pc` 仍指向 c，恢复就从 c 重放；
    实现里若 `‥pc` 已推进到 c+1，恢复规则说的「PC-1」就是本模型的 c。 -/
structure State where
  data : Store
  pc : Nat
  status : Status

/-- 执行一条指令：提交效果，PC 前进一条。 -/
def step (s : State) (i : Instr) : State :=
  { data := applyWrites s.data (i.effect s.data), pc := s.pc + 1, status := s.status }

/-- 带燃料的执行；燃料耗尽或越过程序末尾即停。 -/
def runFuel (p : List Instr) : Nat → State → State
  | 0, s => s
  | n + 1, s =>
      match p[s.pc]? with
      | none => s
      | some i => runFuel p n (step s i)

/-- 从当前 PC 把程序跑完。 -/
def run (p : List Instr) (s : State) : State :=
  runFuel p (p.length + 1) s

/-- 崩溃中断：指令只落库了子写集 `partialWrites`，PC 仍指向该指令（未前进）。 -/
def interrupted (s : State) (partialWrites : Writes) : State :=
  { data := applyWrites s.data partialWrites,
    pc := s.pc,
    status := s.status }

/-- 主动暂停：只改控制状态，数据与 PC 不动。 -/
def pause (s : State) : State :=
  { s with status := .paused }

/-- 恢复：把控制状态改回 running；数据与 PC 保持暂停时的值。 -/
def resume (s : State) : State :=
  { s with status := .running }

end Kvlang
