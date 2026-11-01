/*
 * prime_platform.cpp — HP Prime 平台适配实现 (SVC 直调版)
 * ========================================================
 * v2: 不再依赖 Python 传 framebuffer 地址 —— 直接调用 Besta OS SVC:
 *   - Display:  SVC 0x1008D 获取活动 LCD (framebuffer, XRGB32), 内部
 *               RGB565 渲染缓冲, present 时转换写入并 D-Cache clean。
 *   - Input:    SVC 0x10040 非阻塞轮询挂起事件, HP 键码 → InputEvent。
 *   - Clock:    SVC 0x100A5 GetSysTime, SVC 0x10008 OSSleep。
 *   - File:     渲染库形态无外部文件, stub。
 * 服务表 (PrimeServices) 仍作为后备: 若 SVC 不可用 (模拟环境), 可用其
 * framebuffer 字段 (此时假定 RGB565 布局)。
 */
#include "prime_platform.h"

#include <cstdint>
#include <vector>

#include "prime_log.h"
#include "prime_svc.h"

namespace nmarkdown {
namespace {

/* D-Cache clean: 与 ndless 版同一思路 (LCD DMA 前回写脏行)。
 * ARM926EJ-S (G1) 与 Cortex-A7 (G2) 均支持 CP15 c7,c10 操作。 */
void clean_dcache_range(const void* data, std::size_t size) {
    if (data == nullptr || size == 0) return;
    const std::uintptr_t kCacheLineBytes = 32;
    std::uintptr_t current =
        reinterpret_cast<std::uintptr_t>(data) & ~(kCacheLineBytes - 1U);
    const std::uintptr_t end =
        (reinterpret_cast<std::uintptr_t>(data) + size + kCacheLineBytes - 1U) &
        ~(kCacheLineBytes - 1U);
    for (; current < end; current += kCacheLineBytes) {
        __asm volatile("mcr p15, 0, %0, c7, c10, 1" : : "r"(current) : "memory");
    }
    const std::uint32_t zero = 0;
    __asm volatile("mcr p15, 0, %0, c7, c10, 4" : : "r"(zero) : "memory");
}

/* RGB565 → XRGB32 (Besta 活动 LCD 帧缓冲格式, doom.c 同款) */
inline std::uint32_t rgb565_to_xrgb32(std::uint16_t c) {
    const std::uint32_t r = (c >> 11U) & 0x1FU;
    const std::uint32_t g = (c >> 5U) & 0x3FU;
    const std::uint32_t b = c & 0x1FU;
    return 0xFF000000U | ((r << 3U) | (r >> 2U)) << 16U |
           ((g << 2U) | (g >> 4U)) << 8U | ((b << 3U) | (b >> 2U));
}

/* ---------- 事件线程共享状态 (主线程读, 事件线程写) ---------- */
volatile int      g_ev_ready = 0;      /* 1 = 有事件待主线程消费 */
volatile uint32_t g_ev_out_type = 0;   /* 解析后的 InputEventType */
volatile uint32_t g_ev_raw_type = 0;   /* 原始事件类型 (诊断)     */
volatile uint32_t g_ev_raw_w8 = 0;
volatile uint32_t g_ev_raw_w9 = 0;
void* g_ev_thread = nullptr;

/* HP Prime 物理键扫描码 → nMarkdown 语义事件。
 * 扫描码表来自 HP Prime 键盘布局 (与 devkit 的 HP_KEY 一致)。 */
InputEvent map_scan_code(std::uint32_t scan) {
    InputEvent ev;
    switch (scan) {
        case 0x03: ev.type = InputEventType::ScrollLineUp; break;    /* UP    */
        case 0x05: ev.type = InputEventType::ScrollLineDown; break;  /* DOWN  */
        case 0x02: ev.type = InputEventType::SwipeLeft; break;       /* LEFT  */
        case 0x04: ev.type = InputEventType::SwipeRight; break;      /* RIGHT */
        case 0x0D: ev.type = InputEventType::Activate; break;        /* ENTER */
        case 0x01: ev.type = InputEventType::Back; break;            /* ESC   */
        case 0x83: ev.type = InputEventType::Quit; break;            /* ON    */
        case 0xB9: ev.type = InputEventType::OpenMenu; break;        /* +     */
        case 0xB1: ev.type = InputEventType::OpenBookmarks; break;   /* APPS  */
        case 0x8B: ev.type = InputEventType::OpenSettings; break;    /* SHIFT */
        default: ev.type = InputEventType::None; break;
    }
    return ev;
}

}  // namespace

/* ---------- Display ---------- */
DisplayPrime::DisplayPrime(const PrimeServices* services)
    : services_(services) {}

bool DisplayPrime::initialize() {
    // 1) 优先 SVC 获取真实活动 LCD (XRGB32)
    PrimeLCD** pp_lcd = prime_get_lcd();
    if (pp_lcd != nullptr && *pp_lcd != nullptr &&
        (*pp_lcd)->window1_bufferstart != 0) {
        fb_addr_ = (*pp_lcd)->window1_bufferstart;
        fb_width_ = (*pp_lcd)->x_res;
        fb_height_ = (*pp_lcd)->y_res;
        fb_format_ = FbFormat::Xrgb32;
    } else if (services_ != nullptr && services_->framebuffer != 0) {
        // 2) 后备: 服务表 framebuffer (假定 RGB565 布局)
        fb_addr_ = services_->framebuffer;
        fb_width_ = static_cast<int>(services_->fb_width);
        fb_height_ = static_cast<int>(services_->fb_height);
        fb_format_ = FbFormat::Rgb565;
    } else {
        return false;
    }
    if (fb_width_ <= 0 || fb_height_ <= 0) {
        fb_format_ = FbFormat::None;
        return false;
    }
    // 内部 RGB565 渲染缓冲: 固定 320x240 逻辑画布 (横屏)。
    // 物理 framebuffer 的竖屏/偏移由 present() 自适应转换。
    try {
        offscreen_.assign(static_cast<std::size_t>(kLogicalWidth) *
                              kLogicalHeight,
                          0);
    } catch (...) {
        fb_format_ = FbFormat::None;
        return false;
    }
    render_surface_ =
        Surface565(offscreen_.data(), kLogicalWidth, kLogicalHeight,
                   kLogicalWidth);
    initialized_ = render_surface_.valid();
    return initialized_;
}

void DisplayPrime::shutdown() {
    initialized_ = false;
    fb_format_ = FbFormat::None;
    render_surface_ = Surface565();
    offscreen_.clear();
}

Surface565 DisplayPrime::surface() {
    return initialized_ ? render_surface_ : Surface565();
}

bool DisplayPrime::fb_info(FbInfo& out) const {
    if (!initialized_) return false;
    out.x_res = static_cast<uint32_t>(fb_width_);
    out.y_res = static_cast<uint32_t>(fb_height_);
    out.bpp = 32;
    out.bytes_per_line = static_cast<uint32_t>(fb_width_) * 4u;
    out.bufferstart = fb_addr_;
    out.format = fb_format_ == FbFormat::Xrgb32
                     ? 1
                     : (fb_format_ == FbFormat::Rgb565 ? 2 : 0);
    return true;
}

/* present: 把 320x240 逻辑画布写入物理 framebuffer。
 * 布局策略 (由服务表 display_flags 配置, 默认 0 = 自动):
 *   - 竖屏 (y_res > x_res): 内容顺时针旋转 90° 后写入 (内容占满竖屏)
 *   - 横屏: 直写
 *   - 居中: 内容在物理屏上居中 (默认开)
 *   - flip_y: framebuffer y 轴反向时置 bit0 (本机无 VFP, 整数运算) */
void DisplayPrime::present() {
    if (!initialized_) return;
    const std::uint32_t flags =
        services_ != nullptr ? services_->display_flags : 0u;
    const bool auto_mode = flags == 0u;
    const bool flip_y = (flags & 1u) != 0;
    const bool force_rotate = (flags & 2u) != 0;
    const bool center_off = (flags & 4u) != 0;
    const bool portrait = fb_height_ > fb_width_;
    const bool do_rotate = force_rotate || (auto_mode && portrait);
    const bool do_center = !center_off;

    const int cw = kLogicalWidth;
    const int ch = kLogicalHeight;

    switch (fb_format_) {
        case FbFormat::Xrgb32: {
            // RGB565 逻辑画布 → XRGB32 活动 LCD 帧缓冲
            std::uint32_t* fb = reinterpret_cast<std::uint32_t*>(
                static_cast<std::uintptr_t>(fb_addr_));
            const int fb_w = fb_width_;
            const int fb_h = fb_height_;
            if (do_rotate) {
                // 顺时针 90°: 内容 (cx, cy) → fb (fx=ch-1-cy, fy=cx)
                // 旋转后内容尺寸 = ch x cw (竖屏)
                const int ox = do_center ? (fb_w - ch) / 2 : 0;
                const int oy = do_center ? (fb_h - cw) / 2 : 0;
                for (int cy = 0; cy < ch; ++cy) {
                    const std::uint16_t* src = render_surface_.row(cy);
                    const int fx = ch - 1 - cy + ox;
                    if (fx < 0 || fx >= fb_w) continue;
                    std::uint32_t* dst = fb + fx;
                    for (int cx = 0; cx < cw; ++cx) {
                        const int fy = cx + oy;
                        if (fy < 0 || fy >= fb_h) continue;
                        dst[static_cast<std::size_t>(fy) * fb_w] =
                            rgb565_to_xrgb32(src[cx]);
                    }
                }
            } else {
                const int ox = do_center ? (fb_w - cw) / 2 : 0;
                const int oy = do_center ? (fb_h - ch) / 2 : 0;
                for (int y = 0; y < ch; ++y) {
                    const std::uint16_t* src = render_surface_.row(y);
                    const int fy = oy + (flip_y ? (ch - 1 - y) : y);
                    if (fy < 0 || fy >= fb_h) continue;
                    std::uint32_t* dst =
                        fb + static_cast<std::size_t>(fy) * fb_w;
                    for (int x = 0; x < cw; ++x) {
                        if (ox + x >= fb_w) break;
                        if (ox + x < 0) continue;
                        dst[ox + x] = rgb565_to_xrgb32(src[x]);
                    }
                }
            }
            clean_dcache_range(fb,
                               static_cast<std::size_t>(fb_w) * fb_h * 4U);
            break;
        }
        case FbFormat::Rgb565: {
            // 服务表 framebuffer: 直接 565 拷贝 (行距按 stride_bytes)
            std::uint16_t* fb = reinterpret_cast<std::uint16_t*>(
                static_cast<std::uintptr_t>(fb_addr_));
            const std::size_t stride_words =
                services_ != nullptr
                    ? static_cast<std::size_t>(services_->fb_stride_bytes) / 2U
                    : static_cast<std::size_t>(fb_width_);
            if (do_rotate) {
                const int ox = do_center ? (fb_width_ - ch) / 2 : 0;
                const int oy = do_center ? (fb_height_ - cw) / 2 : 0;
                for (int cy = 0; cy < ch; ++cy) {
                    const std::uint16_t* src = render_surface_.row(cy);
                    const int fx = ch - 1 - cy + ox;
                    if (fx < 0 || fx >= fb_width_) continue;
                    for (int cx = 0; cx < cw; ++cx) {
                        const int fy = cx + oy;
                        if (fy < 0 || fy >= fb_height_) continue;
                        fb[static_cast<std::size_t>(fy) * stride_words + fx] =
                            src[cx];
                    }
                }
            } else {
                const int ox = do_center ? (fb_width_ - cw) / 2 : 0;
                const int oy = do_center ? (fb_height_ - ch) / 2 : 0;
                for (int y = 0; y < ch; ++y) {
                    const std::uint16_t* src = render_surface_.row(y);
                    const int fy = oy + (flip_y ? (ch - 1 - y) : y);
                    if (fy < 0 || fy >= fb_height_) continue;
                    for (int x = 0; x < cw; ++x) {
                        if (ox + x < 0 || ox + x >= fb_width_) continue;
                        fb[static_cast<std::size_t>(fy) * stride_words + ox +
                           x] = src[x];
                    }
                }
            }
            clean_dcache_range(
                fb, static_cast<std::size_t>(fb_width_) * 2U * fb_height_);
            break;
        }
        case FbFormat::None:
            break;
    }
}

/* ---------- Input ---------- */
InputPrime::InputPrime(const PrimeServices* services) : services_(services) {}

void InputPrime::drain(void) {
    /* 事件线程的 ClearAllEvents() 已清空队列; 这里仅清共享标志与触摸态 */
    g_ev_ready = 0;
}

bool InputPrime::poll(InputEvent& event) {
    event = InputEvent();
    if (!g_ev_ready) {
        return true;
    }
#ifdef PRIMETEX_EVENT_DIAG
    prime_log_write("[ev] ty=");
    prime_log_hex(g_ev_raw_type);
    prime_log_write(" w8=");
    prime_log_hex(g_ev_raw_w8);
    prime_log_write(" w9=");
    prime_log_hex(g_ev_raw_w9);
    prime_log_write(" out=");
    prime_log_int(static_cast<int>(g_ev_out_type));
    prime_log_write("\n");
#endif
    event.type = static_cast<InputEventType>(g_ev_out_type);
    g_ev_ready = 0;
    return true;
}

/* ---------- 事件线程 (eventg1 权威结构) ----------
 * eventg1/doom 事件查询法 (HP/g1sdk 官方示例):
 *   OSCreateThread(event_manager, 0, 0, 0)   ← call 上下文直接创建, 栈=0
 *   线程内: ClearAllEvents(); for(;;) if (GetEvent(&e)) { 解析; 写共享 }
 *   主线程: 轮询共享变量 (prime_event/prime_keydown/prime_mousex/y)。
 * 此前失败根因: ① 事件缓冲仅 32B (ui_event_t ≥ 68B, GetEvent 越界写栈);
 * ② 事件线程由 OSCreateThread 线程嵌套创建 (应为 call 上下文); ③ 未调
 * ClearAllEvents(); ④ 栈用 0x80000 (应为 0 系统默认)。 */
namespace {

/* 事件线程函数: ClearAllEvents + 阻塞 GetEvent → 解析 → 写共享状态 */
int event_thread_fn(void*) {
    std::uint32_t buf[18];     // ui_event_t 完整结构 (eventg1 打印到 ptr[16])
    bool t_down = false;
    int t_x0 = 0, t_y0 = 0;
    for (;;) {
        if (!prime_get_event(buf)) {
            /* GetEvent 阻塞: 无事件时不会走到这里 (doom/eventg1 同款) */
            continue;
        }
        const std::uint32_t type = buf[7];
        std::uint32_t out = 0;
        if (type == 0x10U) {  // key down
            out = static_cast<uint32_t>(map_scan_code(buf[8] >> 16U).type);
        } else if (type == 0x1U) {  // touch begin
            t_down = true;
            t_x0 = static_cast<int>(buf[8] >> 16U);
            t_y0 = static_cast<int>(buf[9]);
        } else if (type == 0x2U) {
            /* touch move: 忽略 (以按下/抬起位置判定滑动) */
        } else if (type == 0x8U && t_down) {  // touch end
            const int dx = static_cast<int>(buf[8] >> 16U) - t_x0;
            const int dy = static_cast<int>(buf[9]) - t_y0;
            t_down = false;
            if (dx < -30) {            /* 左滑 → 下一页 */
                out = static_cast<uint32_t>(InputEventType::ScrollLineDown);
            } else if (dx > 30) {      /* 右滑 → 上一页 */
                out = static_cast<uint32_t>(InputEventType::ScrollLineUp);
            } else if (dy > 30) {      /* 下滑 → 下一页 */
                out = static_cast<uint32_t>(InputEventType::ScrollLineDown);
            } else if (dy < -30) {     /* 上滑 → 上一页 */
                out = static_cast<uint32_t>(InputEventType::ScrollLineUp);
            } else {                   /* 点按 → 下一页 */
                out = static_cast<uint32_t>(InputEventType::Activate);
            }
        }
        /* keyup (0x100000): 忽略 */
        if (out != 0) {
            g_ev_raw_type = buf[7];
            g_ev_raw_w8 = buf[8];
            g_ev_raw_w9 = buf[9];
            g_ev_out_type = out;
            g_ev_ready = 1;
        }
    }
    return 0;
}

}  // namespace

/* 主线程非阻塞取一个事件（路线 A 使用；Upsilon 路线不需要）。
 * 返回事件类型；-2 = 无事件；触摸按下/抬起之间的滑动在这里合成。 */
extern "C" volatile unsigned g_dbg_w7 = 0;
extern "C" volatile unsigned g_dbg_w8 = 0;
extern "C" volatile int g_dbg_n = 0;
extern "C" volatile unsigned g_dbg_scan = 0;
extern "C" volatile int g_dbg_ev = 0;
static volatile int g_pending;      /* 解码后的事件类型，0 = 无 */
static volatile int g_touch_down;
static volatile int g_tx0, g_ty0;

/* SDK prime_input.S 的 prime_privileged_memcpy：字节拷贝 + CP15 维护 */
extern "C" __attribute__((naked)) void prime_priv_memcpy(void* dst,
                                                         const void* src,
                                                         unsigned n) {
    (void)dst; (void)src; (void)n;
    __asm__ volatile(
        "push {r4, r5, r6, lr}\n"
        "mrs r4, apsr\n"
        "orr r5, r4, #128\n"
        "msr cpsr_c, r5\n"
        "mrc p15, 0, r6, c3, c0, 0\n"
        "mvn r5, #0\n"
        "mcr p15, 0, r5, c3, c0, 0\n"
        "1:\n"
        "cmp r2, #0\n"
        "beq 2f\n"
        "ldrb r3, [r1], #1\n"
        "strb r3, [r0], #1\n"
        "subs r2, r2, #1\n"
        "bne 1b\n"
        "2:\n"
        "mov r3, #0\n"
        "mcr p15, 0, r3, c7, c10, 1\n"
        "mcr p15, 0, r3, c7, c5, 0\n"
        "mcr p15, 0, r3, c7, c10, 4\n"
        "mcr p15, 0, r6, c3, c0, 0\n"
        "msr cpsr_c, r4\n"
        "pop {r4, r5, r6, pc}\n");
}

#define PRIME_HOOK_TARGET 0x307FBFA0u
static unsigned char g_saved_code[16] __attribute__((aligned(4)));
static unsigned g_tramp[2] __attribute__((aligned(4)));
static int g_hook_installed;

extern "C" int prime_hook_install(void* cb) {
    if (g_hook_installed || !cb) {
        return 0;
    }
    prime_priv_memcpy(g_saved_code, (const void*)PRIME_HOOK_TARGET, 16u);
    g_tramp[0] = 0xE51FF004u;
    g_tramp[1] = (unsigned)(unsigned long)cb;
    prime_priv_memcpy((void*)PRIME_HOOK_TARGET, g_tramp, 8u);
    g_hook_installed = 1;
    return 1;
}

extern "C" void prime_hook_remove(void) {
    if (!g_hook_installed) {
        return;
    }
    prime_priv_memcpy((void*)PRIME_HOOK_TARGET, g_saved_code, 16u);
    g_hook_installed = 0;
}

extern "C" void prime_input_hook(void* evp) {
    /* 判据照 keymon（真机实测）：不看 +4 的 type。
     *   w7 = *(u32*)(ev+28):  16 按下 / 0x100000 抬起；触摸 1/2/8
     *   w8 = *(u32*)(ev+32):  按键时 scan 在高 16 位
     */
    unsigned char* e = reinterpret_cast<unsigned char*>(evp);
    if (!prime_get_event(evp)) {
        return;
    }
    const std::uint32_t w7 = *reinterpret_cast<std::uint32_t*>(e + 28);
    g_dbg_w7 = w7;
    g_dbg_w8 = *reinterpret_cast<std::uint32_t*>(e + 32);
    g_dbg_n++;
    const std::uint32_t w8 = *reinterpret_cast<std::uint32_t*>(e + 32);
    if (w7 == 16U) {                            /* key press */
        const unsigned scan = w8 >> 16;
        const InputEvent ev = map_scan_code(scan);
        g_dbg_scan = scan;
        g_dbg_ev = static_cast<int>(ev.type);
        if (ev.type != InputEventType::None) {
            g_pending = static_cast<int>(ev.type);
        }
    } else if (w7 == 1U) {                      /* touch press -> 下一页 */
        g_pending = static_cast<int>(InputEventType::Activate);
    }
}

extern "C" int prime_poll_key_event(void) {
    /* 路线 A：非阻塞 GetPendEvent（0x10040），事件结构与 GetEvent 相同 */
    std::uint32_t buf[18];
    if (!prime_get_pend_event(buf)) {
        return -2;
    }
    const std::uint32_t type = buf[7];
    if (type == 0x10U) {
        const InputEvent e = map_scan_code(buf[8] >> 16U);
        return e.type == InputEventType::None ? -2 : static_cast<int>(e.type);
    }
    if (type == 0x1U || type == 0x8U) {
        /* 触摸: 按下/抬起都当作"点按=下一页" */
        return static_cast<int>(InputEventType::Activate);
    }
    return -2;
}

int prime_event_thread_start(void) {
    if (g_ev_thread != nullptr) {
        return 0;  // 已启动 (幂等)
    }
    /* 栈=0 系统默认 (eventg1/doom 同款), 由 call 上下文直接创建 */
    g_ev_thread = prime_os_create_thread(
        reinterpret_cast<void*>(&event_thread_fn), nullptr, 0u, 0u);
#ifdef PRIMETEX_EVENT_DIAG
    prime_log_write("[thr] 事件线程=");
    if (g_ev_thread != nullptr) {
        prime_log_hex(reinterpret_cast<uint32_t>(g_ev_thread));
    } else {
        prime_log_write("创建失败");
    }
    prime_log_write("\n");
#endif
    return g_ev_thread != nullptr ? 0 : -1;
}

void prime_event_thread_stop(void) {
    if (g_ev_thread != nullptr) {
        prime_os_terminate_thread(g_ev_thread, 0);
        g_ev_thread = nullptr;
    }
    g_ev_ready = 0;
}

/* ---------- Clock ---------- */
ClockPrime::ClockPrime(const PrimeServices* services) : services_(services) {}

std::uint64_t ClockPrime::milliseconds() const {
    PrimeDateTime dt;
    prime_get_sys_time(&dt);
    return ((static_cast<std::uint64_t>(dt.hour) * 60U + dt.minute) * 60U +
            dt.second) * 1000U + dt.millis;
}

void ClockPrime::sleep_ms(std::uint32_t duration) {
    prime_os_sleep(duration);
}

/* ---------- FileSystem (stub) ---------- */
bool FileSystemPrime::probe(const char*, DocumentProbe& result,
                            std::string& error) {
    result = DocumentProbe();
    error = "filesystem is not supported by the Prime render-library build";
    return false;
}

bool FileSystemPrime::read_all(const char*, std::size_t,
                               std::vector<std::uint8_t>& data,
                               std::string& error) {
    data.clear();
    error = "filesystem is not supported by the Prime render-library build";
    return false;
}

bool FileSystemPrime::read_range(const char*, std::uint64_t, std::uint8_t*,
                                 std::size_t, std::string& error) {
    error = "filesystem is not supported by the Prime render-library build";
    return false;
}

bool FileSystemPrime::write_atomic(const char*, const std::uint8_t*, std::size_t,
                                   std::string& error) {
    error = "filesystem is not supported by the Prime render-library build";
    return false;
}

}  // namespace nmarkdown
