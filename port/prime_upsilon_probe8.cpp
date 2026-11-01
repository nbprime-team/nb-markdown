/*
 * prime_upsilon_probe8.cpp — 非阻塞事件 SVC 替代测试
 * ==================================================
 * probe6/7 结论: GetEvent 可用但调用后退出必重启 (ClearAllEvents 无效)。
 * 本探针测非阻塞替代: TestPendEvent(0x10046) + GetPendEvent(0x10040),
 * 若可用且退出正常 → 主版换用 GetPendEvent 轮询。
 *   红 → TestPendEvent → 橙 → GetPendEvent → 黄
 *   → 轮询 GetPendEvent×30 (30ms 间隔, 按键捕获) → 绿
 *   → 蓝 → 白 (done) → return 0
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

void fill(std::uint16_t c) {
    if (prime_render_clear(c) == 0) {
        prime_render_present();
    }
}
}  // namespace

extern "C" int main(void) {
    Printf("\f[P0] probe8 enter\n");

    int rc = prime_render_init();
    Printf("[R1] init rc=%d\n", rc);
    if (rc != 0) {
        return 0;
    }

    fill(kRed);
    OSSleep(400);

    /* 1. TestPendEvent (非阻塞探测, 带 event*) */
    std::uint32_t buf[18];
    int t = prime_test_pend_event(buf);
    Printf("[T1] test=%d\n", t);
    fill(kOrange);
    OSSleep(400);

    /* 2. GetPendEvent (非阻塞取) */
    int g = prime_get_pend_event(buf);
    Printf("[G1] pend=%d\n", g);
    fill(kYellow);
    OSSleep(400);

    /* 3. 轮询 GetPendEvent (按键捕获, 30ms 间隔) */
    for (int i = 0; i < 30; ++i) {
        g = prime_get_pend_event(buf);
        if (g != 0) {
            Printf("[G2] event %x\n", buf[7]);
            break;
        }
        OSSleep(30);
    }
    fill(kGreen);
    OSSleep(400);

    fill(kBlue);
    OSSleep(400);
    fill(kWhite);
    OSSleep(400);
    Printf("[R8] probe8 done\n");
    return 0;
}
