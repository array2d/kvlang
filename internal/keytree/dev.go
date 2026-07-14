package keytree

// DevTTY 返回 /dev/tty/<name>/<stream>
// stream ∈ {stdin, stdout, stderr, pty}
func DevTTY(name, stream string) string { return "/dev/tty/" + name + "/" + stream }
