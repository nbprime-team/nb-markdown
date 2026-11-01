/*
 * prime_demo.cpp — ELF 入口与 PrimeTeX 演示程序
 * ===============================================
 * app_entry 是引导器 (generic_loader.py) 调用的入口:
 *   1. 校验 BootConfig 魔数
 *   2. 安装 PrimeServices (固件 malloc/free/framebuffer 等)
 *   3. 初始化字体+公式渲染系统
 *   4. 渲染演示页 (双页公式集) 到 framebuffer 并送显
 *   5. 按键循环: ENTER/DOWN/点按/左滑/下滑 翻下一页,
 *      UP/右滑/上滑 回上一页, ESC/ON 退出, 20s 超时自动退出
 *   6. 退出前清屏 (填背景色), 返回 LogStruct* 供日志回显
 *
 * 日志通过 port/prime_log.h 共享模块写入 (控制台中文回显)。
 * 编译宏 PRIMETEX_EVENT_DIAG=1 时额外输出事件级诊断 (排障用):
 *   [dr] 清理残留=N          — 启动时清掉的事件数
 *   [ev] t=T g=G ty=TYPE ... — 每次 TestPendEvent/GetEvent 的返回值与事件 hex
 */
#include <cstdint>

#include "nmarkdown/render/surface565.h"
#include "prime_log.h"
#include "prime_render_api.h"
#include "prime_svc.h"
#include "prime_sys.h"

namespace {

using nmarkdown::rgb565;

/* 演示页配色 */
const std::uint16_t kBackground = rgb565(250, 250, 245);  // 浅色纸
const std::uint16_t kInk = rgb565(30, 30, 30);
const std::uint16_t kAccent = rgb565(140, 60, 20);
const std::uint16_t kMuted = rgb565(120, 120, 120);

/* 提示行 (两页共用, 屏幕文字保持英文 — DejaVu 无中文字形) */
void render_hint_line() {
    prime_render_text("ENTER/DOWN/TAP: next   ESC/ON: exit", 6, 226, 11,
                      kMuted, kBackground, 0);
}

/* 页 0: 标题 + 正文 + 基础公式 */
void render_page_0() {
    prime_render_clear(kBackground);

    prime_render_text("PrimeTeX", 6, 32, 22, kAccent, kBackground, 0);
    prime_render_text("LaTeX formula rendering on HP Prime", 6, 62, 16,
                      kInk, kBackground, 0);
    prime_render_text("Embedded FreeType 2.14 + HarfBuzz", 6, 86, 16, kInk,
                      kBackground, 0);

    prime_render_formula("\\frac{1}{2}", 6, 112, 18, kInk, kBackground, 0,
                         300);
    prime_render_formula("\\sqrt{x^2 + 1}", 6, 140, 18, kInk, kBackground, 0,
                         300);
    prime_render_formula("e^{i\\pi} + 1 = 0", 6, 168, 18, kInk, kBackground,
                         0, 300);
    prime_render_formula("\\int_0^1 x^2\\,dx = \\frac{1}{3}", 6, 196, 17,
                         kInk, kBackground, 0, 300);

    render_hint_line();
}

/* 页 1: 公式画廊 (矩阵/极限/求和/括号/希腊字母/高斯分布) */
void render_page_1() {
    prime_render_clear(kBackground);

    prime_render_text("Formula Gallery", 6, 30, 20, kAccent, kBackground, 0);

    prime_render_formula("\\begin{pmatrix} 1 & 2 \\\\ 3 & 4 \\end{pmatrix}",
                         6, 62, 17, kInk, kBackground, 0, 300);
    prime_render_formula(
        "\\lim_{n \\to \\infty} \\left(1 + \\frac{1}{n}\\right)^n = e",
        6, 98, 17, kInk, kBackground, 0, 300);
    prime_render_formula("\\sum_{k=1}^{n} k = \\frac{n(n+1)}{2}", 6, 128,
                         17, kInk, kBackground, 0, 300);
    prime_render_formula("\\left[ \\frac{a}{b} \\right]^2", 6, 156, 17,
                         kInk, kBackground, 0, 300);
    prime_render_formula("\\alpha + \\beta = \\gamma", 6, 184, 17, kInk,
                         kBackground, 0, 300);
    prime_render_formula(
        "f(x) = \\frac{1}{\\sigma \\sqrt{2\\pi}}"
        "\\,e^{-\\frac{(x-\\mu)^2}{2\\sigma^2}}",
        6, 210, 14, kInk, kBackground, 0, 300);

    render_hint_line();
}

/* 渲染一页并返回 0=成功 */
int render_page(int page) {
    if (page == 0) {
        render_page_0();
    } else {
        render_page_1();
    }
    return 0;
}

/* 退出前清屏: 填背景色并送显, 避免屏幕残留演示画面 */
void clear_screen_on_exit() {
    if (prime_render_clear(kBackground) == 0) {
        prime_render_present();
    }
}

}  // namespace

