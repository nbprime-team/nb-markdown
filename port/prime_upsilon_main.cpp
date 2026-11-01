/*
 * prime_upsilon_main.cpp — PrimeMarkdown Upsilon 主程序 (v18)
 * ===========================================================
 * PrimeMarkdown: HP Prime (Upsilon) markdown 阅读器。
 *   - 文档选择页: 扫描 c:\apps\*.md (目录枚举 _afindfirst)
 *   - 渲染: 复用原版 nMarkdown 布局引擎 (block_layout + md4c),
 *     run 级混排 (文本/强调/代码/行内公式), 词级折行 + 滚动分页
 *   - 翻页浏览, 文档内 Esc 返回选择页, 选择页 ON 提示强退
 * 保持 v7-v9 已验证骨架: 前置 SVC + 主线程 GetEvent + 死循环 + ClearAllEvents。
 * 已知限制: 文本用 DejaVu (无 CJK 字形, 中文显示为方块; 公式不受影响)。
 */
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <muteki.h>

#include "nmarkdown/render/surface565.h"
#include "prime_layout.h"
#include "prime_log.h"
#include "prime_render_api.h"
#include "prime_svc.h"

using nmarkdown::rgb565;

namespace {

extern "C" long _filesize(void* stream);  /* SVC 0x100CB */

/* ---------- 常量 ---------- */
const std::uint16_t kBg = rgb565(250, 250, 245);
const std::uint16_t kInk = rgb565(30, 30, 30);
const std::uint16_t kAccent = rgb565(140, 60, 20);
const std::uint16_t kMuted = rgb565(120, 120, 120);
const std::uint16_t kSelect = rgb565(220, 230, 250);  /* 选中项背景 */

constexpr int kMaxFiles = 16;         /* 选择页文件上限 */
constexpr int kFileBuf = 60000;       /* 文档最大 60KB */
constexpr int kMargin = 10;           /* 左右留白 */
constexpr int kPageTop = 24;          /* 内容区顶部 */
constexpr int kPageBottom = 234;      /* 内容区底部 (无页脚, 复用原页脚空间) */
constexpr int kPageH = kPageBottom - kPageTop;  /* 一页可用高度 210 */

/* ---------- 文件工具 ---------- */
char* read_file_alloc(const char* path, int* out_size) {
    file_descriptor_t* f = _afopen(path, "rb");
    if (f == nullptr) {
        return nullptr;
    }
    const long sz = _filesize(f);
    if (sz <= 0 || sz > kFileBuf) {
        _fclose(f);
        return nullptr;
    }
    char* buf = static_cast<char*>(malloc(static_cast<std::size_t>(sz) + 1));
    if (buf == nullptr) {
        _fclose(f);
        return nullptr;
    }
    const std::size_t rd = _fread(buf, 1, static_cast<std::size_t>(sz), f);
    _fclose(f);
    buf[rd] = '\0';
    *out_size = static_cast<int>(rd);
    return buf;
}

/* ---------- UI ---------- */
void render_hint_line(const char* hint) {
    prime_render_text(hint, kMargin, 226, 11, kMuted, kBg, 0);
}

/* ---------- 文档选择页 ---------- */
struct FileEntry {
    char name[16];
    long size;
};

int scan_md_files(FileEntry files[], int max) {
    int count = 0;
    find_context_t ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    if (_afindfirst("c:\\apps\\*.md", &ctx, 0) == 0) {
        do {
            if (ctx.filename != nullptr && count < max) {
                std::strncpy(files[count].name, ctx.filename, 15);
                files[count].name[15] = '\0';
                files[count].size = static_cast<long>(ctx.size);
                ++count;
            }
        } while (_afindnext(&ctx) == 0 && count < max);
    }
    return count;
}

void render_selector(const FileEntry files[], int count, int sel) {
    prime_render_clear(kBg);
    prime_render_text("PrimeMarkdown", kMargin, 22, 20, kAccent, kBg, 0);
    prime_render_text("Select doc in c:\\apps  (ASCII+LaTeX only)",
                      kMargin, 48,
                      12, kMuted, kBg, 0);
    int y = 72;
    for (int i = 0; i < count && i < kMaxFiles; ++i) {
        if (i == sel) {
            /* 选中: 背景条 + 强调色 */
            prime_render_text("> ", kMargin, y, 14, kAccent, kBg, 0);
            prime_render_text(files[i].name, 24, y, 15, kAccent, kBg, 0);
        } else {
            prime_render_text(files[i].name, 24, y, 15, kInk, kBg, 0);
        }
        y += 24;
    }
    render_hint_line("ENTER open  UP/DOWN pick  ON force-exit");
    prime_render_present();
}

/* 文档页: 原版布局引擎渲染第 page 页 (无页脚) */
void render_doc_page(int page, int total_pages) {
    (void)total_pages;
    prime_layout_render_page(page, kPageH);
    prime_render_present();
}

/* ---------- 按键 ---------- */
constexpr unsigned kKeyEnter = 0x0D;
constexpr unsigned kKeyUp = 0x03;
constexpr unsigned kKeyDown = 0x05;
constexpr unsigned kKeyLeft = 0x02;
constexpr unsigned kKeyRight = 0x04;
constexpr unsigned kKeyEsc = 0x01;
constexpr unsigned kKeyOn = 0x83;
constexpr unsigned kKeyAdd = 0xB9;  /* + 键 — 重新扫描 */

}  // namespace

