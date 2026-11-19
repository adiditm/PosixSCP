<p align="center">
  <img src="native/ui-qt/posixscp.png" width="128" alt="PosixSCP icon">
</p>

<h1 align="center">PosixSCP</h1>

<p align="center"><b>A native macOS &amp; Linux client for SFTP, SCP, and FTP — bringing WinSCP to POSIX.</b></p>

<p align="center">
  💻 <a href="https://github.com/adiditm/PosixSCP">GitHub Repository</a> ·
  ⬇️ <a href="https://github.com/adiditm/PosixSCP/releases">Releases</a>
</p>

<p align="center"><b>100% free · open source (GPLv3) · no telemetry</b></p>

## Overview

PosixSCP brings the beloved features of **WinSCP** to **macOS** and **Linux** with native performance, modern Qt interface, and portable mode configuration.

### Key Enhancements & Features

- **Multi-Protocol Support**: SFTP, SCP, FTP/FTPS, WebDAV, S3.
- **Dual-Pane Commander Layout**: Classic WinSCP Commander interface with top menu bar, function key bar (F2-F8), directory bookmarking, and file comparison.
- **Synchronized Directory Browsing**: Navigate local and remote directories in lockstep.
- **Advanced Connection Options**:
  - Configurable server response timeouts.
  - SSH keepalives (Null packets, dummy commands, custom interval).
  - Connection buffer size optimization.
  - Internet protocol version selection (Auto, IPv4, IPv6).
- **Session Manager & Workspaces**:
  - Resizable site tree with real-time site search / filter.
  - Full portable configuration stored in `PosixSCP.ini`.
  - Import existing WinSCP site configurations.
- **Cross-Platform**:
  - macOS (Apple Silicon & Intel x86_64).
  - Linux (x86_64 & ARM64 aarch64).

---

## Why

