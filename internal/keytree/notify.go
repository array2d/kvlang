package keytree

// NotifyRoot 是 VM 通知队列的根路径。
const NotifyRoot = "/notify"

// NotifyVM 返回 /notify/vm（VM 新 vthread 通知队列）
const NotifyVM = "/notify/vm"

// DoneRoot 是完成通知的根路径。
const DoneRoot = "/done"

// Done 返回 /done/<vtid>
func Done(id string) string { return "/done/" + id }
