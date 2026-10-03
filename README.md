# EndixOS

> An operating system built completely from scratch.  
> No Linux. No Unix. Not POSIX. Just a fully custom OS.

![Status](https://img.shields.io/badge/status-pre--alpha-orange)
![Version](https://img.shields.io/badge/version-0.1.0-blue)
![Arch](https://img.shields.io/badge/arch-x86__64-lightgrey)
![License](https://img.shields.io/badge/license-MIT-green)
![Not Linux](https://img.shields.io/badge/not-Linux-red)
![Not Unix](https://img.shields.io/badge/not-Unix-red)

**EndixOS** is an independent operating system for the **x86_64** architecture, created entirely from scratch.  
It is **not** a Linux distribution, **not** a Unix clone, and **not** POSIX-compliant unless explicitly stated.  
EndixOS has its own kernel, bootloader, drivers, file system, system call ABI, and userspace.

---

## Table of Contents

- [About](#about)
- [Philosophy](#philosophy)
- [Features](#features)
- [Screenshots](#screenshots)
- [Architecture](#architecture)
- [Repository Structure](#repository-structure)
- [Building](#building)
- [Running in QEMU](#running-in-qemu)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)
- [Contact](#contact)

---

## About

EndixOS is a hobby OS and research project. Its goal is to understand how an operating system works at the lowest level and to build a fully custom platform with a clear architecture.

EndixOS is **not based on Linux, Unix, or any existing kernel**.  
It does **not** use Linux system calls, Unix conventions, or POSIX interfaces by default.  
Every subsystem is designed and implemented from scratch.

The project is at an early stage. Many components may be unstable, incomplete, or under development. EndixOS is **not intended for daily use or production**.

Main goals:

- a fully custom kernel;
- clear and readable code;
- minimalism without losing capability;
- x86_64 support;
- gradual transition to userspace and applications;
- no dependency on Linux, Unix, or POSIX.

---

## Philosophy

- **Fully from scratch** — no borrowed kernel, no Linux/Unix compatibility layer by default.
- **Independent** — EndixOS is its own system, not a clone or a distribution.
- **Simplicity** — complex things should be explainable.
- **Practice** — every subsystem is written, run, and debugged.
- **Openness** — code, documentation, and mistakes are open to everyone.

---

## Features

### Implemented

- [x] Custom bootloader / GRUB / Limine — *choose one*
- [x] Text output to VGA / framebuffer
- [x] Interrupt handling
- [x] Timer
- [x] Keyboard
- [x] Basic memory and allocator
- [x] Command shell

### In Development

- [ ] Virtual memory
- [ ] Multitasking
- [ ] Custom system call ABI
- [ ] Disk drivers
- [ ] EndFS file system
- [ ] Networking
- [ ] Userspace and custom user library
- [ ] Graphical interface

> Mark only what actually exists in the project. A README should not promise more than the OS can do.

---

## Screenshots

![EndixOS boot](docs/screenshots/boot.png)

![EndixOS shell](docs/screenshots/shell.png)

> Add real screenshots to `docs/screenshots/`. If you do not have any yet, remove this section.

---

## Architecture

EndixOS consists of several main layers:

```text
+-----------------------------+
|        Userspace            |
|  shell, utilities, programs |
+-----------------------------+
|     Custom Syscall ABI      |
+-----------------------------+
|          Kernel             |
|  mm | sched | fs | drivers  |
+-----------------------------+
|        Bootloader           |
+-----------------------------+
|        Hardware             |
+-----------------------------+
