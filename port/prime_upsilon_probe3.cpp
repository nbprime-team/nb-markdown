/*
 * prime_upsilon_probe3.cpp — 异常机制 + heap + formula 探针
 * =========================================================
 * probe2 结论: render_init 成功, 崩溃在 R5 (formula) 之后。
 * 本探针专测三个嫌疑:
 *   [H] heap 可用量 (Upsilon 传给 external app 的 heap 大小)
 *   [E] C++ 异常机制 (throw/catch 是否工作 — libstdc++ Unwind)
 *   [R] formula 渲染分段 (不渲染 clear/text, 避免清屏覆盖 Printf 日志)
 * 不调用 clear/ present 中间的渲染, 仅保留公式渲染与最终 present。
 */
#include <cstdlib>
#include <muteki.h>

#include "nmarkdown/render/surface565.h"
#include "prime_render_api.h"

using nmarkdown::rgb565;

namespace {
const std::uint16_t kBg = rgb565(250, 250, 245);
const std::uint16_t kInk = rgb565(30, 30, 30);

/* heap 探测: 从大到小 malloc, 记录最大成功块 */
void heap_probe() {
    static const unsigned sizes[] = {8u << 20, 4u << 20, 2u << 20, 1u << 20,
                                     512u << 10, 256u << 10};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        void* p = malloc(sizes[i]);
        Printf("[H%d] malloc(%u)=0x%x\n", (int)i, sizes[i], (unsigned)p);
        if (p != 0) {
            /* 写一个字节确认页真实可用 */
            ((char*)p)[0] = 0xAA;
            ((char*)p)[sizes[i] - 1] = 0x55;
            free(p);
            break;  /* 最大可用 = sizes[i] */
        }
    }
}

/* 异常机制测试: C++ throw/catch */
void throw_test() {
    Printf("[E0] throw test begin\n");
    try {
        throw 123;
    } catch (int v) {
        Printf("[E1] caught int=%d\n", v);
    }
    try {
        throw 3.14;
    } catch (double v) {
        Printf("[E2] caught double=%f\n", v);
    }
    try {
        throw 42L;
    } catch (long v) {
        Printf("[E3] caught long=%ld\n", v);
    }
    Printf("[E4] throw tests done\n");
}

}  // namespace

extern "C" int main(void) {
    Printf("\f[P0] probe3 enter\n");

    heap_probe();
    throw_test();

    Printf("[R0] render_init...\n");
    int rc = prime_render_init();
    Printf("[R1] init rc=%d\n", rc);
    if (rc != 0) {
        Printf("[R1b] init failed, abort probe\n");
        return 0;
    }

    PrimeFbInfo fb;
    if (prime_render_fb_info(&fb) == 0) {
        Printf("[R2] fb %ux%u @0x%x\n", (unsigned)fb.x_res,
               (unsigned)fb.y_res, (unsigned)fb.bufferstart);
    }

    Printf("[R5a] before formula\n");
    rc = prime_render_formula("\\frac{1}{2}", 6, 70, 18, kInk, kBg, 0, 300);
    Printf("[R5b] formula rc=%d\n", rc);
    rc = prime_render_formula(
        "\\int_0^1 x^2\\,dx = \\frac{1}{3}", 6, 100, 18, kInk, kBg, 0, 300);
    Printf("[R5c] formula2 rc=%d\n", rc);

    Printf("[R6a] before present\n");
    rc = prime_render_present();
    Printf("[R6b] present rc=%d\n", rc);

    Printf("[R8] probe3 done\n");
    return 0;
}
