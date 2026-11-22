# Glossary

We use these terms the same way in the GUI, the documentation and error messages. When you add a
text to the GUI, use a term from this glossary (and add it here if it is missing).

| Term | Explanation |
|---|---|
| **Binary** | A compiled program or library (for example `.exe`, ELF) that we analyse. |
| **File format** | How a binary is stored: ELF (Linux), PE (Windows), Mach-O (macOS, not supported yet), raw data. |
| **Raw data** | A file without a header (firmware dump, shellcode). The architecture and base address must be given, or REVERSA guesses them. |
| **Architecture** | The processor type: x86 (32-bit), x86-64, ARM (32-bit), ARM64 (AArch64). |
| **Endianness** | Byte order in a number: little endian (lowest byte first) or big endian. |
| **Section** | A named part of the file, for example `.text` (code), `.data` (data), `.rodata` (constants), `.bss` (zeroed data). |
| **Segment** | A part of the file that the loader maps into memory; sections live inside segments. |
| **Permissions r/w/x** | r = read, w = write, x = execute code. |
| **Address** | A location in the program's memory after loading (virtual address), shown in hex, for example `0x401000`. |
| **File offset** | The position of a byte in the file on disk (differs from the address). |
| **RVA** | Relative Virtual Address: an address relative to the image base (PE). |
| **Image base** | The address the program is loaded at (for example `0x400000`). |
| **Entry point** | The first instruction the system runs. Usually it is not `main` but the library's startup code. |
| **Main function (main)** | The function where the "real" program starts. It is marked with ★ in the function list. |
| **Function** | A self-contained piece of code with one start that can be called. |
| **Import** | A function from another library (for example `printf` from libc) that the program calls. Grey in the list. |
| **Export** | A function or data that a binary offers to other programs (typical for libraries/DLLs). |
| **Thunk** | A small "trampoline" function that only jumps to an import (for example `strcmp: jmp [__imp_strcmp]`). |
| **PLT / GOT** | Procedure Linkage Table / Global Offset Table: ELF tables through which a program calls imports. |
| **IAT** | Import Address Table: the table of import addresses in PE. |
| **Instruction** | One processor command, for example `mov`, `call`, `jmp`. |
| **Listing** | Instructions one after another with their addresses and bytes (disassembly). |
| **Disassembler** | A tool that turns machine code bytes into readable instructions. |
| **Pseudocode** | An estimate of the original code in C style (decompiled code). In the REVERSA MVP it is **provisional**: types are only guessed. |
| **Decompiler** | A tool that creates pseudocode from instructions. |
| **Variable a1, a2…** | The 1st, 2nd … parameter of a function. |
| **local_X** | A local variable stored on the stack (X = offset in hex). |
| **vN** | A temporary variable created from a processor register. |
| **Basic block** | A sequence of instructions with no jumps in or out except at the start and the end. |
| **CFG / function graph** | Control flow graph: the blocks of one function and the jumps between them. |
| **Loop** | Code that repeats (in the graph: an orange frame and a dashed back edge). |
| **Call graph** | A graph that shows which function calls which. |
| **Cross-reference (x-ref)** | A reference from one place in the program to another: "called from", "read from", "written from", "address used in", "pointer in data". |
| **String** | Text stored in the binary (ASCII or UTF-16). |
| **Comment** | Your note on an address. Stored in the project. |
| **Bookmark** | A named place you want to come back to. |
| **Project** (`.rvproj`) | A file with a copy of the binary and all your annotations (names, comments, bookmarks). |
| **Annotation** | A common name for renames, comments and bookmarks. |
| **Analysis** | Automatic search for functions, strings and x-refs after a file is opened (runs in the background). |
| **Diagnostics** | Warnings from loading (for example a damaged part of the file). |
| **Plugin** | A C/C++ library (.so/.dll) that reacts to events (loading, function analysed, rename). |
| **Script** | A C/C++ library that `reversa script` runs and that uses the REVERSA API. |
| **View** | Custom text output from a script or plugin, shown in the GUI on the Views tab. |
| **Report** | A text summary of the analysis (Markdown or HTML). |
| **Headless** | Running without a graphical interface (CLI, CI). |
| **Hex dump** | Bytes shown in hex together with ASCII characters. |