/* 演示页 C API: 渲染第 1 页 (标题+基础公式), 供宿主直接调用 */
extern "C" int prime_render_demo_page(void) {
    render_page_0();
    return 0;
}

/* ---------- ELF 入口 (eventg1 权威结构) ----------
 * eventg1 main() 同款: 在 call 上下文直接 OSCreateThread 事件线程,
 * SetSystemVariable(85,2,1), 然后主线程轮询共享事件状态并做自己的工作。 */
extern "C" void* demo_main(void* arg);
extern "C" void prime_slot_patch(void*, const void*, int);
extern "C" void prime_input_hook(void*);
static volatile int g_demo_done;
static unsigned* const kSlot = (unsigned*)0x307FBFA0u;
static unsigned g_slot_save[2];

static void input_install(void) {
    g_slot_save[0] = kSlot[0];
    g_slot_save[1] = kSlot[1];
    unsigned p[2] = {0xE51FF004u, (unsigned)(void*)&prime_input_hook};
    prime_slot_patch(kSlot, p, 2);
}
static void input_restore(void) {
    if (g_slot_save[0] || g_slot_save[1]) {
        prime_slot_patch(kSlot, g_slot_save, 2);
    }
}

/* 入口只做"校验 + 起线程"：调试调用上下文的栈很小，初始化 FreeType/HarfBuzz
 * 会在入口直接溢出复位。主循环放到带显式栈大小的线程里（eventg1/doom 同款）。 */
extern "C" void* app_entry_c(PrimeBootConfig* cfg) {
    if (cfg == nullptr || cfg->magic != 0xD00BC760u) {
        return nullptr;
    }
    prime_set_services(reinterpret_cast<const PrimeServices*>(
        static_cast<std::uintptr_t>(cfg->services)));
    g_demo_done = 0;
    void* thr = prime_os_create_thread(reinterpret_cast<void*>(&demo_main), cfg,
                                       96u * 1024u, 0u);
    if (thr == nullptr) {
        /* 线程创建失败: 退回调用上下文执行（可能复位，但至少能跑） */
        return demo_main(cfg);
    }
    /* 不能立刻返回: 启动器会在返回后释放/复用映像，而线程还在跑。
     * 等主循环结束（demo_main 末尾置 g_demo_done）。 */
    while (!g_demo_done) {
        prime_os_sleep(50);
    }
    return nullptr;
}

/* 入口包装（nbCode 同款）：调用上下文只保证 4 字节 SP，而 GCC 假定 AAPCS 的
 * 8 字节对齐，会在函数里用 LDRD/STRD；ARM926 对非对齐的这类访问 data abort
 * → 整机复位。这里先保存 callee-saved、把 SP 对齐到 8，再进 C 代码。 */
extern "C" __attribute__((naked)) void* app_entry(PrimeBootConfig* cfg) {
    (void)cfg;
    __asm__ volatile(
        "push {r4, r5, r6, r7, r8, r9, r10, r11, ip, lr}\n"
        "bic  sp, sp, #7\n"
        "sub  sp, sp, #8\n"
        "bl   app_entry_c\n"
        "add  sp, sp, #8\n"
        "pop  {r4, r5, r6, r7, r8, r9, r10, r11, ip, pc}\n");
}

