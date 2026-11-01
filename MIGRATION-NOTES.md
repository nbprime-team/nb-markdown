# 迁移笔记：PrimeMarkdown（两条路线）

> ⚠️ AI 生成 / 辅助创作：DeepSeek V4.1 Flash（未人工审查）

## 两条路线，一个内核

- **路线 A（原始固件 + PrimeDebug）**：`Makefile.prime` → `build/prime/primetex.elf`
  → `.hpappdir` + MicroPython 启动器（本仓库 CI/日常用）。
- **路线 B（Upsilon 替代固件）**：`Makefile.upsilon` → `build/upsilon/primemarkdown.elf`
  → `c:\apps\`，入口 `AppMain`。

共享的只有渲染核心（`src/`、`include/`、`third_party/`）与 SVC 包装（`port/prime_svc.h`）。
**系统操作不得混用**：

| 关注点 | 路线 A | 路线 B |
|---|---|---|
| 加载 | Python + shellcode，任意基址、ET_DYN | Upsilon 启动器，固定 `0x30900000` |
| 入口 | `app_entry(BootConfig*)` | `AppMain(canary, heap, size)` |
| 堆 | SVC lmalloc | newlib `_sbrk` / OS 堆 |
| 输入 | `GetPendEvent` 轮询或主线程 `GetEvent` | 主线程 `GetEvent` |
| 头文件 | `port/prime_*.h` | `port/upsilon/*`、muteki 头 |

改 `src/` 时只允许依赖 `port/` 暴露的抽象；`port/prime_*` 与 `port/*upsilon*` 互不包含。

## primetex 为什么“能显示、不能按键/触摸”（已定位）

`primetex.elf` 由 `Makefile.prime` 链接 `port/prime_demo.cpp`。该文件的输入不是
主线程轮询，而是 `prime_render_event_thread_start()`
（`port/prime_platform.cpp`）在 **OSCreateThread 线程里**等事件——
`prime_demo.cpp` 顶部注释记录过这条路径“此前由 OSCreateThread 线程嵌套创建 +
32B 缓冲是失效根因”。真机上线程没被调度/入口失败时，画面正常但永远收不到事件。

**Upsilon 路线（已验证可用）的做法**（`port/prime_upsilon_main.cpp`）：
主线程 `GetEvent` 阻塞循环、`ClearAllEvents` 前置、事件缓冲 64 B，
按键 `type==0x10` 取 `buf[8]>>16`，触摸 `type==0x15` 走触控项解码。

## 已实施（本地构建通过，待真机验证）

1. ✅ `prime_render_wait_key()` 改为**主线程直接轮询 `prime_get_event()`**
   （`port/prime_platform.cpp` 的 `prime_poll_key_event`），不再创建事件线程；
2. ✅ 触摸解码按 eventg1 的 `0x1/0x8`（按下/抬起）合成滑动与点按；
3. 保留 `prime_render_wait_key()` 的 API，内部改成“取一个已解码事件”，
   避免改动渲染/翻页逻辑；
4. 用 `examples/*.md` 做回归：ENTER/DOWN/触屏翻页、UP 回翻、ESC 返回。

## 以 Nspire 原版为准

渲染/排版有疑问时对照 `../nMarkdown-0.2.0/nMarkdown-0.2.0`（原始 TI-Nspire 源码），
本仓库只改平台层与移植胶水，不改渲染语义。
