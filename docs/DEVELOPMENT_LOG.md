# PrimeMarkdown Upsilon 移植 — 开发经验总结

> 从「Python 后门引导」到「Upsilon native app」的全过程复盘，
> 含事件系统排查史、崩溃二分诊断法、最终架构与教训。

## 1. 最终形态（v10，已真机验证）

```
Upsilon external app: c:\apps\primemarkdown.elf
构建: Makefile.upsilon (g1app.ld + AppMain 入口 + libsyscalls + newlib + libstdc++)
入口: int main() (besta_fixes.cc 的 AppMain 自动调用)
事件: 主线程 GetEvent(0x1003F) 阻塞 + 死循环 (不 return)
渲染: FreeType + HarfBuzz + LaTeX 公式引擎 (probe4 验证全通)
```

**已真机确认**：正常渲染、按键/触屏翻页流畅。剩余：退出机制、文件读取。

## 2. 决策历程（为什么从 Python 引导转到 Upsilon）

| 阶段 | 结论 |
|---|---|
| Python 后门引导 | 显示正常但**事件被 MicroPython 运行时独占**——GetEvent/TestPendEvent/GetPendEvent 全部拿不到按键/触摸（多轮日志证实） |
| 装双系统后 | Python 后门失效 → 必须 Upsilon 形态（AppMain + g1app.ld 0x30900000） |

**教训**：Python 引导的 ELF 是"Guest 模式"，事件队列归 Python 所有；Upsilon external app 才是"主人"，事件系统完整可用。

## 3. 事件系统排查史（SVC 逐个实测）

| SVC | 含义 | Python 引导 | Upsilon |
|---|---|---|---|
| 0x1003F GetEvent | 取事件（muteki 文档称阻塞） | ❌ 拿不到 | ✅ **可用**（实测为阻塞：无事件时等待） |
| 0x10040 GetPendEvent | 非阻塞取挂起事件 | ❌ | ❌ 返回但不给事件 |
| 0x10046 TestPendEvent | 非阻塞探测 | ❌ 恒 false | ❌ |
| 0x1004A ClearAllEvents | 清事件队列 | — | ✅ 可用 |
| 0x1004B TestKeyEvent | 处理挂起按键 | 未测 | 未测 |

**关键发现**：
- **GetEvent 是阻塞的**（无事件时等待）——doom/eventg1 的事件线程靠它阻塞等待
- **GetPendEvent/TestPendEvent 在两种环境下都拿不到事件**（语义不明）
- **主线程 GetEvent + return 0 = 必然重启**（Upsilon 清理路径崩）
- **GetEvent 在 OSCreateThread 子线程 + return = 偶发重启**（eventg1 官方示例实测，绝大多数正常）

## 4. 崩溃二分诊断法（probe 系列）

**方法论**：每步一个探针 ELF，用「全屏色块」代替 Printf 日志（Printf 会被渲染清屏覆盖、且显示乱码），用户只需报"最后看到的颜色"。

| probe | 内容 | 结论 |
|---|---|---|
| probe | 纯 Printf（无渲染） | 构建/部署/heap/线程/LCD 全正常 |
| probe2 | + 渲染核心 | init OK、clear 变白 |
| probe3 | 异常机制 + heap | heap 8MB+ ✅、throw/catch ✅ |
| probe4 | 渲染核心色块 | init/formula/present 全通 |
| probe5 | + SetSystemVariable/ClearScreen/事件线程 | 线程创建后崩 |
| probe6/7 | 主线程 GetEvent | **运行正常**，return 必重启 |
| probe8 | GetPendEvent/TestPendEvent | 退出正常但拿不到事件 |
| probe10 | 主线程 GetEvent + OSExitThread | 运行正常，OSExitThread 卡死 |
| probe13 | 前置 SVC + 4 公式 + GetEvent | 渲染全通，按键一次后挂起（前置 SVC 副作用） |
| **v6/v7** | probe13 基座 + 去 fill + 按键响应 | **成功！** 切换流畅 |

