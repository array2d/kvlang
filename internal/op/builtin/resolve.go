package builtin

// isRelative 判断参数是否为 vthread 相对路径 (./ 开头)。
func isRelative(param string) bool {
	return len(param) >= 2 && param[:2] == "./"
}

// resolveWriteKey 将相对路径转为 Redis 绝对 key。
func resolveWriteKey(vtid, param string) string {
	if isRelative(param) {
		return "/vthread/" + vtid + "/" + param[2:]
	}
	return param
}
