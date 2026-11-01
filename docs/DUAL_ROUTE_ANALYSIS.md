# HP Prime G1 双系统/双路线深度对比分析

> 结论先行：**两条路线共享同一个 Besta OS 内核（SVC 系统调用表），差异全在
> "谁把 ELF 弄进内存、谁做运行时初始化"**。本工程已证明：为一条路线写的
> portable 渲染核心，另一条路线可几乎零成本复用；平台层差异可收敛到一个
> 小适配层（本项目 `port/`）。

---

## 1. 两种"系统"的本质

HP Prime G1 有**一个真实内核**（Besta OS，S3C2416 ARM926EJ-S 上运行）和
**两条运行原生代码的路径**：

```
                          ┌─ 路线 A: 原始固件 + PrimeDebug 后门
Besta OS (SVC 内核) ──────┤   MicroPython 脚本 → debug 接口 → shellcode → ELF
 0x30000000-0x30100000    │   不改固件, 纯软件加载
                          └─ 路线 B: Upsilon 替代固件 (SDK)
                               固件启动器 → 直接加载 c:\apps\app.elf
                               固定地址 0x30900000, AppMain 入口
```

关键事实（来自 `HP/readme.txt` 与 `HP/g1sdk` 实测）：

| 事实 | 证据 |
|---|---|
| 同一个 SVC 内核 | `HP/g1sdk/src/syscalls.s`：`svc 0x10000` 起，与 devkit 的 `get_addr(0x10037)` 同源 |
| framebuffer 可运行时获取 | `SVC 0x1008D GetActiveLCD` → `window1_bufferstart` (XRGB32 320×240) |
| 内存布局固定 | RAM `0x30000000-0x32000000`；OS 代码 `0x30000000-0x30100000`；OS 缓冲（framebuffer/USB）`0x31D00000-0x32000000` |
| malloc 与 lmalloc 同源 | devkit `get_addr(0x10037)` == muteki `lmalloc` == `SVC 0x10037` |

**结论**：这是"一个内核、两种加载器"，不是两个系统。因此本工程的 `port/prime_svc.h`
（SVC 包装）对两条路线**完全通用**。

---

## 2. 两条路线逐项对比

| 维度 | 路线 A：PrimeDebug 后门 | 路线 B：Upsilon/SDK |
|---|---|---|
| **目标用户** | 不改固件的普通玩家 | 愿意刷替代固件的开发者/玩家 |
| **启动方式** | MicroPython 脚本 → debug 文件 → shellcode → 任意基址 ELF | 固件启动器直接加载 `c:\apps\app.elf` |
| **入口** | `app_entry(BootConfig*)`（R0=cfg, R1=0） | `AppMain(canary, heapaddr, heapsize)` |
| **ELF 形态** | ET_DYN + PIC + vaddr=0 + `.rel.dyn` 全 R_ARM_RELATIVE | ET_EXEC（或 DYN）+ g1app.ld 固定 `0x30900000` |
| **链接** | `-shared -Wl,-e,app_entry --no-undefined -Bsymbolic` | `-T g1app.ld --entry=AppMain -lsyscalls`（newlib 模式） |
| **运行时** | 自建：`operator new`→SVC lmalloc，最小 libc（`prime_runtime.cpp`） | newlib：`_sbrk` 堆 + `besta_fixes.cc`（stubs + C++ init） |
| **堆来源** | SVC lmalloc（OS 堆） | 链接脚本 `_heap_start`..`0x31D00000` 或 OS 传入堆 |
| **系统 API** | `prime_svc.h`（SVC 直调，13 个已封装） | muteki 头 + `libsyscalls.a`（~300 个 SVC 包装） |
| **显示** | SVC 获取 LCD → 内部 RGB565 缓冲 → XRGB32 转换直写 | 同上（Nofrendo/doom 直接写 `fb_screen`） |
| **键盘** | `GetPendEvent`（0x10040）非阻塞轮询 | `GetEvent`（0x1003f）阻塞 + 独立线程（doom 模式） |
| **调试回显** | LogReader 环形缓冲 → Python 控制台（现成） | 无 Python 侧；Printf 直接上屏（需先 `\f`） |
| **部署** | 拷 `.hpappdir` + 运行脚本，零风险 | 需改 `PRIME_APP.DAT` 刷 multiloader/Upsilon |
| **回滚** | 无固件改动，直接卸载 | 恢复原始 `PRIME_APP.DAT` |
| **开发成熟度** | 本工程（渲染库）+ DOOM 移植 | SDK 完整：hellog1/eventg1/chess/dames/reversi/doom/khicas |
| **生态** | 小众（需会玩 MicroPython 后门） | 已有完整原生游戏生态（Nofrendo/PeanutGB 模拟器） |

