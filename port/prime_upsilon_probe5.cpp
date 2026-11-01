/*
 * prime_upsilon_probe5.cpp — 主版特有调用逐项色块探针
 * ===================================================
 * probe4 结论: 渲染核心 (init/formula/present) 全正常。
 * 主版崩溃点必在主版特有项: SetSystemVariable / ClearScreen / 事件线程。
 * 本探针逐项调用并全屏色块标记 (报最后颜色即可定位):
 *   红 → SetSystemVariable(85,2,1) → 橙 → ClearScreen(0) → 黄
 *   → 事件线程启动 → 绿 → wait_key(5s) → 青 → wait_key(3s) → 蓝
 *   → stop → 紫 → 白 (done)
 * 按键测试: 绿后用户按任意键, 若青/蓝立即出现 (<1s) 说明事件线程有效。
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
const std::uint16_t kCyan = rgb565(0, 200, 200);
const std::uint16_t kBlue = rgb565(20, 20, 200);
const std::uint16_t kPurple = rgb565(180, 20, 200);
const std::uint16_t kWhite = rgb565(250, 250, 250);

void fill(std::uint16_t c) {
    if (prime_render_clear(c) == 0) {
        prime_render_present();
    }
}
}  // namespace

extern "C" int main(void) {
    Printf("\f[P0] probe5 enter\n");

    int rc = prime_render_init();
    Printf("[R1] init rc=%d\n", rc);
    if (rc != 0) {
        return 0;
    }

    /* 基准: 红 */
    fill(kRed);
    OSSleep(500);

    /* 1. SetSystemVariable (doom/eventg1 同款前置) */
    prime_set_system_variable(85u, 2u, 1u);
    fill(kOrange);
    OSSleep(500);

    /* 2. ClearScreen (doom 同款前置) */
    prime_clear_screen(0);
    fill(kYellow);
    OSSleep(500);

    /* 3. 事件线程启动 (wait_key 内部) */
    rc = prime_render_wait_key(5000);  /* 5s 内按任意键 */
    fill(kCyan);
    OSSleep(800);
    Printf("[W1] wait1 key=%d\n", rc);

    /* 4. 再次等待按键 (验证事件持续有效) */
    rc = prime_render_wait_key(3000);
    fill(kBlue);
    OSSleep(800);
    Printf("[W2] wait2 key=%d\n", rc);

    /* 5. 停止事件线程 */
    prime_render_event_thread_stop();
    fill(kPurple);
    OSSleep(500);

    /* done */
    fill(kWhite);
    OSSleep(500);
    Printf("[R8] probe5 done\n");
    return 0;
}
