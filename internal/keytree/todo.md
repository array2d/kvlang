# keytree 设计问题

## P0-9 `FrameRoot` 和 `ChildFrameRoot` 存在"不应发生"的兜底分支
**文件**：`frame.go`  

`FrameRoot` 注释：
> 兜底：去掉最后一段（不应发生于链接帧）

`ChildFrameRoot` 注释：
> 无 `/_fn/`：初始调用时 callPC 形如 /vthread/vtid/[0,0]

数学上完备的函数不需要"不应发生"的分支。
这两个兜底分支的存在说明调用方在某些路径下传入了不符合链接帧规范的 pc。
应消除产生不合规 pc 的源头，让这些分支真正不可达，或删除兜底并在调用方加断言。
