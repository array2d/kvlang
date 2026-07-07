package keytree

import "fmt"

// OpBackendFunc 返回 /op/<backend>/func/<name>
func OpBackendFunc(backend, name string) string { return fmt.Sprintf("/op/%s/func/%s", backend, name) }

// OpBackendList 返回 /op/<backend>/list
func OpBackendList(backend string) string { return "/op/" + backend + "/list" }

// OpPattern 返回 /op/*/list (SCAN 用)
func OpPattern() string { return "/op/*/list" }
