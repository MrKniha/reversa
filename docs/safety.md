# Safety and ethics

How to analyse suspicious files safely, and what REVERSA is (and is not) for. To report a
vulnerability, see [SECURITY.md](../SECURITY.md).

## Safe malware analysis

REVERSA only does static analysis: it reads a file but never runs it. Still, follow these rules
when you work with suspicious files:

1. **Work in a virtual machine** (VirtualBox, VMware, QEMU/KVM), not on your everyday computer.
   Take a snapshot before the analysis.
2. **Disconnect the network** of the virtual machine or use an isolated (host-only) network.
   Malware may contact command-and-control servers.
3. **Never run the file** outside an isolated environment. Not even "just to try".
4. **Rename files** so they cannot be started by accident (for example `sample.exe_` or
   `sample.bin`). Protect archives with samples with a password (by convention `infected`).
5. **Do not share folders** between the host and the VM unless you need to. Turn off automatic
   opening of files and previews.
6. **Be careful with scripts and plugins.** They run without a sandbox. Do not use plugins from
   unknown sources.
7. **Check the hash** (SHA-256) of the sample before and after the analysis. REVERSA shows it in
   the report and in the tooltip of the GUI status bar, and stores it in the project.
8. **Damaged files are fine.** REVERSA loads what it can and reports problems in the diagnostics
   (*Advanced → Load diagnostics*). It does not crash.
9. **Follow the law and your organisation's rules.** Do not pass malware samples on outside
   trusted channels (for example a CSIRT or an antivirus vendor).

Never add malware or binaries without source code to this repository. The examples in
`samples/` are our own small programs with sources in `samples/src/`.

## Ethics

REVERSA is for learning, CTF competitions, malware analysis, security research,
interoperability and debugging your own software.

**REVERSA does not and will never contain features to bypass DRM, license checks, activation,
anti-cheat systems or copy protection.** We do not accept such contributions.

**Limits of the patch mode:** the future patch mode (roadmap, not in 0.2.0) will be meant for
changing **your own** programs or programs you have permission to change: bug fixes, teaching,
CTF tasks. It will not contain automatic "cracking", finding and removing license checks or
similar features. You are responsible for how you use the tool. Analyse only software you have
the right to analyse.
