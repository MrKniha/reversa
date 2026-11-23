# Tutorials

All tutorials use the sample binaries in `samples/bin/`. They are small programs that we compiled
ourselves from the sources in `samples/src/` (checksums are in `samples/bin/SHA256SUMS`). None of
them is malware.

If you have not run REVERSA yet, start with the [quick start](quickstart.md). Unknown terms are
explained in the [glossary](glossary.md).

## Tutorial 1: First analysis in 10 minutes

**Goal:** find the password and the flag in the `login` program without running it.

1. **Start the demo (1 min).**
   ```bash
   reversa --demo
   ```
   The REVERSA window opens with the file `login_x64.elf`. On the left is the list of functions,
   in the centre the listing (disassembly) and other tabs, on the right the details and x-refs.

2. **Look at the strings (2 min).** Click the **Strings** tab. You see texts like
   `Usage: login <user> <password>`, `Wrong password.` and `Access granted. Flag: ...`.
   Strings are often the quickest clue to what a program does.

3. **Who uses a string? (2 min).** Click `Wrong password.`. In the **Details** panel on the right
   you see the section *Who uses this (x-refs)*: the string is referenced from `main`. Click the
   link to jump there.

4. **Read the pseudocode of `main` (2 min).** You see roughly this:
   ```c
   if (check_user(*(int64_t *)(local_10 + 8)) != 0) {
       if (check_password(*(int64_t *)(local_10 + 16)) == 0) {
           puts("Wrong password.");
   ```
   `local_10` is `argv`, so `local_10 + 8` is `argv[1]` (the user name) and `local_10 + 16` is
   `argv[2]` (the password). Rename `local_10` to `argv` (key **N**).

5. **Who calls `strcmp`? (2 min).** On the **Imports** tab click `strcmp` and look at the
   **Details** panel. Two functions call it: `check_user` and `check_password`. Open
   `check_password`: the pseudocode shows that the password is built from `"reversa"` and `"2026"`.

6. **Rename and save (1 min).** Click a variable or a function and press **N**. The new name
   appears at once in the listing, the pseudocode, the graph and the function list. Save the
   project (**Ctrl+S**). When you open it again, the names are back.

**Solution:** user `admin` or `student`, password `reversa2026`, flag
`CTF{xrefs_are_your_friend}`.

## Tutorial 2: Find the main function

**Goal:** find `main` in a file without symbols (`login_x64_stripped.elf`).

A binary without symbols (stripped) has no function names. Only addresses remain, for example
`sub_401196`. How do you get to `main`?

1. Open the file:
   ```bash
   reversa gui samples/bin/login_x64_stripped.elf
   ```
2. Look at the **entry point** (the function `_start`). On Linux with glibc, `_start` calls
   `__libc_start_main` and passes it the address of `main` as the first argument. In the listing
   of `_start` you see something like:
   ```
   mov   rdi, 0x40125e        ; first argument = address of main
   call  qword ptr [__libc_start_main]
   ```
3. REVERSA recognises this pattern automatically and names the function at address `0x401298`
   `main` (marked with ★ in the function list). Check it: click the address in `mov rdi, ...` and
   you jump to the start of `main`.
4. The other functions are still `sub_...`. Open the pseudocode of `main`: it calls
   `sub_401217(argv[1])` and `sub_401196(argv[2])`. Based on the strings around them
   (`Unknown user.`, `Wrong password.`), rename them to `check_user` and `check_password`
   (key **N**).

**Question:** why should you not look for `main` just as "the first big function"? (Answer: the
order of functions in the file is chosen by the linker, not by the programmer. The data flow from
`_start` is more reliable.)

**Advanced:** in the pseudocode of `sub_401196` (stripped version) you see the numbers
`0x61737265766572` and `0x36323032`. They are strings written as numbers (little-endian). Read the
bytes backwards: `72 65 76 65 72 73 61` = `"reversa"`, `32 30 32 36` = `"2026"`. The compiler
optimised `strcpy`/`strcat` this way. The line `if (local_8 - __fs_28 == 0)` is a **stack canary**
check (protection against stack overflows). It is not part of the program logic.

## Tutorial 3: Understand a loop

**Goal:** recognise a `for` loop in assembly, in the graph and in the pseudocode.

1. Open `samples/bin/hello_x64.elf` and select the function `sum_to`.
2. **Graph (CFG):** switch the centre to **Function graph** (key **3**). You see blocks and one edge
   that goes **back up**. A back edge means a loop, and REVERSA draws it in another colour.
   The block it leads to is the loop header.
3. **Listing:** in the loop header you find a comparison and a conditional jump:
   ```
   cmp   eax, dword ptr [rbp - 0x14]
   jle   ...                         ; jump back into the body if i <= n
   ```
4. **Pseudocode:**
   ```c
   local_8 = 0;
   local_4 = 1;
   while (local_4 <= local_14) {
       local_8 += local_4;
       local_4++;
   }
   return local_8;
   ```
   Rename `local_4` to `i`, `local_8` to `total` and `local_14` to `n`. Now you can see that the
   function computes the sum 1 + 2 + … + n.

**Why `while` and not `for`?** In machine code there is no difference between `for` and `while`.
The decompiler always picks one form. `for (i = 1; i <= n; i++)` is the same as
`i = 1; while (i <= n) { …; i++; }`.

**Exercise:** find the loop in the function `greet`. What is the value of `greetings[i % 3]` when
`i = 4`? (Solution: `"Hello"`; you find the `greetings` table through the x-refs of the string
`Ahoj`.)

## Tutorial 4: Same program, different architecture

Open `samples/bin/login_arm64.elf` (AArch64) and `samples/bin/login_x86.exe` (32-bit Windows PE).
The pseudocode of `main` looks almost the same as on x86-64, even though the listing is completely
different. Notice:

- ARM64 loads string addresses with a pair of `adrp` + `add`. REVERSA joins them and shows the
  string directly in the listing and in the pseudocode.
- 32-bit Windows passes arguments on the stack (`push`), not in registers.
- Windows PE calls DLL functions through the import table (IAT), for example
  `call dword ptr [__imp__strcmp]`.

## Tutorial 5: Without the GUI (headless)

```bash
reversa analyze samples/bin/login_x64.elf --report report.md
reversa analyze samples/bin/login_x64.elf --pseudocode main
reversa search samples/bin/login_x64.elf --insn "call.*strcmp"
reversa script build/bin/examples/scripts/strcmp_callers.so samples/bin/login_x64.elf
```

The report `report.md` contains a file overview, sections, imports, functions, strings, the call
graph and the pseudocode of `main`. Continue with the [scripting tutorial](scripting-tutorial.md).