---

## 3. 使用体验对比与建设性意见

### 3.1 普通用户视角

- **路线 A**：像装一个"Python 应用"，连接计算器 → 拷文件 → 运行脚本。
  门槛低、零风险、可随时卸载。缺点是每次都要跑 Python 引导，且性能受
  MicroPython 轮询（日志回显）影响；崩溃时容易留下内存碎片。
- **路线 B**：像"刷机"。一次改装后获得**独立原生应用生态**，启动即玩、
  不依赖 Python。缺点是刷机有（极小）风险，且 Upsilon 是替代系统（不是
  HP 官方系统，部分 HP 功能在 Upsilon 下不可用，如触摸屏——KhiCAS 除外）。

**建设性意见**：
1. **普通用户首选路线 A**：PrimeTeX 渲染库做成 Python 引导加载即可，
   与现有 DOOM 玩法一致，不需要用户刷机。
2. **把路线 A 的引导体验做成"一键"**：理想形态是一个 `.hpappdir` 里
   的 Python 脚本 + ELF + 说明，双击即玩（现有 devkit 已接近）。
3. **路线 B 适合"发布游戏/工具"**：做成一两个 `app.elf` 放进 `c:\apps`，
   启动体验与原生 app 一致，性能最好（无 Python 开销）。

### 3.2 开发者视角

- **路线 A 开发**：
  - 优点：迭代快（改 Python 配置/重编译 ELF 重跑即可），日志回显方便，
    不需要刷机，出错不毁机。
  - 缺点：ELF 必须满足严格的"无符号重定位"形态（本工程已解决并自动化
    `check_elf.py`）；freestanding 运行时（libc 全靠自己补）；MicroPython
    轮询有额外开销。
- **路线 B 开发**：
  - 优点：完整 newlib + 官方 SDK 头，malloc/printf/file 全都有，工具链
    配置好一次就顺；`besta_fixes.cc` 处理了所有 newlib 对接。
  - 缺点：每次改动要重刷/重拷 `app.elf` 到 `c:\apps`（USB 挂载，尚可）；
    无 Python 侧日志，调试靠 Printf 上屏；Upsilon 与 HP 原装固件行为差异
    （SetFontType 崩溃、Printf 需 `\f`）需注意。

**建设性意见**：
1. **渲染核心必须与平台无关**——本工程已验证（上游 portable 核心零改动）。
2. **两条路线共用 `port/prime_svc.h`**：SVC 编号与布局两路通用，一次封装
   两处受益。
3. **优先走路线 A 开发 + 路线 B 发布**：开发期用 Python 引导（日志好）、
   成熟后出 Upsilon 版（性能好、体验原生）。
4. **工具链统一**：两条路线都用 `arm-none-eabi-g++ 14.2` + `-mcpu=arm926ej-s
   -marm -fPIC`，唯一区别在链接脚本与运行时。

---

## 4. 程序通用部分 / 平台相关部分划分

```
                    ┌─────────────────────────────────────┐
                    │        通用（可复用）                │
                    │  src/text/  src/math/  src/render/  │  ← 上游 portable 核心, 零改动
                    │  FreeType + HarfBuzz (nmarkdown 裁剪)│
                    │  src/generated/ (字体 blob/符号表)   │
                    │  port/prime_svc.h  (SVC 编号/布局)   │  ← 双路线通用!
                    │  port/prime_runtime.cpp (freestanding)│ ← 路线 A 用; B 用 besta_fixes 替代
                    └──────────────┬──────────────────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              ▼                    ▼                    ▼
     路线 A 专属                  平台抽象                路线 B 专属
  port/prime_platform.cpp     platform.h (Display/    g1sdk/besta_fixes.cc
  (SVC 直调实现)              Input/Clock/FileSystem) (newlib stubs+C++init)
  generic_loader.py           接口不变, 两路实现        g1app.ld (0x30900000)
  ET_DYN 构建 (Makefile.prime)                         -lsyscalls + muteki 头
  app_entry(BootConfig*)                                AppMain(canary,heap,size)
```

### 4.1 通用部分（复制即用）

