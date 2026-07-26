package keytree

// 路径与成员分隔符统一管理。所有构造 KV 路径、解析限定名、成员访问的地方
// 均须使用这些常量，禁止硬编码 "." 等裸字符串。

const (
	// ── 分隔符 ─────────────────────────────────────────────────────────

	FuncPathSep = "." // 包名与函数名分隔符，用法：/lib/<pkg><FuncPathSep><name>
	MemberSep   = "." // 成员访问分隔符，X<MemberSep>a 与 X/a 结构分隔正交
	ReservedPrefix = "." // 引擎保留字段前缀，用户代码无法写入
	SrcExt      = ".src" // 函数源码文件后缀

	// ── 路径段名 ─────────────────────────────────────────────────────────

	PathSegSep      = "/"        // 路径分隔符（对齐 kvspace.PathSep）
	PathSegLib      = "lib"      // /lib — 函数库根
	PathSegVthread  = "vthread"  // /vthread — 虚线程根

	SegRO       = "ro"       // 只读参数名单
	SegRParam   = "rparam"   // 读参重定向
	SegWParam   = "wparam"   // 写参重定向


	SegPC       = "pc"       // 当前 PC
	SegStatus   = "status"   // 生命周期状态
	SegCtime    = "ctime"    // 创建时刻
	SegDebugger = "debugger" // 调试控制键
	SegPause    = "pause"    // 暂停事件
	SegResume   = "resume"   // 恢复命令
	SegMsg      = "msg"      // 终态附加描述
	SegTerm     = "term"     // 绑定终端名
	SegSeq      = "seq"      // vtid 自增序列

	SegDev  = "dev"  // /dev
	SegTTY  = "tty"  // /dev/tty
	SegSys  = "sys"  // /sys
	SegOp   = "op"   // /sys/op
	SegCmd  = "cmd"  // 命令队列
	SegFunc = "func" // 算子函数定义
)