/* ---------- 全局状态 (BSS) ----------
 * FileEntry[] 放全局避免 AppMain 线程栈溢出
 * (栈溢出曾导致: 退回选择页文件名丢失、文档渲染异常/仅一页) */
static FileEntry g_files[kMaxFiles];
static int g_file_count = 0;
static bool g_in_doc = false;
static int g_sel = 0;
static int g_doc_pages = 0;
static int g_doc_page = 0;

extern "C" int main(void) {
    Printf("\fPrimeMarkdown\n");

    /* v7 已验证前置 */
    prime_set_system_variable(85u, 2u, 1u);
    prime_clear_screen(0);

    const int init_result = prime_render_init();
    if (init_result != 0) {
        Printf("init fail %d\n", init_result);
        for (;;) {
            prime_os_sleep(1000);
        }
    }
    OSSleep(400);

    /* 扫描文档 (全局缓冲) */
    g_file_count = scan_md_files(g_files, kMaxFiles);

    /* 状态 (全局) */
    g_in_doc = false;
    g_sel = 0;
    g_doc_pages = 0;
    g_doc_page = 0;

    if (g_file_count > 0) {
        render_selector(g_files, g_file_count, g_sel);
    } else {
        /* 无文档: 提示页 */
        prime_render_clear(kBg);
        prime_render_text("PrimeMarkdown", kMargin, 60, 20, kAccent, kBg, 0);
        prime_render_text("No .md found in c:\\apps", kMargin, 100, 15,
                          kInk,
                          kBg, 0);
        render_hint_line("+ rescan  ON force-exit");
        prime_render_present();
    }

    std::uint32_t buf[18];
    for (;;) {
        if (!prime_get_event(buf)) {
            prime_os_sleep(30);
            continue;
        }
        const std::uint32_t type = buf[7];
        if (type == 0x10U) {  /* keydown */
            const unsigned key = buf[8] >> 16U;
            if (g_in_doc) {
                /* ---- 文档视图 ---- */
                if (key == kKeyEsc) {
                    g_in_doc = false;
                    prime_layout_close();
                    if (g_file_count > 0) {
                        render_selector(g_files, g_file_count, g_sel);
                    }
                } else if (key == kKeyOn) {
                    prime_clear_all_events();
                    prime_clear_screen(0);
                    return 0;  /* v9 行为: 即使重启 (见 DEVELOPMENT_LOG) */
                } else if (key == kKeyEnter || key == kKeyDown ||
                           key == kKeyRight) {
                    if (g_doc_page < g_doc_pages - 1) {
                        ++g_doc_page;
                        render_doc_page(g_doc_page, g_doc_pages);
                    }
                } else if (key == kKeyUp || key == kKeyLeft) {
                    if (g_doc_page > 0) {
                        --g_doc_page;
                        render_doc_page(g_doc_page, g_doc_pages);
                    }
                }
            } else {
                /* ---- 选择页 / 无文档页 ---- */
                if (g_file_count == 0) {
                    if (key == kKeyAdd) {
                        /* 重新扫描 */
                        const int n = scan_md_files(g_files, kMaxFiles);
                        if (n > 0) {
                            g_sel = 0;
                            render_selector(g_files, n, 0);
                        }
                    } else if (key == kKeyOn || key == kKeyEsc) {
                        prime_clear_all_events();
                        prime_clear_screen(0);
                        return 0;  /* v9 行为: 即使重启 */
                    }
                    continue;
                }
                if (key == kKeyEnter) {
                    /* 打开选中文档 */
                    char path[48];
                    std::strcpy(path, "c:\\apps\\");
                    std::strcat(path, g_files[g_sel].name);
                    int size = 0;
                    char* buf2 = read_file_alloc(path, &size);
                    if (buf2 != nullptr) {
                        const int rc = prime_layout_open(buf2, size);
                        free(buf2);
                        if (rc == 0) {
                            g_doc_pages = prime_layout_page_count(kPageH);
                            if (g_doc_pages < 1) {
                                g_doc_pages = 1;
                            }
                            g_doc_page = 0;
                            g_in_doc = true;
                            render_doc_page(g_doc_page, g_doc_pages);
                        }
                    }
                } else if (key == kKeyDown || key == kKeyRight) {
                    if (g_sel < g_file_count - 1) {
                        ++g_sel;
                        render_selector(g_files, g_file_count, g_sel);
                    }
                } else if (key == kKeyUp || key == kKeyLeft) {
                    if (g_sel > 0) {
                        --g_sel;
                        render_selector(g_files, g_file_count, g_sel);
                    }
                } else if (key == kKeyOn || key == kKeyEsc) {
                    prime_clear_all_events();
                    prime_clear_screen(0);
                    return 0;  /* v9 行为: 即使重启 */
                }
            }
        } else if (type == 0x1U || type == 0x8U) {  /* touch */
            if (g_in_doc) {
                if (g_doc_page < g_doc_pages - 1) {
                    ++g_doc_page;
                    render_doc_page(g_doc_page, g_doc_pages);
                }
            } else if (g_file_count > 0) {
                /* 触屏选择: 按 y 位置估算 */
                const int ty = static_cast<int>(buf[9]);
                const int idx = (ty - 72) / 24;
                if (idx >= 0 && idx < g_file_count) {
                    g_sel = idx;
                }
            }
        }
        prime_clear_all_events();
        prime_os_sleep(30);
    }
    /* 永不 return */
}
