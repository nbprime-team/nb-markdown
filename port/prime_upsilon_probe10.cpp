/*
 * prime_upsilon_probe10.cpp — 无线程 + 主线程 GetEvent + OSExitThread 退出
 * =======================================================================
 * 已确认: 渲染+事件线程=启动崩; 渲染+主线程 GetEvent=运行正常(仅 return 崩)。
 * 本探针: 渲染 + 主线程 GetEvent (probe6/7 证明运行 OK) + 退出用
 * OSExitThread(0x10007) 替代 return (绕过 Upsilon app 返回清理路径)。
 * 若退出不重启 → 主版最终形态确定。
 *   红(init) → 橙(渲染第1页) → for(;;) GetEvent 阻塞
 *   按键: Esc → 白 → OSExitThread(0)
 *        其他键 → 渲染第2页 → 绿 (再按 Esc 退出)
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
const std::uint16_t kWhite = rgb565(250, 250, 250);
const std::uint16_t kBg = rgb565(250, 250, 245);
const std::uint16_t kInk = rgb565(30, 30, 30);

void fill(std::uint16_t c) {
    if (prime_render_clear(c) == 0) {
        prime_render_present();
    }
}
}  // namespace

extern "C" int main(void) {
    Printf("\f[P0] probe10 enter\n");

    int rc = prime_render_init();
    Printf("[R1] init rc=%d\n", rc);
    if (rc != 0) {
        return 0;
    }
    fill(kRed);
    OSSleep(400);

    /* 渲染第 1 页 (真实内容) */
    rc = prime_render_clear(kBg);
    rc |= prime_render_text("PrimeTeX probe10", 6, 32, 20, kInk, kBg, 0);
    rc |= prime_render_formula("\\frac{1}{2}", 6, 70, 18, kInk, kBg, 0, 300);
    rc |= prime_render_formula("\\int_0^1 x^2\\,dx = \\frac{1}{3}", 6, 100,
                               18, kInk, kBg, 0, 300);
    prime_render_present();
    fill(kOrange);  /* 第 1 页渲染完成 (被 fill 覆盖, 仅标记) */
    OSSleep(400);

    /* 主循环: 主线程 GetEvent 阻塞等事件 */
    std::uint32_t buf[18];
    int page = 1;
    for (;;) {
        if (!prime_get_event(buf)) {
            continue;  /* GetEvent 阻塞, 无事件不返回 */
        }
        const std::uint32_t type = buf[7];
        if (type == 0x10U) {  /* keydown */
            const unsigned key = buf[8] >> 16U;
            Printf("[K] key=%x\n", key);
            if (key == 0x01 /* Esc */ || key == 0x83 /* ON */) {
                fill(kWhite);  /* 退出前标记 */
                OSSleep(300);
                prime_os_exit_thread(0);  /* 替代 return */
                return 0;      /* 兜底 (不应到达) */
            }
            if (page < 2) {
                ++page;
                rc = prime_render_clear(kBg);
                rc |= prime_render_text("PrimeTeX page 2", 6, 32, 20, kInk,
                                        kBg, 0);
                rc |= prime_render_formula(
                    "\\sum_{k=1}^{n} k = \\frac{n(n+1)}{2}", 6, 70, 18,
                    kInk, kBg, 0, 300);
                prime_render_present();
            }
            fill(kGreen);
            OSSleep(300);
        } else if (type == 0x1U || type == 0x8U) {  /* touch */
            Printf("[T] touch %x,%x\n", buf[8] >> 16U, buf[9]);
            if (page < 2) {
                ++page;
                rc = prime_render_clear(kBg);
                rc |= prime_render_text("PrimeTeX page 2", 6, 32, 20, kInk,
                                        kBg, 0);
                rc |= prime_render_formula(
                    "\\sum_{k=1}^{n} k = \\frac{n(n+1)}{2}", 6, 70, 18,
                    kInk, kBg, 0, 300);
                prime_render_present();
            }
            fill(kGreen);
            OSSleep(300);
        }
    }
}
