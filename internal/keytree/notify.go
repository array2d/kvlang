package keytree

// NotifyVM 返回 notify:vm
const NotifyVM = "notify:vm"

// Done 返回 done:<vtid>
func Done(id string) string { return "done:" + id }
