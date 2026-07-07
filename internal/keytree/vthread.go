package keytree

import "fmt"

// VThread 返回 /vthread/<id>
func VThread(id string) string { return "/vthread/" + id }

// VThreadAt 返回 /vthread/<id>/<pc>
func VThreadAt(id, pc string) string { return "/vthread/" + id + "/" + pc }

// VThreadSub 返回 /vthread/<id>/<pc>/  (子栈前缀)
func VThreadSub(id, pc string) string { return "/vthread/" + id + "/" + pc + "/" }

// VThreadSlot 返回 /vthread/<id>/[a,b]
func VThreadSlot(id string, a, b int) string { return fmt.Sprintf("/vthread/%s/[%d,%d]", id, a, b) }

// VThreadTerm 返回 /vthread/<id>/term
func VThreadTerm(id string) string { return "/vthread/" + id + "/term" }

// VThreadPattern 返回 /vthread/* (SCAN 用)
func VThreadPattern() string { return "/vthread/*" }
