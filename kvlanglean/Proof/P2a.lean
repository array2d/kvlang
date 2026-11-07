import Proof.Basic
import Proof.Lemmas

namespace Kvlang

/-- 恢复暂停前的状态：只要暂停前是 running，`resume (pause s) = s`。 -/
theorem resume_pause (s : State) (h : s.status = .running) :
    resume (pause s) = s := by
  cases s with
  | mk data pc status =>
      simp [pause, resume]
      exact h.symm

/-- P2a：stop/resume 在任意指令边界语义不变。

    前提：暂停发生在指令边界，且暂停前控制状态是 `running`。
    结论：另起进程从同一 PC 恢复执行，与从未暂停执行得到同一个状态。
    证明：`pause` 只改 status，`resume` 把它改回来；执行本身不读 status。 -/
theorem p2a_stop_resume (p : List Instr) (s : State) (n : Nat)
    (h : s.status = .running) :
    runFuel p n (resume (pause s)) = runFuel p n s := by
  rw [resume_pause s h]

/-- P2a 的分段形态：先跑 n 步、停在指令边界，再从该边界跑 m 步，
    与一次跑 n + m 步等价。暂停/恢复没有改写数据或 PC。 -/
theorem p2a_boundary_decomposition (p : List Instr) (s : State) (n m : Nat) :
    runFuel p (n + m) s = runFuel p m (runFuel p n s) :=
  runFuel_add p n m s

end Kvlang