[WinSCP](https://winscp.net/) is the file manager a lot of people grew up with — SFTP/FTP/FTPS/SCP/S3/
WebDAV, a dual-pane Commander, a built-in editor, synchronized browsing. But it has only ever existed
on **Windows**. On macOS and Linux that gap has stayed open for ~20 years: you end up juggling
Cyberduck, FileZilla, `sftp`, sshfs… none of them *is* WinSCP.

**PosixSCP closes that gap** by running WinSCP's actual engine natively on macOS and Linux, behind a
faithful Qt rebuild of its interface.

## How it works (ported vs rewritten)

- **The protocol engine is WinSCP's own code, recompiled natively — not a reimplementation.**
  `source/core` (~76k lines of C++: the SFTP/SCP/FTP/WebDAV/S3 state machines, transfer logic,
  synchronization) is the upstream WinSCP engine. It's welded to Embarcadero C++Builder's RTL, so
  instead of rewriting it we wrote that RTL from scratch:
  - **`native/rtlcompat`** — a from-zero reimplementation of the Embarcadero RTL the engine needs
    (`UnicodeString`, `TStringList`, `__property`, `TStream`, threading, …), so the engine compiles
    unmodified with clang. Transfers therefore behave *identically* to WinSCP — same code path.
  - **`native/platform` + `native/putty`** — POSIX adapters (registry→INI, Win32→POSIX, BSD sockets,
    a PuTTY unix backend) so the SSH/network layer runs on Unix.
  - Vendored protocol libraries built natively: **PuTTY** (SSH), **neon** (WebDAV), **libs3** (S3),
    **FileZilla** (FTP), plus OpenSSL/expat.
- **The GUI is rewritten from scratch in Qt 6 Widgets** (`native/ui-qt`), closely mirroring WinSCP's
  Commander — it calls the engine directly (no IPC) across one small UTF-8 boundary
  (`enginebridge`).

The original Windows build tree (`source/`, `*.cbproj`, `build.bat`) is left untouched and still
builds on Windows; all port work lives under `native/`.

## Features

Dual-pane Commander · **SFTP · SCP · FTP / FTPS · WebDAV · S3** · public-key auth (OpenSSH keys
auto-converted) · host-key verification · drag &amp; drop (incl. from Finder) · background transfer
**queue** with parallel transfers · **edit remote files in any editor** (auto re-upload on save) ·
built-in editor · directory **tree** · **session tabs** · synchronized browsing · bookmarks ·
overwrite confirmation · recursive chmod · file masks · Open Terminal.

New in **0.3**:

- **Connect anywhere** — SSH **tunnel** through a jump host, **proxies** (SOCKS4, SOCKS5, HTTP),
  **two-factor / keyboard-interactive** login prompts, **FTPS** (explicit and implicit TLS), FTP
  active mode, UTF-8 file-name switch.
- **Transfer Settings** — binary / text / automatic mode, keep timestamps, set permissions, change
  name case, include/exclude **file masks**, newer-only, **speed limit**, and interrupted transfers
  **resume** where they stopped.
- **Find Files** (local and remote, WinSCP masks) · **Calculate Directory Sizes** ·
  **Compare Directories** · **Compare Files** side by side (local vs remote).
- **Synchronize** now decides by real timestamps: correct time zones, timestamps preserved on every
  protocol that can carry them.
- Sturdier: FTP time-outs and keep-alive, clean errors when a server dies or a directory cannot be
  read, no more idle CPU use while waiting on the network.

What is still missing compared with WinSCP is tracked in
[`docs/porting/ROADMAP.md`](docs/porting/ROADMAP.md).

## Build (macOS, Apple Silicon)

```sh
brew install qt cmake ninja nasm autoconf automake libtool pkg-config openssl
cd native
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
ctest --test-dir build        # rtlcompat + engine unit tests
```

(Linux follows the same shape — the platform layer is POSIX/`__APPLE__`/`__linux__`-clean; build the
vendored libs + Qt6 there. See `docs/porting/STATUS.md`.)

## Run

```sh
open native/build/ui-qt/winscp-qt.app          # macOS
# or: ./native/build/ui-qt/winscp-qt.app/Contents/MacOS/winscp-qt
```

Connect from the Login dialog (protocol, host, user, password or private key). Save sites for reuse;
open several at once with the **+** tab.

## Project layout

```
source/            upstream WinSCP source (engine = source/core). Unchanged; still builds on Windows.
native/rtlcompat   Embarcadero RTL reimplemented from scratch (lets the engine compile with clang)
native/platform    macOS/Linux platform adapters
native/putty       PuTTY unix backend (sockets/select/entropy)
native/neon|libs3|filezilla   native builds of the vendored protocol libs
native/ui-qt       the Qt 6 GUI + enginebridge (the UI↔engine boundary)
native/scripts     build, packaging and end-to-end test scripts (e2e/ = test servers)
docs/porting       PLAN / STATUS / ROADMAP / LEARNINGS / UPSTREAM-PATCHES
```

## Status & license

All five protocols work end-to-end (connect / list / transfer / file ops) and are exercised by the
suites in `native/scripts` (`e2e-ssh.sh`, `e2e-features.sh`, `e2e-adversarial.sh`) against real
servers; see `docs/porting/STATUS.md`. GPLv3, inherited from
WinSCP. PosixSCP is an independent port and is not affiliated with or endorsed by Martin Přikryl /
WinSCP.

---

<details>
<summary>Original WinSCP README (Windows build)</summary>

[WinSCP](https://winscp.net/) is a popular free file manager for Windows supporting SFTP, FTP, FTPS,
SCP, S3, WebDAV and local-to-local file transfers. To build the upstream Windows version you need
Embarcadero C++Builder 11 and Visual Studio 2022 build tools; see [`build.bat`](build.bat). The
upstream source lives under `source/` and is unmodified by the port (except small `#ifndef _WIN32`
guards documented in `docs/porting/UPSTREAM-PATCHES.md`).

</details>
