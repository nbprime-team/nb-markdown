#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_elf.py — 校验 HP Prime 引导器可加载的 ELF 形态。

用法:
    python3 check_elf.py path/to/xxx.elf [--relocs] [--quiet]

校验项 (与 DOOM 项目实测的 puredoom.elf 形态一致):
  1. e_machine == 40 (EM_ARM)
  2. e_type == 3 (ET_DYN) — 加载到任意 malloc 基址的关键
  3. 首个 PT_LOAD 段 vaddr == 0 (shellcode 按 基址+p_vaddr 拷贝)
  4. 入口 e_entry 为段内偏移 (非绝对地址)
  5. .rel.dyn 全部为 R_ARM_RELATIVE (23) 且无符号引用 (r_info>>8 == 0)
     —— 加载器只需对每条做 *slot += base
  6. 无未定义符号 (.dynsym 中 st_shndx == SHN_UNDEF 的全局符号)

退出码: 0 = 全部通过; 1 = 失败。
"""
import struct
import sys

EM_ARM = 40
ET_DYN = 3
PT_LOAD = 1
R_ARM_RELATIVE = 23
SHN_UNDEF = 0


def read_cstr(data, offset):
    end = data.find(b"\x00", offset)
    if end < 0:
        end = len(data)
    return data[offset:end].decode("utf-8", "replace")


def load_sections(f):
    f.seek(0)
    ehdr = f.read(52)
    if len(ehdr) < 52 or ehdr[0:4] != b"\x7fELF":
        sys.exit(f"not an ELF file: {path}")
    e_type, e_machine = struct.unpack_from("<HH", ehdr, 16)
    e_entry = struct.unpack_from("<I", ehdr, 24)[0]
    e_phoff = struct.unpack_from("<I", ehdr, 28)[0]
    e_shoff = struct.unpack_from("<I", ehdr, 32)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", ehdr, 42)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", ehdr, 46)

    # program headers
    f.seek(e_phoff)
    phdrs = []
    for i in range(e_phnum):
        ph = f.read(e_phentsize)
        if len(ph) < e_phentsize:
            break
        p_type, p_offset, p_vaddr, _, p_filesz, p_memsz, _, _ = \
            struct.unpack_from("<IIIIIIII", ph, 0)
        phdrs.append((p_type, p_offset, p_vaddr, p_filesz, p_memsz))

    # section headers
    f.seek(e_shoff)
    sects = []
    for i in range(e_shnum):
        sh = f.read(e_shentsize)
        if len(sh) < e_shentsize:
            break
        name_off, s_type, _, s_addr, s_off, s_size, _, _, _, _ = \
            struct.unpack_from("<IIIIIIIIII", sh, 0)
        sects.append((name_off, s_type, s_addr, s_off, s_size))
    if e_shstrndx < len(sects) and e_shstrndx != 0:
        f.seek(sects[e_shstrndx][3])
        shstr = f.read(sects[e_shstrndx][4])
    else:
        shstr = b""

    def sec_name(o):
        return read_cstr(shstr, o)

    return {
        "e_type": e_type, "e_machine": e_machine, "e_entry": e_entry,
        "phdrs": phdrs, "sects": sects, "sec_name": sec_name,
    }


def check_no_vfp(path):
    import subprocess
    out = subprocess.run(["arm-none-eabi-objdump", "-d", path],
                         capture_output=True, text=True).stdout
    bad = [l for l in out.splitlines()
           if any(op in l for op in ("vldr", "vstr", "vmov", "vadd", "vsub",
                                     "vmul", "vdiv", "vcvt", "vpush", "vpop"))]
    if bad:
        print("ERROR: 目标 ELF 含 VFP 指令（ARM926 无 FPU，必复位）")
        for l in bad[:5]:
            print("  ", l.strip())
        return False
    return True


def main():
    global path
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    relocs_only = "--relocs" in sys.argv
    quiet = "--quiet" in sys.argv
    if not args:
        sys.exit(__doc__)
    path = args[0]

    with open(path, "rb") as f:
        info = load_sections(f)
        e_type, e_machine, e_entry = (info["e_type"], info["e_machine"],
                                      info["e_entry"])
        failures = []

        def check(ok, msg):
            if not ok:
                failures.append(msg)
            if not quiet:
                print(("PASS " if ok else "FAIL ") + msg)

        check(e_machine == EM_ARM, f"e_machine == EM_ARM (40): got {e_machine}")
        check(e_type == ET_DYN, f"e_type == ET_DYN (3): got {e_type}")

        loads = [p for p in info["phdrs"] if p[0] == PT_LOAD]
        check(len(loads) >= 1, "at least one PT_LOAD")
        if loads:
            check(loads[0][2] == 0, f"first LOAD vaddr == 0: got {loads[0][2]:#x}")
            # entry 是段内偏移: 必须落在某 LOAD 的 [vaddr, vaddr+memsz)
            entry_ok = any(p[2] <= e_entry < p[2] + p[4] for p in loads)
            check(entry_ok, f"entry {e_entry:#x} lies inside a LOAD segment")

        # 符号表与重定位
        sym_undef = 0
        reloc_issues = 0
        reloc_total = 0
        for (name_off, s_type, s_addr, s_off, s_size) in info["sects"]:
            name = info["sec_name"](name_off)
            if name == ".dynsym" and s_size >= 16:
                f.seek(s_off)
                symtab = f.read(s_size)
                for i in range(s_size // 16):
                    st_name, st_value, st_size, st_info, st_other, st_shndx = \
                        struct.unpack_from("<IIIBBH", symtab, i * 16)
                    if st_shndx == SHN_UNDEF and (st_info >> 4) in (1, 2):
                        sym_undef += 1
            if name == ".rel.dyn" and s_size >= 8:
                f.seek(s_off)
                rel = f.read(s_size)
                reloc_total = s_size // 8
                for i in range(reloc_total):
                    _, info_word = struct.unpack_from("<II", rel, i * 8)
                    if info_word & 0xFF != R_ARM_RELATIVE:
                        reloc_issues += 1
                    if info_word >> 8 != 0:
                        reloc_issues += 1

        check(sym_undef == 0, f"no undefined global symbols: found {sym_undef}")
        if relocs_only or not quiet:
            print(f"INFO rel.dyn: {reloc_total} entries, "
                  f"{reloc_issues} non-R_ARM_RELATIVE/with-symbol")
        check(reloc_issues == 0, "rel.dyn all R_ARM_RELATIVE, no symbol refs")

    if failures:
        print(f"\nFAILED ({len(failures)}):")
        for msg in failures:
            print("  - " + msg)
        sys.exit(1)
    print("\nOK: ELF is loadable by the Prime shellcode loader.")
    sys.exit(0)


if __name__ == "__main__":
    main()