extern "C" void* demo_main(void* arg) {
    PrimeBootConfig* cfg = reinterpret_cast<PrimeBootConfig*>(arg);
    if (cfg == nullptr || cfg->magic != 0xD00BC760u) {
        return nullptr;
    }
    prime_set_services(reinterpret_cast<const PrimeServices*>(
        static_cast<std::uintptr_t>(cfg->services)));

    /* 输入走主线程 GetEvent（prime_render_wait_key 内部轮询）。
     * 不在调用上下文里 ClearAllEvents：真机上这一步会导致复位，
     * 事件由 GetEvent 自然排空。 */

    /* doom/eventg1 同款前置: 关系统电源管理 + 清屏 */
    prime_set_system_variable(85u /* SYSVAR_SYSPWCTRL */, 2u /* SET */,
                              1u /* TRUE */);
    prime_clear_screen(0);

    const int init_result = prime_render_init();
    if (init_result != 0) {
        prime_log_write("渲染初始化失败: ");
        prime_log_int(init_result);
        prime_log_write("\n");
        return prime_log_struct();
    }

    /* 帧缓冲诊断 (中文日志, 供显示排障) */
    PrimeFbInfo fb;
    if (prime_render_fb_info(&fb) == 0) {
        prime_log_write("帧缓冲: ");
        prime_log_int(static_cast<int>(fb.x_res));
        prime_log_write("x");
        prime_log_int(static_cast<int>(fb.y_res));
        prime_log_write(" bpp=");
        prime_log_int(static_cast<int>(fb.bpp));
        prime_log_write(" 行字节=");
        prime_log_int(static_cast<int>(fb.bytes_per_line));
        prime_log_write(" 地址=");
        prime_log_hex(fb.bufferstart);
        prime_log_write("\n");
    } else {
        prime_log_write("帧缓冲信息不可用\n");
    }

    /* 双页演示 + 按键/触摸循环 (主线程) */
    const int kPageCount = 2;
    constexpr int kQuit = 13;      /* InputEventType::Quit (ON)  */
    constexpr int kBack = 12;      /* InputEventType::Back (ESC) */
    constexpr int kActivate = 11;  /* InputEventType::Activate    */
    constexpr int kScrollDown = 2; /* InputEventType::ScrollLineDown */
    constexpr int kScrollUp = 1;   /* InputEventType::ScrollLineUp   */
    const int kTimeoutMs = 20000;  /* 每页 20 秒无操作自动退出 */

    int page = 0;
    while (page >= 0 && page < kPageCount) {
        if (render_page(page) != 0) {
            prime_log_write("演示渲染失败\n");
            break;
        }
        if (prime_render_present() != 0) {
            prime_log_write("送显失败\n");
            break;
        }

        prime_log_write("第 ");
        prime_log_int(page + 1);
        prime_log_write(" 页 已绘像素=");
        prime_log_int(prime_render_count_ink(kBackground));
        prime_log_write("\n");

        const int key = prime_render_wait_key(kTimeoutMs);
#ifdef PRIMETEX_EVENT_DIAG
        prime_log_write("[ev] 等待结果 key=");
        prime_log_int(key);
        prime_log_write("\n");
#endif
        if (key < 0) {  // 事件线程创建失败等异常
            prime_log_write("事件系统异常: ");
            prime_log_int(key);
            prime_log_write("\n");
            break;
        }
        if (key == kQuit || key == kBack) {
            break;
        }
        if (key == 0 /* timeout */) {
            prime_log_write("等待超时, 自动退出\n");
            break;
        }
        if (key == kActivate || key == kScrollDown) {
            ++page;
        } else if (key == kScrollUp) {
            --page;
        }
        /* 其他键: 忽略 */
    }

    clear_screen_on_exit();
    prime_log_write("PrimeTeX 演示结束\n");
    g_demo_done = 1;
    return prime_log_struct();
}
