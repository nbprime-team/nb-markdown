# PrimeTeX 测试清单

分两层: **离线测试** (本机可跑, 无需真机) 与 **真机测试** (需在 HP Prime 上执行)。

## 1. 离线测试 (本机)

| # | 测试 | 命令 | 预期 |
|---|---|---|---|
| T1 | ELF 形态校验 | `make -f Makefile.prime check` | 全 PASS (EM_ARM/ET_DYN/vaddr=0/无未定义符号/重定位全 R_ARM_RELATIVE) |
| T2 | 引导脚本语法 | `python3 -m py_compile PRIMETEX.hpappdir/main.py` | 无错误 |
| T3 | 引导脚本语法 (devkit) | `python3 -m py_compile hp-prime-devkit/generic_loader.py` | 无错误 |
| T4 | BootConfig ABI 一致性 | 见下方脚本 | 32B 布局 / 服务表 9 字段与 prime_sys.h 一致 |
| T5 | 元数据与模板一致 | `cmp` DOOM 模板 | 字节相同 |
| T6 | icon.png 有效 | PNG 签名/CRC 校验 | 通过 |
| T7 | portable 核心字节一致 | 与原项目对比 | 逐字节相同 |

```sh
# T4 快速检查: 服务表字段数与 prime_sys.h 一致
grep -c "uint32_t" port/prime_sys.h   # PrimeServices 内应 9 个字段
python3 - <<'EOF'
import struct
print(struct.calcsize("<IIIIIIIII"))  # 36 = 9 x uint32
EOF
```

## 2. 真机测试 (HP Prime G1)

部署: 把 `PRIMETEX.hpappdir/` 拷到 `C:\DATA\`, 在 Apps 列表打开 PRIMETEX。

### T10 启动与中文横幅
- 预期: 控制台显示 PrimeTeX 中文横幅 + "[1/2] 正在加载..." 提示
- 失败排查: 若卡在启动, 检查 `.hpappdir` 结构是否完整

### T11 帧缓冲诊断日志 (中文, 关键!)
- 预期: 日志回显 `帧缓冲: 320x240 bpp=32 行字节=1280 地址=0x31xxxxxx` 一行
- **这条日志决定显示修复方向** —— 把 `宽x高 / 行字节 / 地址` 数值
  反馈给开发者, 即可精确配置旋转/翻转/居中

### T12 显示布局 (自动模式 flags=0)
- 预期: 内容**全屏、居中**, 横屏直写 / 竖屏自动旋转 90°
- 异常时调整 `main.py` 的 `framebuffer.flags`:
  - 内容上下颠倒 → `flags=1` (flip_y)
  - 竖屏但内容横躺 → `flags=2` (force_rotate, 需配合方向反馈)
  - 内容不居中偏左上 → `flags=4` 关居中观察 (再手动调 offset)

### T13 渲染内容与像素统计
- 预期: 日志回显 `page 1 ink=<N>`、`page 2 ink=<M>`, N/M 均 > 1000
- 若 ink=0: 渲染系统异常 (字体加载失败), 检查 init 日志
- 若内容只显示一部分: 参照 T12 调整布局 flags

### T14 按键与触屏交互
- 预期: ENTER/DOWN/点按/左滑/下滑 → 第 2 页; UP/右滑/上滑 → 第 1 页;
  ESC 或 ON 退出; 每页 20 秒无操作自动退出 (启动时自动清除残留事件)
- 退出后日志回显 `PrimeTeX 演示结束`
- 若按键无反应: 检查事件 SVC (TestPendEvent 0x10046 带参 + GetEvent 0x1003F)

### T15 稳定性
- 连续运行 3 次, 无死机/花屏/内存泄漏崩溃 (ELF 内 malloc 走 SVC lmalloc)

## 3. 回归: 与 puredoom.elf 链路一致性

```sh
python3 hp-prime-nmarkdown/tools/check_elf.py \
  "DOOM.hpappdir/DOOM.hpappdir/puredoom.elf"   # 应 PASS (基准)
python3 hp-prime-nmarkdown/tools/check_elf.py \
  hp-prime-nmarkdown/build/prime/primetex.elf  # 应 PASS (本工程)
```

## 4. 排障速查

| 症状 | 可能原因 | 处理 |
|---|---|---|
| 内容左下角/截断 | 物理 fb 方向/尺寸与假设不符 | 反馈 T11 日志, 调 flags |
| 静态无响应 | 旧版无按键循环 | 升级到含 `prime_render_wait_key` 的 ELF |
| render init failed: -3 | SVC 获取 LCD 失败 | 检查固件版本; 服务表 framebuffer 兜底 |
| render init failed: -4 | 嵌入字体加载失败 | 检查 core_font_pack 是否链接 |
| ink=0 | 渲染未发生 | 检查 text.initialize 日志 |
| 花屏 | D-Cache 未 clean / 双缓冲 | 确认 present 的 clean_dcache_range |
