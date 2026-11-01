/*
 * prime_upsilon_probe13.cpp — 主版前置 SVC + 全部公式逐项色块
 * ===========================================================
 * 主版 v3 重启, 但 probe10 (无前置SVC/少公式) 正常。两处未验证差异:
 *   A. SetSystemVariable(85,2,1) + ClearScreen(0) 前置 (主版有)
 *   B. 页1 公式 \sqrt{x^2+1} / e^{i\pi}+1=0 (probe10 未测过)
 * 色块序列逐项定位:
 *   SetSystemVariable → ClearScreen → init → 红
 *   \frac → 橙 → \sqrt → 黄 → e^{i\pi} → 绿 → \int → 蓝
 *   present → 白 → GetEvent 循环 (按键→紫)
 */
#include <cstdlib>
#include <muteki.h>

#include "nmarkdown/render/surface565.h"
#include "prime_render_api.h"
#include "prime_svc.h"

using nmarkdown::rgb565;

namespace {
const std::uint16_t kRed = rgb565(200, 20, 20);
const std::uint16_t kOrange = rgb565(255, 140, 0);
const std::uint16_t kYellow = rgb565(255, 230, 0);
const std::uint16_t kGreen = rgb565(20, 200, 20);
const std::uint16_t kBlue = rgb565(20, 20, 200);
const std::uint16_t kWhite = rgb565(250, 250, 250);
const std::uint16_t kPurple = rgb565(180, 20, 200);
const std::uint16_t kBg = rgb565(250, 250, 245);
const std::uint16_t kInk = rgb565(30, 30, 30);

int formula(const char* latex, int y, std::uint16_t mark) {
    int rc = prime_render_formula(latex, 6, y, 18, kInk, kBg, 0, 300);
    Printf("[F] rc=%d %s\n", rc, latex);
    if (prime_render_clear(mark) == 0) {
        prime_render_present();
    }
    OSSleep(400);
    return rc;
}
}  // namespace

extern "C" int main(void) {
    Printf("\f[P0] probe13 enter\n");

    /* A. 主版前置 (probe10 没有的) */
    prime_set_system_variable(85u, 2u, 1u);
    prime_clear_screen(0);

    int rc = prime_render_init();
    Printf("[R1] init rc=%d\n", rc);
    if (rc != 0) {
        return 0;
    }
    if (prime_render_clear(kRed) == 0) prime_render_present();
    OSSleep(400);

    /* B. 页1 全部公式逐个 (每个后色块) */
    prime_render_clear(kBg);
    formula("\\frac{1}{2}", 40, kOrange);      /* 橙: \frac OK */
    prime_render_clear(kBg);
    formula("\\sqrt{x^2 + 1}", 40, kYellow);   /* 黄: \sqrt OK */
    prime_render_clear(kBg);
    formula("e^{i\\pi} + 1 = 0", 40, kGreen);  /* 绿: e^{i\pi} OK */
    prime_render_clear(kBg);
    formula("\\int_0^1 x^2\\,dx = \\frac{1}{3}", 40, kBlue); /* 蓝 */

    /* present */
    rc = prime_render_clear(kBg);
    rc |= prime_render_text("PrimeTeX probe13", 6, 32, 20, kInk, kBg, 0);
    prime_render_present();
    if (prime_render_clear(kWhite) == 0) prime_render_present();
    OSSleep(400);

    /* GetEvent 主循环 (probe10 验证正常) */
    std::uint32_t buf[18];
    for (;;) {
        if (!prime_get_event(buf)) continue;
        const std::uint32_t type = buf[7];
        if (type == 0x10U) {
            Printf("[K] key=%x\n", buf[8] >> 16U);
            if (prime_render_clear(kPurple) == 0) prime_render_present();
            OSSleep(400);
        } else if (type == 0x1U || type == 0x8U) {
            Printf("[T] touch %x,%x\n", buf[8] >> 16U, buf[9]);
            if (prime_render_clear(kPurple) == 0) prime_render_present();
            OSSleep(400);
        }
    }
}
