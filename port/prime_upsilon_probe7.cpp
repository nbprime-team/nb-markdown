/*
 * prime_upsilon_probe7.cpp — 退出前清理事件 (验证退出崩溃修复)
 * ===========================================================
 * probe6 结论: GetEvent 可用 (黄绿/蓝白随按键出现), 但 return 0 后重启
 * —— 崩溃在退出清理阶段, 嫌疑: GetEvent 拿到的事件未清干净。
 * 本探针: probe6 逻辑 + 退出前 ClearAllEvents() 清理。
 * 若正常退出 → 主版在退出前加 ClearAllEvents 即可修复。
 *   红 → GetEvent → 橙 → GetEvent → 绿 → 蓝 → [ClearAllEvents]
 *   → 白 (done) → return 0
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
const std::uint16_t kGreen = rgb565(20, 200, 20);
const std::uint16_t kBlue = rgb565(20, 20, 200);
const std::uint16_t kWhite = rgb565(250, 250, 250);

void fill(std::uint16_t c) {
    if (prime_render_clear(c) == 0) {
        prime_render_present();
    }
}
}  // namespace

extern "C" int main(void) {
    Printf("\f[P0] probe7 enter\n");

    int rc = prime_render_init();
    Printf("[R1] init rc=%d\n", rc);
    if (rc != 0) {
        return 0;
    }

    fill(kRed);
    OSSleep(400);

    /* GetEvent (非阻塞) */
    std::uint32_t buf[18];
    int g = prime_get_event(buf);
    Printf("[G1] get=%d\n", g);
    fill(kOrange);
    OSSleep(400);

    /* GetEvent 再次 */
    g = prime_get_event(buf);
    Printf("[G2] get=%d\n", g);
    fill(kGreen);
    OSSleep(400);

    /* GetEvent 循环 (按键捕获) */
    for (int i = 0; i < 10; ++i) {
        g = prime_get_event(buf);
        if (g != 0) {
            Printf("[G3] event %x\n", buf[7]);
            break;
        }
        OSSleep(50);
    }
    fill(kBlue);
    OSSleep(400);

    /* ★ 退出前清理事件 (修复验证) */
    prime_clear_all_events();

    fill(kWhite);
    OSSleep(400);
    Printf("[R8] probe7 done\n");
    return 0;
}
