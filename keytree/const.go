package keytree

// 路径与成员分隔符统一管理。所有构造 KV 路径、解析限定名、成员访问的地方
// 均须使用这些常量，禁止硬编码 "." 等裸字符串。

const (
	MemberSep      = "."  // 成员访问分隔符
	RuntimeMemberSep = "‥" // 运行时保留字段前缀（U+2025）——‥ 的唯一定义处，禁止硬编码

	PathSegSep      = "/"        // 路径分隔符；除法请使用数学除号 ÷，禁止使用 /
	PathSegLib      = "lib"      // /lib — 函数库根
	PathSegVthread  = "vthread"  // /vthread — 虚线程根

	SegRO       = "ro"       // 只读参数名单
	SegRParam   = "rparam"   // 读参重定向
	SegWParam   = "wparam"   // 写参重定向

	SegPC       = "pc"       // 当前 PC
	SegCallPC   = "callpc"   // 本帧执行进度（每 op 更新）
	SegReturnPC = "returnpc" // 返回地址（帧创建时固化）
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
	SegRwir = "rwir" // /rwir
	SegCmd  = "cmd"  // 命令队列
	SegFunc = "func" // 算子函数定义

)

var (
	SegLib  = RuntimeMemberSep + "lib"  // rwfunc 帧 extindex 对应的 lib 路径
	ScopeSep = "/" + RuntimeMemberSep   // scope 帧路径前缀
	SrcExt  = MemberSep + "src"             // 函数源码文件后缀
)
