package keytree

const SysRoot = PathSegSep + SegSys

func SysOp(backend, n string) string { return SysRoot + PathSegSep + SegOp + PathSegSep + backend + PathSegSep + n }

func SysOpCmd(backend, n string) string { return SysOp(backend, n) + PathSegSep + SegCmd }

func SysOpFunc(backend, name string) string { return SysRoot + PathSegSep + SegOp + PathSegSep + backend + PathSegSep + SegFunc + PathSegSep + name }

const SysOpRoot = PathSegSep + SegSys + PathSegSep + SegOp