| 模块 | 说明 |
|---|---|
| `src/text/` 7 个 .cpp | FreeType 光栅化 + HarfBuzz 整形，无平台依赖 |
| `src/math/` 6 个 .cpp | LaTeX 解析/布局，纯 STL |
| `src/render/` | Surface565（320×240 RGB565 与 Nspire CX/Prime 都一致）|
| `third_party/freetype` + `harfbuzz` | nmarkdown 定制裁剪配置 |
| `port/prime_svc.h` | Besta OS SVC 编号与结构体定义（LCD/事件/时间/内存） |
| 渲染 C API 设计 | `prime_render_init/text/formula/present` 抽象 |

### 4.2 平台相关部分（必须按路线实现）

| 功能 | 路线 A 实现 | 路线 B 实现 |
|---|---|---|
| 入口 | `app_entry(BootConfig*)`（devkit 约定） | `AppMain(canary, heapaddr, heapsize)`（g1app.ld） |
| 内存 | `prime_runtime.cpp`：new/malloc → SVC lmalloc | newlib `_sbrk` + `besta_fixes.cc` 初始化 |
| 显示 | `DisplayPrime`：SVC GetActiveLCD → 565→XRGB32 | 同上逻辑（doom.c 直接写 `fb_screen`） |
| 键盘 | `GetPendEvent` 轮询 | `GetEvent` 阻塞 + 线程（doom.c 模式） |
| 文件 | stub（未来 `_afopen` SVC 族） | newlib stdio + `besta_fixes.cc` 文件 stubs |
| 构建 | `Makefile.prime`（ET_DYN + `--no-undefined` + `-Bsymbolic`） | SDK Makefile（g1app.ld + `-lsyscalls` + nano.specs） |
| 部署 | `generic_loader.py` 引导 | `c:\apps\app.elf` |

### 4.3 若要把本渲染库编译成路线 B（Upsilon app）

只需新增一个入口文件（约 40 行）+ 换构建脚本，portable 核心与 `prime_svc.h`
原样复用：

```c
// port/prime_sdk_main.cpp (示意)
extern "C" void AppMain(int canary, void* heapaddr, unsigned heapsize) {
    prime_render_init();          // SVC 自动获取 LCD
    prime_render_clear(rgb565(250,250,245));
    prime_render_text("PrimeTeX on Upsilon", 6, 26, 18, ...);
    prime_render_formula("\\int_0^1 x^2\\,dx = \\frac{1}{3}", 6, 60, ...);
    prime_render_present();
    for (;;) ;                     // 或事件循环
}
```

编译：用 `g1app.ld` + `besta_fixes.cc` + `-lsyscalls` 替代 `prime_runtime.cpp` +
`ET_DYN` 链接；`prime_svc.h` 的 SVC 直接可用（muteki 头同编号）。

---

## 5. 本工程的编译实测记录（路线 A）

| 步骤 | 结果 |
|---|---|
| 无 root 安装工具链 | `apt-get download` 5 包 + `dpkg-deb -x`，605MB，成功 |
| 头文件路径 | Ubuntu multiarch 布局：C++ 头 `usr/include/newlib/c++/14.2.1`，newlib 头 `usr/include/newlib`，配置 `TOOLCHAIN_ROOT` 自动处理 |
| `-ffreestanding` 陷阱 | 会禁用 libstdc++ hosted 头（vector 等），须去掉（上游本就不用） |
| C++ naked 函数 | 属性必须放返回类型前（C++ 不允许 declarator 后） |
| 链接补符号 | 补 `strcpy/strstr/qsort/memchr/snprintf/strtol/floor/fabs/__cxa_atexit/__dso_handle/setjmp/longjmp/__gnu_Unwind_Find_exidx` 等 |
| setjmp 关键坑 | newlib 版保存 VFP（`vstmia {d8-d15}`），G1 无 VFP 会崩；自写整数寄存器版 |
| `-Bsymbolic --exclude-libs,ALL` | 消除 JUMP_SLOT/GLOB_DAT 符号重定位（1097→0）——shellcode 只处理 R_ARM_RELATIVE |
| `__gnu_Unwind_Find_exidx` | ARM 异常表查找，自给实现返回 `.ARM.exidx` 基址 |
| **最终结果** | **ELF 2.98MB，`make check` 全 PASS：ET_DYN/EM_ARM/vaddr=0/2306 条重定位全 R_ARM_RELATIVE/无未定义符号** |

---

## 6. 选型建议（一句话）

> **开发用路线 A（迭代快、日志好、不毁机），发布用路线 B（原生体验、
> 性能好、生态成熟）；渲染核心与 SVC 封装两路共用，平台差异收敛到
> `port/` 一个目录。** 本工程就是这一策略的完整示例。
