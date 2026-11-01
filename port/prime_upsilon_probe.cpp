/*
 * prime_upsilon_probe.cpp — Upsilon 最小探针 (二分定位崩溃)
 * ========================================================
 * 用途: Upsilon 版 primetex.elf 直接重启, 先用纯 Printf 探针定位
 * 崩溃阶段。本文件不加载渲染核心, 只验证: 构建/部署/AppMain 入口/
 * besta_fixes heap 初始化/Printf/malloc/OSSleep/线程/LCD 是否正常。
 *
 * 若本探针也重启 → 构建链或 heap 初始化问题;
 * 若正常 → 崩溃在渲染初始化 (下一步加 prime_render_init)。
 */
#include <cstdlib>
#include <muteki.h>

namespace {

int probe_thread_fn(void*) {
    return 0;
}

}  // namespace

extern "C" int main(void) {
    /* \f 清屏 (muteki Printf) */
    Printf("\f[P0] main enter\n");

    /* 1. heap 已由 besta_fixes 初始化, 测试 malloc */
    void* p = malloc(1024);
    Printf("[P1] malloc(1024)=0x%x\n", (unsigned)p);
    if (p != 0) {
        free(p);
        Printf("[P1b] free ok\n");
    }

    /* 2. 系统服务: OSSleep */
    Printf("[P2] before sleep\n");
    OSSleep(500);
    Printf("[P3] after sleep\n");

    /* 3. 线程创建测试 (事件线程机制) */
    void* thr = OSCreateThread(&probe_thread_fn, nullptr, 0u, 0u);
    Printf("[P4] OSCreateThread=0x%x\n", (unsigned)thr);
    if (thr != 0) {
        OSSleep(300);
        OSTerminateThread(thr, 0);
        Printf("[P5] thread ok\n");
    }

    /* 4. LCD 获取 (SVC 0x1008D, muteki) */
    lcd_t* lcd = GetActiveLCD();
    if (lcd != 0) {
        Printf("[P6] lcd ok\n");
    } else {
        Printf("[P6] lcd FAIL\n");
    }

    Printf("[P7] probe done\n");
    return 0;
}
