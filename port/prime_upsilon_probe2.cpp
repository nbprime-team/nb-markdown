/*
 * prime_upsilon_probe2.cpp — 渲染初始化探针 (二分定位第二步)
 * ==========================================================
 * probe (纯 C) 已验证: 构建链/部署/heap/Printf/线程/LCD 全正常。
 * 本探针在前者基础上加载渲染核心 (FreeType+字体+公式引擎),
 * 定位崩溃点: render_init / 公式渲染 / present。
 * 若本探针重启 → 崩溃在渲染初始化或异常机制 (libstdc++ Unwind)。
 */
#include <cstdlib>
#include <muteki.h>

#include "nmarkdown/render/surface565.h"
#include "prime_render_api.h"

using nmarkdown::rgb565;

namespace {
const std::uint16_t kBg = rgb565(250, 250, 245);
const std::uint16_t kInk = rgb565(30, 30, 30);
}  // namespace

extern "C" int main(void) {
    Printf("\f[P0] probe2 enter\n");

    /* 基础复验 (快速版) */
    void* p = malloc(1024);
    Printf("[P1] malloc=0x%x\n", (unsigned)p);
    if (p != 0) free(p);

    /* ---- 渲染核心 ---- */
    Printf("[R0] render_init...\n");
    int rc = prime_render_init();
    Printf("[R1] init rc=%d\n", rc);
    if (rc != 0) {
        Printf("[R1b] init failed, abort probe\n");
        return 0;
    }

    PrimeFbInfo fb;
    if (prime_render_fb_info(&fb) == 0) {
        Printf("[R2] fb %ux%u bpp=%u @0x%x\n", (unsigned)fb.x_res,
               (unsigned)fb.y_res, (unsigned)fb.bpp,
               (unsigned)fb.bufferstart);
    } else {
        Printf("[R2] fb info FAIL\n");
    }

    /* 文本渲染 */
    rc = prime_render_clear(kBg);
    Printf("[R3] clear rc=%d\n", rc);
    rc = prime_render_text("PrimeTeX probe2", 6, 32, 20, kInk, kBg, 0);
    Printf("[R4] text rc=%d\n", rc);
    rc = prime_render_formula("\\frac{1}{2}", 6, 70, 18, kInk, kBg, 0, 300);
    Printf("[R5] formula rc=%d\n", rc);
    rc = prime_render_present();
    Printf("[R6] present rc=%d\n", rc);
    rc = prime_render_count_ink(kBg);
    Printf("[R7] ink=%d\n", rc);

    Printf("[R8] probe2 done\n");
    return 0;
}
