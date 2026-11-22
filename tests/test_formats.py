import pytest
from conftest import ALL_SAMPLES, sample

from reversa.core.errors import LoadError
from reversa.formats.loader import detect_format, load_bytes, load_file
from reversa.formats.raw import guess_arch
from reversa.formats.reader import ParseError, Reader

EXPECTED = {
    "hello_x64.elf": ("ELF", "x86-64", 64, 0x401070),
    "hello_arm64.elf": ("ELF", "arm64", 64, 0x700),
    "hello_x64.exe": ("PE", "x86-64", 64, 0x140001410),
    "hello_x86.exe": ("PE", "x86", 32, 0x4013E0),
}


@pytest.mark.parametrize("name", sorted(EXPECTED))
def test_headers(name):
    b = load_file(sample(name))
    fmt, arch, bits, entry = EXPECTED[name]
    assert (b.format, b.arch, b.bits, b.entry) == (fmt, arch, bits, entry)
    assert b.endian == "little"
    assert any(s.executable for s in b.sections)
    assert not [d for d in b.diagnostics if d.severity == "error"]


def test_elf_details():
    b = load_file(sample("hello_x64.elf"))
    names = {s.name for s in b.sections}
    assert {".text", ".rodata", ".data", ".bss", ".plt"} <= names
    assert {i.name for i in b.imports} >= {"printf", "puts", "__libc_start_main"}
    assert "libc.so.6" in b.libraries
    assert any(s.name == "sum_to" and s.kind == "func" for s in b.symbols)
    assert b.compiler.startswith("GCC")
    assert any("JUMP_SLOT" in r.type for r in b.relocations)


def test_pe_details():
    b = load_file(sample("login_x64.exe"))
    assert {i.name for i in b.imports} >= {"strcmp", "puts"}
    assert any(lib.lower().endswith(".dll") for lib in b.libraries)
    assert b.extra_function_starts, ".pdata function starts expected for x64 PE"
    assert b.compiler.startswith("MinGW")
    assert b.meta["subsystem"] == "konzola"
    assert any(r.type == "DIR64" for r in b.relocations)
    sec = b.section_at(b.entry)
    assert sec is not None and sec.executable


def test_stripped_elf_has_no_symbols_but_imports():
    b = load_file(sample("login_x64_stripped.elf"))
    assert not [s for s in b.symbols if s.source == "symtab"]
    assert {i.name for i in b.imports} >= {"strcmp", "puts"}


@pytest.mark.parametrize("name", ALL_SAMPLES)
def test_all_samples_load(name):
    b = load_file(sample(name))
    assert b.format in ("ELF", "PE")
    assert b.read(b.entry, 4)


def test_address_mapping_roundtrip():
    b = load_file(sample("hello_x64.elf"))
    text = next(s for s in b.sections if s.name == ".text")
    off = b.addr_to_offset(text.addr + 5)
    assert off == text.offset + 5
    assert b.offset_to_addr(off) == text.addr + 5
    bss = next(s for s in b.sections if s.name == ".bss")
    assert b.read(bss.addr, 4) == b"\0\0\0\0"
    assert b.addr_to_offset(bss.addr) is None
    assert b.read(0x10, 4) == b""  # unmapped


def test_detect_and_raw():
    assert detect_format(b"\x7fELF" + b"\0" * 60) == "ELF"
    assert detect_format(b"\xcf\xfa\xed\xfe" + b"\0" * 60) == "MACHO"
    assert detect_format(b"PK\x03\x04") == "ARCHIVE"
    assert detect_format(b"\x90" * 100) == "RAW"
    code = bytes.fromhex("554889e5897dfc8b45fc5dc3") * 4
    assert guess_arch(code)[0] == "x86-64"
    arm = bytes.fromhex("fd7bbfa9fd030091fd7bc1a8c0035fd6") * 4
    assert guess_arch(arm)[0] == "arm64"
    raw = load_bytes(code, "shellcode.bin", base=0x1000)
    assert raw.format == "RAW" and raw.arch == "x86-64" and raw.entry == 0x1000
    assert any("odhadol" in d.message for d in raw.diagnostics)
    manual = load_bytes(code, "x.bin", arch="x86", base=0x8000)
    assert manual.arch == "x86" and manual.bits == 32 and not manual.diagnostics


def test_macho_and_zip_fall_back_to_raw():
    b = load_bytes(b"\xcf\xfa\xed\xfe" + b"\0" * 64, "a.macho")
    assert b.format == "RAW" and "Mach-O" in b.diagnostics[-1].message
    z = load_bytes(b"PK\x03\x04" + b"\0" * 64, "a.apk")
    assert "APK" in z.diagnostics[-1].message


def test_load_errors(tmp_path):
    with pytest.raises(LoadError):
        load_file(str(tmp_path / "missing.bin"))
    with pytest.raises(LoadError):
        load_file(str(tmp_path))
    empty = tmp_path / "empty.bin"
    empty.write_bytes(b"")
    with pytest.raises(LoadError, match="prázdny"):
        load_file(str(empty))


def test_reader_bounds():
    r = Reader(b"\x01\x02\x03")
    assert r.u16(0) == 0x0201
    with pytest.raises(ParseError):
        r.u32(0)
    with pytest.raises(ParseError):
        r.bytes(2, 5)
    with pytest.raises(ParseError):
        r.cstr(10)
    assert r.cstr(0) == "\x01\x02\x03"


def test_corrupted_elf_header_falls_back_to_raw():
    data = bytearray(open(sample("hello_x64.elf"), "rb").read())
    data[4] = 7  # invalid ELF class
    b = load_bytes(bytes(data), "bad.elf")
    assert b.format == "RAW"
    err = [d for d in b.diagnostics if d.severity == "error"]
    assert err and "poškodený" in err[0].message and err[0].detail


def test_truncated_pe_reports_but_loads():
    data = open(sample("login_x64.exe"), "rb").read()[:0x2000]
    b = load_bytes(data, "trunc.exe")
    assert b.format == "PE"
    assert any("koniec súboru" in d.message or "poškodené" in d.message for d in b.diagnostics)
