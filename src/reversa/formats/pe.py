"""PE/COFF loader (PE32 and PE32+; x86, x86-64, ARM64).

Implemented from the Microsoft "PE Format" specification. Parses headers,
sections, imports, exports, base relocations, x64 exception directory
(function starts) and COFF symbol tables emitted by MinGW.
"""
from __future__ import annotations

from reversa.core.model import Binary, Export, Import, Relocation, Section, Symbol
from reversa.formats.reader import ParseError, Reader

MACHINES = {0x14C: ("x86", 32), 0x8664: ("x86-64", 64), 0xAA64: ("arm64", 64), 0x1C0: ("arm", 32), 0x1C4: ("arm", 32)}
SCN_EXEC, SCN_READ, SCN_WRITE = 0x20000000, 0x40000000, 0x80000000
SCN_CODE = 0x20


def is_pe(data: bytes) -> bool:
    if data[:2] != b"MZ" or len(data) < 0x40:
        return False
    lfanew = int.from_bytes(data[0x3C:0x40], "little")
    return data[lfanew:lfanew + 4] == b"PE\0\0"


def load_pe(path: str, data: bytes) -> Binary:
    r = Reader(data)
    lfanew = r.u32(0x3C)
    if r.bytes(lfanew, 4) != b"PE\0\0":
        raise ParseError("chýba podpis PE", lfanew)
    machine, nsec, _ts, symptr, nsyms, opt_size, chars = r.unpack("HHIIIHH", lfanew + 4)
    arch, bits = MACHINES.get(machine, (None, 32))
    opt = lfanew + 24
    magic = r.u16(opt)
    if magic not in (0x10B, 0x20B):
        raise ParseError(f"neznámy typ voliteľnej hlavičky (0x{magic:x})", opt)
    pe32plus = magic == 0x20B
    if pe32plus:
        entry_rva = r.u32(opt + 16)
        image_base = r.u64(opt + 24)
        subsystem = r.u16(opt + 68)
        ndirs = r.u32(opt + 108)
        dirs_off = opt + 112
    else:
        entry_rva = r.u32(opt + 16)
        image_base = r.u32(opt + 28)
        subsystem = r.u16(opt + 68)
        ndirs = r.u32(opt + 92)
        dirs_off = opt + 96
    if arch is None:
        arch, bits = ("x86-64", 64) if pe32plus else ("x86", 32)
    b = Binary(path=path, data=data, format="PE", arch=arch, bits=bits, base=image_base,
               entry=(image_base + entry_rva) if entry_rva else None, os="Windows")
    if machine not in MACHINES:
        b.warn(f"Neznámy typ procesora PE (0x{machine:x}) – odhadujem {arch}.", lfanew + 4)
    b.meta["subsystem"] = {1: "native", 2: "GUI", 3: "konzola", 10: "EFI"}.get(subsystem, str(subsystem))
    b.meta["dll"] = bool(chars & 0x2000)

    dirs = []
    for i in range(min(ndirs, 16)):
        try:
            dirs.append(r.unpack("II", dirs_off + i * 8))
        except ParseError:
            dirs.append((0, 0))
    while len(dirs) < 16:
        dirs.append((0, 0))

    sec_off = opt + opt_size
    strtab_off = symptr + nsyms * 18 if symptr else None
    for i in range(min(nsec, 96)):
        try:
            raw_name, vsize, va, rawsize, rawptr = r.unpack("8sIIII", sec_off + i * 40)
            flags = r.u32(sec_off + i * 40 + 36)
        except ParseError as e:
            b.warn("Tabuľka sekcií PE je orezaná.", e.offset, str(e))
            break
        name = raw_name.rstrip(b"\0").decode("latin-1")
        if name.startswith("/") and strtab_off is not None:  # long name in COFF string table
            try:
                name = r.cstr(strtab_off + int(name[1:]))
            except (ValueError, ParseError):
                pass
        size = vsize or rawsize
        fsize = min(rawsize, size)
        if rawptr + fsize > len(data):
            b.warn(f"Sekcia {name} presahuje koniec súboru – zvyšok bude prázdny.", rawptr)
            fsize = max(0, len(data) - rawptr)
        b.sections.append(Section(name, image_base + va, size, rawptr, fsize, bool(flags & SCN_READ),
                                  bool(flags & SCN_WRITE), bool(flags & (SCN_EXEC | SCN_CODE))))

    def rva_off(rva: int) -> int:
        off = b.addr_to_offset(image_base + rva)
        if off is None:
            raise ParseError(f"RVA 0x{rva:x} neukazuje do súboru", rva)
        return off

    for label, fn in (("Importy", _imports), ("Exporty", _exports), ("Relokácie", _relocs), ("Tabuľka výnimiek", _pdata)):
        try:
            fn(b, r, dirs, rva_off, pe32plus)
        except ParseError as e:
            b.warn(f"{label}: dáta sú poškodené – načítal som, čo sa dalo.", e.offset, str(e))
    if symptr and nsyms:
        try:
            _coff_symbols(b, r, symptr, nsyms)
        except ParseError as e:
            b.warn("Tabuľka COFF symbolov je poškodená.", e.offset, str(e))
    _detect_compiler(b, r, lfanew)
    return b


