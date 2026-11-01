/*
 * prime_upsilon_probe4.cpp — 全屏色块探针 (定位崩溃点, 不依赖 Printf)
 * =================================================================
 * probe3 结论: heap 8MB+ OK, C++ 异常 OK。崩溃仍可能在 formula/present。
 * 本探针用全屏纯色块分步定位 (每步 present 后停留 500ms):
 *   红 → formula1 → 绿 → formula2 → 蓝 → present → 白 (done)
 * 用户只需报告最后看到的颜色, 即可精确定位崩溃的调用。
 * 颜色: 红=0xF800 绿=0x07E0 蓝=0x001F 白=0xFFFF
 */
#include <cstdlib>
#include <muteki.h>

#include "nmarkdown/render/surface565.h"
#include "prime_render_api.h"

using nmarkdown::rgb565;

namespace {
const std::uint16_t kRed = rgb565(200, 20, 20);
const std::uint16_t kGreen = rgb565(20, 200, 20);
const std::uint16_t kBlue = rgb565(20, 20, 200);
const std::uint16_t kWhite = rgb565(250, 250, 250);
const std::uint16_t kInk = rgb565(30, 30, 30);

/* 全屏填色 + present (每步状态) */
void fill(std::uint16_t c) {
    if (prime_render_clear(c) == 0) {
        prime_render_present();
    }
}
}  // namespace

extern "C" int main(void) {
    Printf("\f[P0] probe4 enter\n");

    /* heap 探测 (快速) */
    void* p = malloc(8u << 20);
    Printf("[H0] malloc(8M)=0x%x\n", (unsigned)p);
    if (p != 0) free(p);

    Printf("[R0] init...\n");
    int rc = prime_render_init();
    Printf("[R1] init rc=%d\n", rc);
    if (rc != 0) {
        return 0;
    }

    /* 步骤 1: 红屏 — formula 之前 */
    fill(kRed);
    OSSleep(500);

    /* formula1 (分数) */
    rc = prime_render_formula("\\frac{1}{2}", 6, 70, 18, kInk, kRed, 0, 300);
    Printf("[R5b] formula rc=%d\n", rc);

    /* 步骤 2: 绿屏 — formula1 完成 */
    fill(kGreen);
    OSSleep(500);

    /* formula2 (积分) */
    rc = prime_render_formula("\\int_0^1 x^2\\,dx = \\frac{1}{3}", 6, 100,
                              18, kInk, kGreen, 0, 300);
    Printf("[R5c] formula2 rc=%d\n", rc);

    /* 步骤 3: 蓝屏 — formula2 完成 */
    fill(kBlue);
    OSSleep(500);

    /* 直接 present (验证 present 本身) */
    rc = prime_render_present();
    Printf("[R6b] present rc=%d\n", rc);

    /* 步骤 4: 白屏 — done */
    fill(kWhite);
    OSSleep(500);

    Printf("[R8] probe4 done\n");
    return 0;
}
