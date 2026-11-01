/*
 * prime_upsilon_stubs.cpp — Upsilon external app 运行时补丁
 * =========================================================
 * Upsilon 形态 (g1app.ld + besta_fixes.cc) 下 newlib 提供完整 libc,
 * 不再需要 prime_runtime.cpp 的自写内存/libc —— 本文件只补 Upsilon
 * 环境缺、而 libstdc++/平台层需要的少量符号:
 *   - prime_set_services / prime_get_services: 服务表是 Python 引导形态
 *     的概念, Upsilon 下恒为 nullptr (平台层全部走 SVC 直调)
 *   - __gnu_Unwind_Find_exidx: libstdc++ 异常表查找 (newlib 不提供)
 */
#include <cstdint>

#include "prime_sys.h"

extern "C" {

/* Upsilon: 无服务表, SVC 直调 (DisplayPrime/InputPrime 的 services 后备为空) */
static const PrimeServices* g_upsilon_services = nullptr;

void prime_set_services(const PrimeServices* svc) {
    (void)svc;
}

const PrimeServices* prime_get_services(void) {
    return g_upsilon_services;
}

/* ARM 异常表查找 (libstdc++ _Unwind 需要)。g1app.ld 定义了
 * __exidx_start/__exidx_end 符号。 */
int __gnu_Unwind_Find_exidx(void* pc, int* pcount) {
    extern char __exidx_start[] __attribute__((weak));
    extern char __exidx_end[] __attribute__((weak));
    (void)pc;
    if (pcount != 0) {
        *pcount = static_cast<int>(
                      ((unsigned long)(void*)__exidx_end -
                       (unsigned long)(void*)__exidx_start) /
                      8);
    }
    return (int)(unsigned long)(void*)__exidx_start;
}

} /* extern "C" */
