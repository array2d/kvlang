package keytree

import "fmt"

const OpRoot = "/op"
const opPrefix = "/op/"

// OpBackendFunc 返回 /op/<backend>/func/<name>
func OpBackendFunc(backend, name string) string { return fmt.Sprintf(opPrefix+"%s/func/%s", backend, name) }

// OpBackendList 返回 /op/<backend>/list
func OpBackendList(backend string) string { return opPrefix + backend + "/list" }

// OpPattern 返回 /op/*/list (SCAN 用)
func OpPattern() string { return opPrefix + "*/list" }
