"""ELF loader (32/64-bit, little/big endian; x86, x86-64, ARM, AArch64).

Written from the ELF specification (System V ABI, "Tool Interface Standard
ELF v1.2") rather than wrapping a library so every sub-structure can fail
independently with a precise diagnostic (graceful degradation).
"""
from __future__ import annotations

from reversa.core.model import Binary, Export, Import, Relocation, Section, Symbol
from reversa.formats.reader import ParseError, Reader

MACHINES = {3: ("x86", 32), 62: ("x86-64", 64), 40: ("arm", 32), 183: ("arm64", 64)}
SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_DYNAMIC, SHT_NOBITS, SHT_REL, SHT_DYNSYM = 2, 3, 4, 6, 8, 9, 11
SHF_WRITE, SHF_ALLOC, SHF_EXEC = 1, 2, 4
PT_LOAD, PT_DYNAMIC = 1, 2
DT_NEEDED, DT_STRTAB = 1, 5
RELOC_NAMES = {
    "x86-64": {1: "R_X86_64_64", 6: "R_X86_64_GLOB_DAT", 7: "R_X86_64_JUMP_SLOT", 8: "R_X86_64_RELATIVE"},
    "arm64": {257: "R_AARCH64_ABS64", 1025: "R_AARCH64_GLOB_DAT", 1026: "R_AARCH64_JUMP_SLOT", 1027: "R_AARCH64_RELATIVE"},
    "x86": {1: "R_386_32", 6: "R_386_GLOB_DAT", 7: "R_386_JMP_SLOT", 8: "R_386_RELATIVE"},
    "arm": {2: "R_ARM_ABS32", 21: "R_ARM_GLOB_DAT", 22: "R_ARM_JUMP_SLOT", 23: "R_ARM_RELATIVE"},
}


def is_elf(data: bytes) -> bool:
    return data[:4] == b"\x7fELF"


