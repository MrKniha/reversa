"""Detects well-known crypto constants in the binary (idea similar to the
FindCrypt plugins for IDA/Ghidra). Adds a bookmark for every hit and a view."""

SIGNATURES = {
    "AES S-box": "63 7c 77 7b f2 6b 6f c5 30 01 67 2b fe d7 ab 76",
    "SHA-256 K[0..3]": "98 2f 8a 42 91 44 37 71 cf fb c0 b5 a5 db b5 e9",
    "SHA-256 H0 (LE)": "67 e6 09 6a 85 ae 67 bb",
    "MD5 T[0..1] (LE)": "78 a4 6a d7 56 b7 c7 e8",
    "CRC32 table": "00 00 00 00 96 30 07 77 2c 61 0e ee",
}


class CryptoConstants:
    def __init__(self):
        self.hits = []

    def on_load(self, program):
        self.hits = []
        for name, pattern in SIGNATURES.items():
            for addr in program.search_bytes(pattern):
                self.hits.append((name, addr))

    def view(self, program):
        if not self.hits:
            return "Žiadne známe kryptografické konštanty sa nenašli."
        return "\n".join(f"0x{a:x}  {n}" for n, a in self.hits)


def register(api):
    plugin = CryptoConstants()
    api.add_view("Krypto konštanty", plugin.view)
    original = plugin.on_load

    def on_load(program):
        original(program)
        for name, addr in plugin.hits:
            if addr not in program.bookmarks():
                program.bookmark(addr, f"krypto: {name}")
    plugin.on_load = on_load
    return plugin
