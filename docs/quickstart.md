# Quick start in 10 minutes

Goal: install REVERSA, open the sample binary and find the password in it, with no prior knowledge.

## 1. Install (2 minutes)

Download a package from GitHub Actions (open the latest successful run, then *Artifacts*):

- Windows: `reversa-windows-x64` (zip). Unpack it and run `reversa-windows-x64\bin\reversa-gui.exe`.
  For the commands below, open a command prompt in the `bin` folder and type `reversa.exe`.
- Linux (Ubuntu 24.04 and newer): `reversa-linux-x64-deb`, then `sudo apt install ./reversa_0.2.0_amd64.deb`.

Check the install with `reversa --version`. To build from source, see [building.md](building.md).

## 2. Sample analysis (1 minute)

```bash
reversa --demo
```

The REVERSA window opens and analyses the sample program in the background. The status bar shows
the progress, and the *Cancel* button stops the analysis. On the first start a short guide appears;
you can skip it. Without a graphical environment (server, CI) use
`reversa --demo --no-gui`: the results are printed to the terminal.

## 3. The three main panels

| Panel | What it shows |
|---|---|
| Functions (left) | All functions of the program. ★ = the main function `main`. Grey = imports from libraries. |
| Centre | Tabs *Listing* (instructions), *Pseudocode* (readable code), *Function graph*, *Call graph*, *Strings*, *Imports*… |
| Details (right) | What is at the selected address and who uses it (cross-references). Buttons Rename / Comment / Bookmark. |

## 4. Find the password (5 minutes)

1. In the left panel click **★ main** and switch to the **Pseudocode** tab (or press `2`).
   You see that `main` calls `check_user` and then `check_password`.
2. Click **check_password**. The pseudocode contains a call to `strcmp(…)`, which compares two strings.
3. Switch to **Listing** (`1`). You see constants that the program puts together in a variable: they
   are the bytes of the text `reversa` and `2026` (numbers in the pseudocode, `2026` on the *Strings* tab).
4. The password is `reversa2026`. Check: `samples/bin/login_x64.elf admin reversa2026`.
5. Rename `check_password` to `verify_password` (key `N`). The name changes at once in the list, in
   the pseudocode of `main` and in the x-refs. `Ctrl+Z` undoes the change, `Ctrl+Y` redoes it.
6. Save the project (`Ctrl+S`). The message shows the path; later you open it with
   `reversa gui path.rvproj`.

## 5. Your own file

Click **Open file** or run `reversa gui /path/to/program`. REVERSA reads ELF (x86-64, ARM64, x86,
ARM), PE 32/64-bit and raw data (`reversa gui firmware.bin --arch arm64 --base 0x8000000`).

## Keyboard shortcuts

| Key | Action |
|---|---|
| `Ctrl+O` / `Ctrl+S` | open a file or project / save the project |
| `Ctrl+F` | search for a function, string or address |
| `G` | go to an address or name |
| `1` `2` `3` `4` | listing, pseudocode, function graph, call graph |
| `N` | rename a function, an address or the selected variable in the pseudocode |
| `;` / `B` | comment / bookmark |
| `Ctrl+Z` / `Ctrl+Y` | undo / redo |
| `Alt+←` | back in the navigation history |
| `Ctrl+Shift+T` | light / dark theme |
| `F1` | getting started guide |

You can change every shortcut in *View → Settings*. There you also set the font size and the
language, and turn off the hints for beginners. The default language is English; restart REVERSA
after changing it.

## Next

- [tutorials.md](tutorials.md): "Find the main function", "Understand a loop", examples with solutions.
- [scripting-tutorial.md](scripting-tutorial.md): automation with C++ scripts.
- [glossary.md](glossary.md): glossary of terms.