def load_elf(path: str, data: bytes) -> Binary:
    if len(data) < 0x34:
        raise ParseError("ELF hlavička je neúplná", 0)
    cls, enc = data[4], data[5]
    if cls not in (1, 2):
        raise ParseError(f"neznáma trieda ELF ({cls})", 4)
    is64 = cls == 2
    endian = "big" if enc == 2 else "little"
    r = Reader(data, ">" if endian == "big" else "<")
    if is64:
        (e_type, e_machine, _v, e_entry, e_phoff, e_shoff, _f, _eh, e_phentsize, e_phnum,
         e_shentsize, e_shnum, e_shstrndx) = r.unpack("HHIQQQIHHHHHH", 16)
    else:
        (e_type, e_machine, _v, e_entry, e_phoff, e_shoff, _f, _eh, e_phentsize, e_phnum,
         e_shentsize, e_shnum, e_shstrndx) = r.unpack("HHIIIIIHHHHHH", 16)
    arch, bits = MACHINES.get(e_machine, (None, 64 if is64 else 32))
    b = Binary(path=path, data=data, format="ELF", arch=arch or "x86-64", bits=bits, endian=endian,
               entry=e_entry or None, os="Linux/Unix")
    b.meta["elf_type"] = {1: "REL (objekt)", 2: "EXEC (spustiteľný)", 3: "DYN (PIE/knižnica)", 4: "CORE"}.get(e_type, str(e_type))
    if arch is None:
        b.warn(f"Architektúra ELF (e_machine={e_machine}) nie je podporovaná – disassembler použije x86-64 ako odhad.", 18)

    # --- program headers (segments) ---------------------------------
    segments = []
    try:
        for i in range(min(e_phnum, 512)):
            o = e_phoff + i * e_phentsize
            if is64:
                p_type, p_flags, p_off, p_vaddr, _pa, p_filesz, p_memsz, _al = r.unpack("IIQQQQQQ", o)
            else:
                p_type, p_off, p_vaddr, _pa, p_filesz, p_memsz, p_flags, _al = r.unpack("IIIIIIII", o)
            segments.append((p_type, p_flags, p_off, p_vaddr, p_filesz, p_memsz))
    except ParseError as e:
        b.warn("Tabuľka segmentov (program headers) je poškodená – načítal som, čo sa dalo.", e.offset, str(e))
    loads = [s for s in segments if s[0] == PT_LOAD]
    if loads:
        b.base = min(s[3] for s in loads) & ~0xFFF

    # --- section headers ---------------------------------------------
    shdrs = []
    try:
        if e_shoff:
            for i in range(min(e_shnum, 4096)):
                o = e_shoff + i * e_shentsize
                if is64:
                    shdrs.append(r.unpack("IIQQQQIIQQ", o))
                else:
                    shdrs.append(r.unpack("IIIIIIIIII", o))
    except ParseError as e:
        b.warn("Tabuľka sekcií je poškodená alebo orezaná – použijem segmenty.", e.offset, str(e))
    names_off = None
    if 0 <= e_shstrndx < len(shdrs):
        names_off = shdrs[e_shstrndx][4]

    def sec_name(idx: int) -> str:
        if names_off is None:
            return f"sec{idx}"
        try:
            return r.cstr(names_off + shdrs[idx][0]) or f"sec{idx}"
        except ParseError:
            return f"sec{idx}"

    for i, sh in enumerate(shdrs):
        name_i, typ, flags, addr, off, size = sh[:6]
        if not (flags & SHF_ALLOC) or addr == 0:
            continue
        fsize = 0 if typ == SHT_NOBITS else size
        if fsize and off + fsize > len(data):
            b.warn(f"Sekcia {sec_name(i)} presahuje koniec súboru – zvyšok bude prázdny.", off)
            fsize = max(0, len(data) - off)
        b.sections.append(Section(sec_name(i), addr, size, off, fsize, True, bool(flags & SHF_WRITE), bool(flags & SHF_EXEC)))
    if not b.sections:  # stripped section table: fall back to segments
        for n, (p_type, p_flags, p_off, p_vaddr, p_filesz, p_memsz) in enumerate(loads):
            fsize = max(0, min(p_filesz, len(data) - p_off))
            b.sections.append(Section(f"segment{n}", p_vaddr, p_memsz, p_off, fsize,
                                      bool(p_flags & 4), bool(p_flags & 2), bool(p_flags & 1)))
        if loads:
            b.warn("Súbor nemá tabuľku sekcií – zobrazujem segmenty.", e_shoff or None, severity="info")
    b.meta["segments"] = [{"vaddr": s[3], "memsz": s[5], "offset": s[2], "filesz": s[4],
                           "perms": ("r" if s[1] & 4 else "-") + ("w" if s[1] & 2 else "-") + ("x" if s[1] & 1 else "-")}
                          for s in loads]

    # --- symbols -----------------------------------------------------
    dynsyms: list[tuple[str, int, int, int, int]] = []
    for i, sh in enumerate(shdrs):
        typ = sh[1]
        if typ not in (SHT_SYMTAB, SHT_DYNSYM):
            continue
        try:
            syms = _read_symbols(r, is64, sh, shdrs)
        except ParseError as e:
            b.warn(f"Tabuľka symbolov {sec_name(i)} je poškodená.", e.offset, str(e))
            continue
        if typ == SHT_DYNSYM:
            dynsyms = syms
        source = "symtab" if typ == SHT_SYMTAB else "dynsym"
        for name, value, size, info, shndx in syms:
            stype, bind = info & 0xF, info >> 4
            if not name or shndx == 0 or stype not in (1, 2) or value == 0:
                continue
            b.symbols.append(Symbol(name, value, size, "func" if stype == 2 else "object", source))
            if typ == SHT_DYNSYM and bind in (1, 2) and stype == 2:
                b.exports.append(Export(name, value))

    # --- dynamic: needed libraries -----------------------------------
    for sh in shdrs:
        if sh[1] != SHT_DYNAMIC:
            continue
        try:
            strtab_off = shdrs[sh[6]][4] if sh[6] < len(shdrs) else None
            ent = 16 if is64 else 8
            for k in range(min(sh[5] // ent, 4096)):
                tag, val = r.unpack("qQ" if is64 else "iI", sh[4] + k * ent)
                if tag == 0:
                    break
                if tag == DT_NEEDED and strtab_off is not None:
                    b.libraries.append(r.cstr(strtab_off + val))
        except ParseError as e:
            b.warn("Sekcia .dynamic je poškodená.", e.offset, str(e))

    # --- relocations -> imports --------------------------------------
    names = RELOC_NAMES.get(b.arch, {})
    seen_imports: dict[str, Import] = {}
    for i, sh in enumerate(shdrs):
        typ = sh[1]
        if typ not in (SHT_RELA, SHT_REL):
            continue
        try:
            link = sh[6]
            symlist = _read_symbols(r, is64, shdrs[link], shdrs) if link < len(shdrs) and shdrs[link][1] in (SHT_SYMTAB, SHT_DYNSYM) else dynsyms
            rela = typ == SHT_RELA
            ent = (24 if rela else 16) if is64 else (12 if rela else 8)
            for k in range(min(sh[5] // ent, 200000)):
                o = sh[4] + k * ent
                if is64:
                    vals = r.unpack("QQq" if rela else "QQ", o)
                    sym_i, rtype = vals[1] >> 32, vals[1] & 0xFFFFFFFF
                else:
                    vals = r.unpack("IIi" if rela else "II", o)
                    sym_i, rtype = vals[1] >> 8, vals[1] & 0xFF
                addend = vals[2] if rela else 0
                symname = symlist[sym_i][0] if 0 < sym_i < len(symlist) else ""
                tname = names.get(rtype, str(rtype))
                b.relocations.append(Relocation(vals[0], tname, symname, addend))
                if symname and ("JUMP_SLOT" in tname or "JMP_SLOT" in tname or "GLOB_DAT" in tname):
                    sym = symlist[sym_i]
                    if sym[4] == 0:  # undefined -> imported
                        imp = seen_imports.get(symname)
                        if imp is None or "JUMP" in tname or "JMP" in tname:
                            imp = Import(symname, "", vals[0])
                            seen_imports[symname] = imp
        except (ParseError, IndexError) as e:
            b.warn(f"Relokácie v {sec_name(i)} sú poškodené.", getattr(e, "offset", sh[4]), str(e))
    b.imports = list(seen_imports.values())
    _detect_compiler(b, r, shdrs, sec_name)
    return b


def _read_symbols(r: Reader, is64: bool, sh: tuple, shdrs: list) -> list[tuple[str, int, int, int, int]]:
    off, size, link = sh[4], sh[5], sh[6]
    strtab = shdrs[link][4] if link < len(shdrs) else None
    ent = 24 if is64 else 16
    out = []
    for k in range(min(size // ent, 500000)):
        o = off + k * ent
        if is64:
            st_name, st_info, _o, st_shndx, st_value, st_size = r.unpack("IBBHQQ", o)
        else:
            st_name, st_value, st_size, st_info, _o, st_shndx = r.unpack("IIIBBH", o)
        name = ""
        if strtab is not None and st_name:
            try:
                name = r.cstr(strtab + st_name)
            except ParseError:
                name = ""
        out.append((name, st_value, st_size, st_info, st_shndx))
    return out


def _detect_compiler(b: Binary, r: Reader, shdrs: list, sec_name) -> None:
    for i, sh in enumerate(shdrs):
        if sec_name(i) == ".comment":
            try:
                text = r.bytes(sh[4], min(sh[5], 512)).replace(b"\0", b" ").decode("utf-8", "replace").strip()
            except ParseError:
                continue
            if text:
                b.compiler = text
                return
        if sec_name(i) == ".note.go.buildid":
            b.compiler = "Go"
            return
    if b"rust_panic" in b.data or b"/rustc/" in b.data:
        b.compiler = "Rust (rustc)"
    elif b"GCC: (" in b.data:
        b.compiler = "GCC"
