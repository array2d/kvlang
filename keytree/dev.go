package keytree

func DevTTY(name, stream string) string {
	return PathSegSep + SegDev + PathSegSep + SegTTY + PathSegSep + name + PathSegSep + stream
}