**教训**：
- **一个探针只改一个变量**（色块定位）
- **以能跑的版本为基座做最小修改**（v6 严格基于 probe13），不要从零重写
- OSSleep 时序可能是"能跑"的隐性条件（probe13 每步 400ms 间隔）；v7 去掉渲染内间隔后仍正常（说明时序非必需，但修改要逐步）

## 5. 最终架构要点（v7）

```cpp
main:
  Printf 启动
  SetSystemVariable(85,2,1)      // 前置 (probe13 原样, 删了会重启!)
  ClearScreen(0)
  prime_render_init()            // 字体 + 公式引擎
  render_page(0)                 // 渲染页 (无 OSSleep, 一次 present)
  for(;;) {                      // 死循环, 永不 return
    if (!GetEvent(buf)) { OSSleep(30); continue; }   // 主线程 GetEvent
    keydown: ENTER/DOWN/右/触屏 → 下一页; UP/左 → 上一页
    ClearAllEvents()             // 清堆积, 防连翻
    OSSleep(30)
  }
```

**已验证的关键组合**（改动需谨慎）：
- 前置 `SetSystemVariable(85,2,1)` + `ClearScreen(0)` **不能删**（v4 删了重启；probe13 有前置能跑）
- 主线程 GetEvent 阻塞 + 死循环 = 稳定运行
- 按键处理后 `ClearAllEvents` = 防事件堆积连翻
- 渲染页无 OSSleep = 切换即时（<50ms）

## 6. 工具链与构建要点

- 工具链：Ubuntu `gcc-arm-none-eabi` 系，**无 root 解包**到 `DOOM/armtc/root`（/tmp 会被环境清理，放工作区持久路径）
- **含空格路径**：Makefile 的 `-I/-L/-B` 参数必须加引号（sh 会拆分）
- **nano.specs**：Ubuntu 无 newlib-nano 包 → 链接绕过：`-lc`（完整 newlib）替代 `--specs=nano.specs`
- **-fpermissive**：besta_fixes.cc 宽松编译必需
- **--allow-multiple-definition**：容忍 SDK 与 libc 的少量重复符号
- C++ 异常（libstdc++）：Upsilon 下可用（probe3 验证 throw/catch）；`__gnu_Unwind_Find_exidx` 需自实现（stubs）
- 事件缓冲必须 ≥72 字节（ui_event_t 完整结构，eventg1 打印到 ptr[16]；32 字节会越界写栈）

## 7. 主要经验教训

1. **事件在"宿主"手里**：Python 引导的 ELF 拿不到事件（宿主=MicroPython）；Upsilon app 拿得到（宿主=自己）
2. **退出是 Upsilon 的雷区**：`return 0` 可能重启（主线程 GetEvent 后必现）；OSExitThread 卡死；**唯一稳定 = 死循环 + 系统级退出**（doom/chess 惯例）
3. **前置 SVC 是玄学**：`SetSystemVariable(85,2,1)`+`ClearScreen(0)` 删了重启、留着正常——doom 的初始化序列不能随意裁剪
4. **色块 > Printf 日志**：Printf 会被渲染覆盖、显示乱码；全屏色块编码状态最可靠
5. **以验证过的版本为基座**：直接改 probe13（能跑）比重写 main（每次崩）成功率高得多——"能跑"包含了很多隐性条件
6. **工具链可移植性**：/tmp 清理、路径空格、nano.specs 缺失——每个都是坑，Makefile 已固化

## 8. 后续路线（一步步来）

1. **按键退出**（当前）：Esc → 尝试 `ClearAllEvents + return 0`（最小改动测试）；若重启 → 事件线程版（eventg1 完整结构，主线程轮询共享 + OSTerminateThread + return，偶发重启可接受）
2. **文件读取**：Upsilon 文件 SVC（`_afopen`/`_fread`）读 `c:\apps\*.tex`，或嵌入公式库
3. **实时渲染**：输入 LaTeX → 即时渲染（事件已通，输入法映射是下一步）
