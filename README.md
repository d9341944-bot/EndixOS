# EndixOS 8.0

An operating system written from scratch: its own 32-bit x86 kernel in C and assembly, and a graphical desktop in the style of macOS (Liquid Glass). No Linux, no libc, no third-party libraries.

> This is an educational, experimental project. Target hardware: an old netbook (Atom, 2 GB RAM, BIOS/Legacy). Much of it has only been verified by host-side tests and in an emulator, see [Status](#status).

## Features

**System**
- Multiboot boot via GRUB, GDT/IDT/PIC, 100 Hz PIT timer, RTC
- PS/2 keyboard and mouse, event queue
- In-memory file system and on-disk FAT32 (read/write, long file names on read)
- Graphical installer: MBR partitioning, FAT32, GRUB bootloader, login screen with user accounts
- Trash, Spotlight-style search, notifications

**Hardware**
- AHCI (SATA), Intel HD Audio (sound), JMicron JMC250/260 and e1000 (network)
- Network stack with built-in TLS (HTTPS) and a simple web browser

**Desktop**
- Animated windows, Dock with context menu, menu bar, Mission Control, Control Center
- Files (icon and list views), Settings, Terminal, browser, Snake
- Transparent menu bar in the style of macOS Tahoe, light/dark themes, accent colors
- 5 built-in 3D wallpapers (pre-rendered, embedded in the kernel) and your own photos from `/Pictures` (JPEG)
- Screen resolution picker applied on next boot (Settings → Displays)

## Build and run

Arch / CachyOS:

```sh
sudo pacman -S --needed qemu-system-x86 nasm gcc make grub xorriso mtools
make run            # build the image and start it in QEMU
```

The mouse is captured on hover; release it with **Ctrl+Alt+G**. Building requires `gcc -m32` (or `i686-elf-gcc`).

### Bootable USB drive

```sh
lsblk                      # find your USB drive; a wrong name will erase another disk
make usb DEV=/dev/sdX      # builds the image and writes it to the drive (removable devices only)
```

Then boot from USB in Legacy/CSM mode. Pick a resolution for your screen in the GRUB menu. The system runs from RAM until you install it to disk with the built-in installer.

## Project layout

```
boot/      bootloader (Multiboot), interrupt stubs
kernel/    kernel: memory, FAT32, MBR, networking, TLS, crypto, installation
drivers/   PIT, PS/2, RTC, PCI, AHCI, HD Audio, e1000, JMicron
gui/       graphics, fonts, windows, apps, wallpapers, installer
assets/    pre-rendered 3D wallpapers (wall.pak)
tools/     font generator, wallpaper renderer, host-side tests
docs/      development notes by stage (NOTES.md)
```

## Tests

Host-side tests (`tools/tests/`) pull in the GUI and driver sources and check their logic: windows, Files, Settings, the installer, FAT32, the sound driver against a controller model, and more. They are not a substitute for testing on real hardware.

## Status

| Component | Verification |
|---|---|
| GUI, windows, animations, Files, Settings | host tests, screenshots |
| FAT32, installer, MBR | host tests, disk images |
| AHCI, HD Audio, JMicron, photo wallpapers, `grub.cfg` writing | **not verified on real hardware** |
| Wi-Fi, USB storage, eMMC/SD | not supported yet |

## License

Released under the [MIT License](LICENSE). Fonts: Inter (SIL Open Font License), DejaVu Sans Mono (Bitstream Vera / DejaVu license).
