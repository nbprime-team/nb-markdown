# PrimeMarkdown — HP Prime (Upsilon) Markdown 阅读器

把 TI Nspire 的 [nMarkdown](../../nMarkdown-0.2.0/nMarkdown-0.2.0) 的**字体渲染**
（FreeType 2.14.3 裁剪版 + HarfBuzz）与**数学公式渲染**（自带 LaTeX 引擎 +
Latin Modern Math）移植到 HP Prime G1，以 **Upsilon external app** 形态运行。

**功能**：文档选择页（扫描 `c:\apps\*.md`）+ 公式文字混排渲染（标题/文本/
块级 `$$...$$`/行内 `$...$` 公式）+ 翻页浏览。

**真机已验证**：渲染、按键/触屏翻页、文件读取、文档选择全通。

## 1. 部署（真机）

```
build/upsilon/primemarkdown.elf   → 计算器 c:\apps\primemarkdown.elf
examples/*.md                     → 计算器 c:\apps\（.md 文档）
```

1. Upsilon USB 模式（Vars 键）→ 拷文件到 `c:\apps\`
2. Upsilon External app（Home 快捷 9）启动 PrimeMarkdown
3. 选择页列出 `c:\apps\*.md` → ENTER 打开 → 翻页浏览

## 2. 按键

| 键 | 功能 |
|---|---|
| ENTER / DOWN / 触屏点按 | 下一页 |
| UP | 上一页 |
| ESC | 文档内 → 返回选择页；选择页 → 退出（已知可能重启） |
| ON | 退出（已知可能重启） |
| + | 选择页重新扫描文件列表 |

## 3. 支持的文档格式

```
# 标题（# 开头）

普通 ASCII 文本行

行内公式：$e^{i\pi} + 1 = 0$

块级公式（可跨行）：

$$
\int_0^1 x^2\,dx = \frac{1}{3}
$$
```

**限制**：
- 文本仅支持 ASCII（DejaVu 无 CJK 字形；**含非 ASCII 的文本/标题行自动跳过**）
- 每条公式 ≤240 字符，单文件 ≤60KB，最多 64 条公式
- 复杂 LaTeX 环境（matrix 等）可能渲染失败（该行跳过，不崩溃）

## 4. 构建

```sh
# 工具链: gcc-arm-none-eabi 系 (无 root 解包到 DOOM/armtc/root)
make -f Makefile.upsilon TOOLCHAIN_ROOT=<解包根目录>   # → build/upsilon/primemarkdown.elf
make -f Makefile.upsilon check TOOLCHAIN_ROOT=<解包根目录>
```

链接形态：g1app.ld（0x30900000）+ `ENTRY(AppMain)` + besta_fixes.cc +
libsyscalls + newlib + libstdc++（绕过 nano.specs，Ubuntu 无 newlib-nano 包）。

## 5. 目录结构

```
hp-prime-nmarkdown/
├── Makefile.upsilon      Upsilon 构建 (primemarkdown.elf + probe 系列)
├── port/
│   ├── prime_upsilon_main.cpp   主程序 (选择页 + md 解析 + 混排渲染)
│   ├── prime_platform.*         显示/输入 (SVC 直调, GetEvent 主循环)
│   ├── prime_render_api.*       C 渲染 API
│   ├── prime_svc.*              Besta OS SVC 包装
│   └── prime_log.*              日志 (Upsilon 下 Printf 上屏)
├── examples/              示例 .md 文档
├── docs/                  开发经验总结 / 测试清单 / 双路线分析
├── include/ src/ third_party/   上游 portable 核心 (原样)
└── tools/check_elf.py     ELF 校验 (Python 引导形态用)
```

## 6. 参考

- **开发经验总结**：`docs/DEVELOPMENT_LOG.md`（事件系统排查、崩溃二分诊断、退出机制研究）
- **测试清单**：`docs/TESTS.md`（含真机验收步骤）
- **双路线分析**：`docs/DUAL_ROUTE_ANALYSIS.md`（Python 引导 vs Upsilon）
- 系统 API：`HP/g1sdk`（Project Muteki 逆向的 Besta OS syscalls）
- 平台参考：`HP/eventg1`（官方事件示例）、`HP/doom/doom.c`
