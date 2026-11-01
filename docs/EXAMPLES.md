# PrimeTeX 公式示例集

本工程移植的 LaTeX 公式渲染器 (nMarkdown MathSystem + Latin Modern Math)
支持以下语法类别。演示程序 `primetex.elf` 内置两组示例页；下面的每条公式
都可以用 `prime_render_formula()` 渲染。

## 1. 基础语法

| 类别 | LaTeX 源码 | 渲染效果 |
|---|---|---|
| 分数 | `\frac{1}{2}` | ½ |
| 分数链 | `\frac{a+b}{c+d}` | (a+b)/(c+d) |
| 根号 | `\sqrt{x^2 + 1}` | √(x²+1) |
| 上标/下标 | `x^2`、`x_1` | x², x₁ |
| 上下标组合 | `x_1^2` | x₁² |
| 欧拉恒等式 | `e^{i\pi} + 1 = 0` | e^iπ + 1 = 0 |
| 定积分 | `\int_0^1 x^2\,dx = \frac{1}{3}` | ∫₀¹ x²dx = 1/3 |
| 求和 | `\sum_{k=1}^{n} k` | Σₖ₌₁ⁿ k |
| 极限 | `\lim_{n \to \infty} (1+\frac{1}{n})^n = e` | lim 定义式 |
| 矩阵 | `\begin{pmatrix} 1 & 2 \\ 3 & 4 \end{pmatrix}` | 2×2 矩阵 |
| 自适应括号 | `\left[ \frac{a}{b} \right]^2` | 大括号随内容伸缩 |
| 希腊字母 | `\alpha + \beta = \gamma` | α + β = γ |
| 高斯分布 | `f(x)=\frac{1}{\sigma\sqrt{2\pi}}\,e^{-\frac{(x-\mu)^2}{2\sigma^2}}` | 正态分布公式 |

## 2. 运算符与符号

- `\cdot` `\times` `\div` `\pm` `\mp` — 运算符号
- `\leq` `\geq` `\neq` `\approx` `\equiv` `\propto` — 关系符号
- `\to` `\rightarrow` `\leftarrow` `\mapsto` `\Rightarrow` — 箭头
- `\in` `\notin` `\subset` `\supset` `\subseteq` `\cup` `\cap` — 集合
- `\infty` `\partial` `\nabla` `\forall` `\exists` `\emptyset` — 常用符号
- `\ldots` `\cdots` `\,` `\;` `\quad` `\qquad` — 间距

## 3. 分数嵌套示例

```latex
\frac{ \frac{a}{b} + 1 }{ \frac{c}{d} - 2 }
```

## 4. 带文本的公式

```latex
f(x) = \begin{cases} x^2 & x > 0 \\ -x & x \leq 0 \end{cases}
```

> 注意: 分段函数 `\begin{cases}` 需要 MathSystem 支持 cases 环境,
> 若解析失败会返回错误码 -6 (见 `prime_render_formula`)。

## 5. 在 C 代码中调用

```c
/* LaTeX 字符串 → 公式渲染到 (6, 120) 处, 字号 15px, 最大宽 300px */
int rc = prime_render_formula("\\int_0^1 x^2\\,dx = \\frac{1}{3}",
                              6, 120, 15, kInk, kBackground, 0, 300);
/* 返回 0 = 成功; -6 = 公式解析/布局失败 */
```

## 6. 在 main.py 中扩展演示页

修改 `port/prime_demo.cpp` 的 `render_page_0()` / `render_page_1()`,
加入新公式后重新编译:

```sh
make -f Makefile.prime TOOLCHAIN_ROOT=/tmp/armtc/root
```

## 7. 测试建议

- 每页公式数 ≤ 6, 最后一条基线 y ≤ 220 (逻辑画布 320×240, 底部留提示行)
- 长公式设 `max_width=300` 防溢出; 溢出时公式会被裁切
- 渲染后用 `prime_render_count_ink(bg)` 统计非背景像素, 确认真的画了内容
- 真机日志会输出每页 `ink=` 像素数, 与离线估算对比可判断内容是否完整