def _imports(b: Binary, r: Reader, dirs, rva_off, pe32plus: bool) -> None:
    rva, size = dirs[1]
    if not rva:
        return
    o = rva_off(rva)
    ptr = 8 if pe32plus else 4
    flag = 1 << 63 if pe32plus else 1 << 31
    for _ in range(4096):
        ilt, _ts, _fwd, name_rva, iat = r.unpack("IIIII", o)
        if not (ilt or name_rva or iat):
            break
        lib = r.cstr(rva_off(name_rva)) if name_rva else "?"
        b.libraries.append(lib)
        lookup = ilt or iat
        k = 0
        while k < 65536:
            entry = r.unpack("Q" if pe32plus else "I", rva_off(lookup) + k * ptr)[0]
            if entry == 0:
                break
            slot = b.base + iat + k * ptr
            if entry & flag:
                ordn = entry & 0xFFFF
                b.imports.append(Import(f"{lib.split('.')[0]}_ord{ordn}", lib, slot, None, ordn))
            else:
                name = r.cstr(rva_off(entry & 0x7FFFFFFF) + 2)
                b.imports.append(Import(name, lib, slot))
            k += 1
        o += 20


def _exports(b: Binary, r: Reader, dirs, rva_off, pe32plus: bool) -> None:
    rva, size = dirs[0]
    if not rva:
        return
    o = rva_off(rva)
    (_c, _t, _ma, _mi, _name, base, nfuncs, nnames, funcs_rva, names_rva, ords_rva) = r.unpack("IIHHIIIIIII", o)
    funcs = [r.u32(rva_off(funcs_rva) + 4 * i) for i in range(min(nfuncs, 65536))]
    named = {}
    for i in range(min(nnames, 65536)):
        nrva = r.u32(rva_off(names_rva) + 4 * i)
        ordi = r.u16(rva_off(ords_rva) + 2 * i)
        named[ordi] = r.cstr(rva_off(nrva))
    for i, frva in enumerate(funcs):
        if not frva:
            continue
        name = named.get(i, f"ord_{base + i}")
        fwd = ""
        if rva <= frva < rva + size:
            fwd = r.cstr(rva_off(frva))
        b.exports.append(Export(name, b.base + frva, base + i, fwd))
        if not fwd:
            b.symbols.append(Symbol(name, b.base + frva, 0, "func" if b.is_code(b.base + frva) else "object", "export"))


def _relocs(b: Binary, r: Reader, dirs, rva_off, pe32plus: bool) -> None:
    rva, size = dirs[5]
    if not rva:
        return
    o, end = rva_off(rva), rva_off(rva) + size
    types = {3: "HIGHLOW", 10: "DIR64", 0: "ABSOLUTE"}
    while o + 8 <= end and len(b.relocations) < 500000:
        page, bsize = r.unpack("II", o)
        if bsize < 8:
            break
        for k in range((bsize - 8) // 2):
            v = r.u16(o + 8 + 2 * k)
            t = v >> 12
            if t:
                b.relocations.append(Relocation(b.base + page + (v & 0xFFF), types.get(t, str(t))))
        o += bsize


def _pdata(b: Binary, r: Reader, dirs, rva_off, pe32plus: bool) -> None:
    rva, size = dirs[3]
    if not rva or b.arch != "x86-64":
        return
    o = rva_off(rva)
    for k in range(min(size // 12, 200000)):
        start, end, _unwind = r.unpack("III", o + 12 * k)
        if start and end > start:
            b.extra_function_starts.append(b.base + start)


def _coff_symbols(b: Binary, r: Reader, symptr: int, nsyms: int) -> None:
    strtab = symptr + nsyms * 18
    secs = [s for s in b.sections]
    i = 0
    seen = {s.addr for s in b.symbols}
    while i < min(nsyms, 500000):
        raw, value, secnum, typ, sclass, naux = r.unpack("8sIhHBB", symptr + i * 18)
        if raw[:4] == b"\0\0\0\0":
            name = r.cstr(strtab + int.from_bytes(raw[4:], "little"))
        else:
            name = raw.rstrip(b"\0").decode("latin-1")
        if 0 < secnum <= len(secs) and sclass in (2, 3) and name and not name.startswith("."):
            addr = secs[secnum - 1].addr + value
            is_func = (typ >> 4) == 2 or (typ == 0x20)
            if is_func and addr not in seen:
                b.symbols.append(Symbol(name, addr, 0, "func", "coff"))
                seen.add(addr)
        i += 1 + naux


def _detect_compiler(b: Binary, r: Reader, lfanew: int) -> None:
    if b"Rich" in b.data[0x40:lfanew]:
        b.compiler = "Microsoft Visual C++ (Rich hlavička)"
    elif any(s.name in (".CRT", ".tls") for s in b.sections) and (b"mingw" in b.data.lower() or b"GCC: (" in b.data):
        b.compiler = "MinGW-w64 GCC"
    elif b"Go build ID" in b.data:
        b.compiler = "Go"
    elif b"rust_panic" in b.data:
        b.compiler = "Rust (rustc)"
    if any(s.name.startswith("UPX") for s in b.sections):
        b.warn("Súbor je pravdepodobne skomprimovaný packerom UPX – kód bude čitateľný až po rozbalení.", None, severity="info")
