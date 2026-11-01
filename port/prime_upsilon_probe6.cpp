/*
 * prime_upsilon_probe6.cpp — 事件 SVC 主线程直测
 * =============================================
 * probe5 结论: SetSystemVariable/ClearScreen 正常, 崩溃在事件线程内
 * (ClearAllEvents/GetEvent 或线程调度)。本探针在主线程直接调用这两个
 * SVC, 判断是 SVC 本身问题还是线程上下文问题:
 *   红 → ClearAllEvents() → 橙 → GetEvent(buf) → 黄 → GetEvent 再次 → 绿
 *   → OSSleep(500) → 蓝 → done 白
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
    Printf("\f[P0] probe6 enter\n");

    int rc = prime_render_init();
    Printf("[R1] init rc=%d\n", rc);
    if (rc != 0) {
        return 0;
    }

    fill(kRed);
    OSSleep(400);

    /* 1. ClearAllEvents (SVC 0x1004A) 主线程直调 */
    prime_clear_all_events();
    fill(kOrange);
    OSSleep(400);

    /* 2. GetEvent (SVC 0x1003F) 主线程直调 */
    std::uint32_t buf[18];
    int g = prime_get_event(buf);
    Printf("[G1] get=%d\n", g);
    fill(kYellow);
    OSSleep(400);

    /* 3. GetEvent 再次 */
    g = prime_get_event(buf);
    Printf("[G2] get=%d\n", g);
    fill(kGreen);
    OSSleep(400);

    /* 4. 主线程忙等测试 (模拟事件线程的阻塞循环, 但放主线程)
     *    GetEvent 阻塞时此处会卡住 (蓝不出现) */
    for (int i = 0; i < 10; ++i) {
        g = prime_get_event(buf);
        if (g != 0) {
            Printf("[G3] event got %x\n", buf[7]);
            break;
        }
        OSSleep(50);
    }
    fill(kBlue);
    OSSleep(400);

    fill(kWhite);
    OSSleep(400);
    Printf("[R8] probe6 done\n");
    return 0;
}
