# EndixOS v8

Своя ОС с нуля: 32-бит x86, ядро на C и ассемблере, графический рабочий стол в духе macOS.

## Запуск (Arch / CachyOS)

```
sudo pacman -S --needed qemu-system-x86 nasm gcc make grub xorriso mtools
make run
```

`make run` собирает образ через GRUB и запускает QEMU. Мышь захватывается при наведении,
освободить её: **Ctrl+Alt+G**. Отладочный вывод ядра идёт в терминал (COM1).

Обычный `qemu -kernel` не подходит: он не умеет выставлять графический режим.

## Что есть

- GDT, IDT, PIC, таймер PIT (100 Гц), часы RTC
- PS/2 мышь и клавиатура, очередь событий
- Фреймбуфер с двойной буферизацией, альфа-смешивание, скруглённые прямоугольники
- Шрифты: Inter (интерфейс), DejaVu Sans Mono (терминал), латиница и кириллица
- Оконный менеджер: перетаскивание, фокус, закрытие, Dock, строка меню с часами
- Settings: Wallpapers (12 процедурных обоев-сцен с превью), Themes (светлая и тёмная тема, 6 акцентных цветов), About
- Файловая система в памяти (папки, файлы, переименование, перенос)
- Files: навигация, Back/Forward, превью текста, New Folder/New File, Rename (F2), Delete, двойной клик
- Snake: стрелки или WASD, пауза, рекорд, ускорение
- Terminal: ls cd pwd cat mkdir touch rm mv cp echo > файл, snake, files, cmatrix, wallpaper 1-12, theme и др.
- Файлы и настройки живут в памяти и пропадают после выключения (дискового драйвера пока нет)

## Структура

```
boot/     загрузчик (Multiboot) и заглушки прерываний
kernel/   ядро: GDT, IDT, события, CPUID
drivers/  serial, PIC, PIT, PS/2, RTC
gui/      fb (графика), font, wm (окна, Dock, терминал), settings, theme, wallpaper
tools/    genfont.py: генератор растрового шрифта (gui/font_data.c)
```

## Лицензии шрифтов

Inter: SIL Open Font License. DejaVu Sans Mono: Bitstream Vera / DejaVu license (свободная).

## Версия 8.0
Релиз 8.0 (без суффиксов alpha). Новое в последних сборках: значки в стиле macOS, Liquid Glass с настройками,
переделанная анимация сворачивания ("джинн": у каждой строки окна свой прогресс, нижние приходят в значок первыми),
терминал v2.

### История разработки: alpha 16
- Сворачивание окон «джинн» (в Dock и обратно), снимок окна рисуется построчно с воронкой и мягкими краями.
- Settings > Appearance: ползунок «Magnification size» (размер значка под курсором 56..104).

### История разработки: alpha 17
- Значок в Dock подпрыгивает три раза при запуске приложения.

### История разработки: alpha 18
- Плавные переходы: окна вырастают при открытии и уменьшаются при закрытии, разворот на весь экран анимирован,
  выпадающие меню и Control Center выезжают из-под меню-бара, Launchpad проявляется, увеличение Dock следует за курсором плавно.

### История разработки: alpha 19
- Settings > Appearance > Dock hiding: Off / Auto (прячется, выезжает у нижнего края) / Smart (прячется, когда окно заходит на Dock или развёрнуто на весь экран).

### История разработки: alpha 20
- Mission Control: F3, Ctrl+Вверх или Window > Mission Control. Окна плавно раскладываются сеткой миниатюр с подписями, клик выбирает окно, Esc или клик мимо закрывает.

### История разработки: alpha 21 — Terminal v2
- Цвета, прокрутка (PgUp/PgDn, 300 строк), редактор строки (←/→/Home/End/Del), история ↑/↓ (24), Tab-автодополнение.
- Пайпы `|` и перенаправление `>` / `>>`.
- Команды: neofetch, tree, find, grep, head, tail, wc, sort, cal, calc, ps, kill, open, df, history, cowsay, fortune, accent, dock, help и др.

### История разработки: alpha 22 — Новые значки
- Все значки (Dock, Launchpad) перерисованы в стиле macOS: `gui/icons.c`, 128x128, градиенты, блик, кромка, ровные края (поля расстояний, без float).
- Files (папка), Terminal (>_), Settings (шестерёнка), Snake (змейка из блоков и яблоко), About (белый клевер на плитке цвета акцента), Launchpad (цветная сетка). У глифов мягкая тень.
- Для мелких размеров используется уменьшенная копия 64x64.
- Значки боковых панелей Files (дом, монитор, документ, загрузки, фото, нота, диск) и плитки Settings (Wallpaper, Appearance) тоже в новом стиле; `icon_draw(id, x, y, size)` в icons.c - единый кэш с уменьшенными копиями 64 и 32.
- Значок Control Center в строке меню перерисован как в macOS (два контурных тумблера, сглаживание).
- Liquid Glass v2 (`fb_glass`): преломление у краёв плавающих панелей (Dock, Control Center, меню, подсказки), насыщенный размытый фон, светлая кромка и внутренний блик сверху, тёмная линия у нижней кромки, мягкая тень. Отключается "Reduce transparency".
- Settings -> Liquid Glass: живой образец и 5 ползунков (Refraction, Blur, Frost, Edge light, Vibrancy) + Reset. Параметры: `fb_glass_param_set/get/reset`.

- Рабочий стол: значки из ~/Desktop (выбор, двойной клик открывает Files на нужном месте) и контекстное меню по правой кнопке (New Folder, Change Wallpaper, Appearance, Liquid Glass) с плавным появлением. Модуль gui/desktop.c.
- Виджеты (gui/widgets.c): часы (сглаженные стрелки), календарь месяца, сведения о системе; плавное появление, скрываются через правый клик по столу (Hide/Show Widgets).
- Spotlight (Ctrl+Пробел или лупа в строке меню): приложения, файлы, калькулятор; уведомления-баннеры (команда notify в терминале).
- Редактор виджетов: правый клик по виджету (Remove Widget / Edit Widgets...) или по столу (Edit Widgets...). В режиме редактирования на виджетах кнопки «минус», справа галерея с «плюсом», Done или Esc выходят. Виджеты: Clock, Calendar, System, Date.
- Сохранение настроек: драйвер ATA PIO (`drivers/ata.c`) и `gui/prefs.c`. `make run` подключает виртуальный диск `disk.img` (создаётся сам, `make clean` его не трогает). Сохраняются обои, тема, акцент, Dock, 24-часовое время, прозрачность, Liquid Glass и виджеты; запись через ~1 с после последнего изменения. Пишется только на диск QEMU и только в пустой или свой сектор 0, на настоящем железе ничего не записывается.

### Сеть (этап 1)

Драйвер Intel e1000 (стандартная сетевая карта QEMU, `drivers/e1000.c`, `drivers/pci.c`) и собственный стек в `kernel/net.c`: Ethernet, ARP, IPv4, ICMP, UDP, DHCP-клиент и DNS. Адрес получается автоматически при загрузке (в QEMU: 10.0.2.15, шлюз 10.0.2.2, DNS 10.0.2.3).

Команды терминала: `ifconfig` (адрес, маска, шлюз, DNS), `ping [-c N] host` (Ctrl+C останавливает), `nslookup host`. `make run` запускает QEMU с `-nic user,model=e1000`.

### Сеть (этап 2): TCP и HTTP

В `kernel/net.c` добавлен TCP-клиент (рукопожатие, повтор потерянных сегментов, приём по порядку, закрытие) и простой HTTP/1.0. Команда `curl [-i] http://host[:port]/path` (также `wget`) печатает страницу в терминал, `-i` показывает заголовки, Ctrl+C прерывает. Вывод ограничен 8 КБ. HTTPS пока не поддерживается (нужен TLS).

### Браузер

Новое приложение Browser (значок-компас в Dock, `open browser` или `browser адрес` в терминале). Адресная строка (Ctrl+L), назад/вперёд/обновить, плавная прокрутка (колесо мыши, стрелки, PgUp/PgDn, Space), тонкая полоса прокрутки, индикатор загрузки, подсказка адреса ссылки. Слова без точки ищутся через FrogFind.

Движок (`gui/html.c`): разбор HTML и вёрстка без CSS и скриптов - заголовки, абзацы, списки, ссылки, жирный, `code`, `pre`, цитаты, таблицы строками текста, UTF-8 (латиница и кириллица). Загрузка (`net_fetch` в `kernel/net.c`): HTTP/1.0, редиректы, chunked. Встроенные страницы `endix://home` и `endix://about`. Колесо мыши: поддержка IntelliMouse в `drivers/ps2.c`.

Пока нет: HTTPS (нужен TLS), картинки, CSS, JavaScript, формы.

### HTTPS и поиск

Свой TLS 1.2 (`kernel/tls.c`, `kernel/crypto.c`): ECDHE на X25519, AES-128-GCM, SHA-256, всё написано с нуля, проверено на хосте против `openssl s_server` (RSA и ECDSA сертификаты, страницы до 94 КБ) и против эталонных реализаций (hashlib, cryptography). Работает во всех HTTP-запросах: браузер, `curl https://...`. Ограничения: сертификат сервера не проверяется (соединение зашифровано, но подлинность сервера не подтверждается), только TLS 1.2 и кривая X25519, случайные числа берутся из времени и движений мыши.

Адресная строка браузера: адрес открывается напрямую, любой другой текст ищется в DuckDuckGo Lite. Ссылки результатов открываются сразу на целевой сайт.

## Картинки в браузере

PNG (без чересстрочности, все глубины и палитры, прозрачность), JPEG (baseline, любая субдискретизация), GIF (первый кадр, чересстрочный тоже). Файл `gui/img.c`: inflate, декодеры и уменьшение усреднением, без malloc и float.
Страница показывается сразу, картинки докачиваются по одной (до 16 на страницу) и плавно проявляются. Не поддержаны: прогрессивный JPEG, WebP, SVG, анимация GIF. Для них вместо картинки показывается серая плашка с alt-текстом. Тест: проверено на хосте против PIL, в QEMU не запускалось.

## Таблицы

Таблицы раскладываются в настоящие колонки: ширины по содержимому, colspan, вложенные таблицы (до 4 уровней), узкие страницы сжимают колонки. Ячейки выравниваются по верху.

## Простой CSS

Цвет текста и фона (hex, rgb(), имена), font-weight, font-size (крупный текст), text-align:center, text-decoration, display:none, ширина таблиц, bgcolor и атрибуты font. Правила из <style> и style="" с простыми селекторами: тег, .класс, #id, тег.класс. Комбинаторы (.a b), псевдоклассы и @media пропускаются. Тёмный фон страницы даёт светлый текст.

## Запуск на ноутбуке с флешки (BIOS)

1. `make iso` (нужны grub, xorriso, mtools) даёт `build/endixos.iso`; он гибридный, его можно писать на флешку как есть.
2. `lsblk`, найди флешку (например `/dev/sdX`; ошибка в имени сотрёт другой диск!). Затем: `sudo dd if=build/endixos.iso of=/dev/sdX bs=4M status=progress oflag=sync`.
3. В ноутбуке выбери загрузку с USB (F12 / Esc / F9 в зависимости от модели), режим Legacy/CSM.
4. В меню GRUB выбери разрешение под экран (для нетбука 1024x600 или 1366x768; если экран маленький, а картинка обрезана - пробуй другой пункт). Меню ждёт 3 секунды, потом грузит 1024x768.
5. На диск ничего не ставится, система работает из оперативной памяти (нужно от 128 МБ). Настройки не сохраняются.

Что работает на железе: PS/2 клавиатура и тачпад, VESA-экран, часы. Сети нет (e1000 только в виртуальной машине), Wi-Fi и USB-устройства пока не поддержаны.

Нетбук DNS (BIOS отдаёт максимум 800x600x32): в GRUB выбирай пункт "800x600". Проверено: окна уменьшаются под экран.

Нетбук 1024x600 с BIOS 800x600: пункт GRUB "netbook 1024x600 via 800x600" передаёт ядру параметр wide. Интерфейс рисуется как 1024x600 и сжимается до 800 по ширине, панель растягивает обратно, пропорции правильные (резкость как у 800).

## Диски (AHCI)

Команда `disks` в терминале показывает SATA-диски и таблицу разделов (только чтение). Основа для установщика.

## Разметка и FAT32 (этап 2 установки)

`kernel/blk.h`, `kernel/mbr.c`, `kernel/fat32.c`: таблица разделов MBR, форматирование FAT32, каталоги, запись, замена и чтение файлов (имена 8.3). Проверено на образе диска независимым Python-разбором (`tools/tests/fattest.c`, `fatcheck.py`): цепочки, обе копии FAT, резервный загрузочный сектор, утечки кластеров. Файлы по 1,5 МБ, 100 файлов, замена файла.

## Установка на диск, этап 3 (загрузчик и установка)

`kernel/install.c`: `inst_run()` ставит систему на диск: MBR-раздел, FAT32, папки `/boot /boot/grub /system /home/<имя>`, ядро (его даёт GRUB вторым модулем, `module /boot/endixos.elf` в `grub.cfg`), меню загрузки, файл пользователей (`/system/users.dat`: имя, соль, хэш пароля SHA-256 в 5000 кругов, полное имя) и GRUB в MBR.
`make` сам собирает образы GRUB (`grub-mkimage`, нужен пакет grub) и вшивает их в ядро (`build/blob`). Без grub установщик сообщит, чего не хватает.
Проверено на образе диска с эталонным разбором на Python (раскладка загрузчика: boot.img, core.img с блоклистом, хэш пароля совпал с hashlib). Реальную загрузку с установленного диска проверяет только железо.

## Graphical installer (stage 4)
Open Terminal and run `install`. Pages: Welcome → Disk → Account → Confirm (switch "I understand, erase the disk") → Progress → Done (Restart).
Boot EndixOS from the USB stick via GRUB (needs the `module` line), BIOS SATA mode = AHCI. The installed grub.cfg is generated from the current screen mode (+ 800x600 and 1024x768 fallbacks).
Host-tested on a RAM disk (tools/tests/ittest.c); writing to a real disk and booting from it are NOT yet tested on hardware.

## Login screen (stage 5)
The installed grub.cfg passes `installed` to the kernel. On such a boot the kernel reads `/system/users.dat` from the SATA disk and shows a macOS-style login screen (blurred wallpaper, date/time, user, password field, Restart / Shut Down). Wrong password shakes the field; the right one fades into the desktop. Live USB boots (no `installed`) go straight to the desktop and never touch the disk. Terminal: `lock` shows the screen again, `whoami`/`hostname` use the installed names.
Host-tested (tools/tests/lgtest.c) on the RAM-disk image made by the installer test; not yet tried on real hardware.

## Moving widgets
Desktop → right click → Edit Widgets (or Esc to leave). In edit mode drag any widget: it lifts with a deeper shadow, snaps to a 6 px grid (and to the left column), and settles with an animation. Dropping on another widget, on the widget gallery or under the Dock puts it back. Places are saved with the other settings. Test: tools/tests/wdtest.c.

## Where is the installer?
Apple menu → "Install EndixOS...", or Spotlight ("install"), or Terminal `install`. On a live USB boot a notification points to it. The USB `grub.cfg` must have the `module /boot/endixos.elf endixos.elf` line (the one in this archive has it), otherwise the installer says the kernel image is not available.

Win+L locks the screen (needs an installed user; on a live USB it only shows a notice).

## Desktop icons
The desktop has an "Install EndixOS" icon (double click opens the installer; it disappears on an installed system). Right click an icon → Open / Delete, or select it and press Delete. The sample text file is gone. Test: tools/tests/dk2.c.

## Wired network on the netbook: JMicron JMC250/260 (stage 6)
`drivers/jme.c` — polled driver written from the Linux `jme.c` register layout and init order: EEPROM reload, MAC from RXUMA, MAC/PHY reset, autonegotiation, link polling every 0.5 s (cable can be plugged in later), RX ring of 16 buffers, TX ring (2 descriptors per frame). Terminal: `nicinfo` prints BAR, chip revision, PHY/link registers and counters; `ifconfig` shows link and frame counts, DHCP starts automatically when the link is up.
Host test with fake registers: tools/tests/jmetest.c. NOT tested on real hardware. Known gap: Linux also sets a bit in PCI config register 0xE4 for newer chip revisions (PHY bandgap); it is not done here, so if `nicinfo` shows link down with a cable plugged in, send the `nicinfo` output.

## Trying the installer in QEMU
`make run-install` boots the ISO with an empty 1 GB SATA (AHCI) disk `target.img`; install from Apple menu → Install EndixOS... (or the desktop icon). Then `make run-installed` boots from `target.img` alone: GRUB from the disk, then the login screen. Needs grub (for the boot blobs, `grub-mkimage`) on the build machine. Delete `target.img` to start over. Not run in the build sandbox (no QEMU there).

## Control Center (Liquid Glass, with editor)
Click the toggle icon in the menu bar. Tiles sit on a 4-column glass grid: Wi-Fi, Focus, Bluetooth, AirDrop, Dark Mode, Accent Color, Mission Control, Spotlight, Settings, Display and Sound sliders; more in the gallery: Now Playing, Terminal, Files, Lock Screen, Transparency. Dark Mode, Accent, Transparency, Display (really dims the screen) and the quick buttons work; Wi-Fi, Bluetooth, AirDrop, Focus and Now Playing are states only (no drivers).
"Edit Controls": minus removes a tile, drag a tile onto another to reorder, "Add a Control" opens the gallery. Esc leaves the editor. Layout is saved with the other settings. Test: tools/tests/ccx.c.

## Trash
Delete in Files and Delete / "Delete" in the desktop icon menu move items to `~/.Trash` (same-name items get "name 2"). The Trash is the last Dock icon (empty / full); clicking it opens Files there. Deleting inside the Trash removes for good. File → Empty Trash clears it. Terminal `rm` still deletes permanently. `.Trash` is hidden in Files. Test: tools/tests/trtest.c.
- Window corners 14 px, green button arrows (Tahoe-style window polish).
- Files: icons/list view toggle (top right of toolbar), Trash in sidebar, Finder-style list rows. Test: tools/tests/fltest.c.
- Settings: Search field in sidebar, new Displays (brightness) and Sound (volume) pages.
- Notifications: stack of up to 3 glass banners, hover keeps them, click dismisses; Empty Trash notifies. Test: tools/tests/nttest.c.
- Crossfade (~0.45 s) when switching Dark/Light, wallpaper or accent. Test: tools/tests/xftest.c.
- Dock label fades/slides in; dragged window gets a deeper shadow. Test: tools/tests/antest2.c.
- Mouse wheel scrolls Files and the Terminal; right-click on a Dock icon opens a glass menu (Open / Minimize / Quit; Trash: Empty Trash). Tests: dmtest.c, whtest.c.

## Sound (Intel HD Audio)
drivers/hda.c: polled HD Audio driver (CORB/RIRB, codec widget graph, speaker/headphone/line-out paths unmuted, 48 kHz 16-bit stereo DMA). Plays a startup chime and a "pop" when the volume changes; the Control Center / Settings volume slider sets the codec amplifier. Terminal: `audio` (status), `audio test` (play the chime). QEMU run targets now add `-device intel-hda -device hda-duplex`. Host test with a fake controller+codec: tools/tests/hdatest.c. NOT verified on real hardware (the codec's pin configuration varies; if nothing is heard run `audio` and tell what it prints).

## Your own photo wallpapers
Put JPEG/PNG files into the folder `Pictures` in the root of the SATA disk's FAT32 partition (the one the installer creates; mount it on another computer to copy files; long names are fine, up to 8 files, 3 MB each). On boot EndixOS scans it (drivers: AHCI only, no USB). Settings → Photos shows thumbnails; click one to set it. Pictures are scaled to fill the screen and cropped at the center. JPEG must be baseline (not progressive), up to 4096 px wide; save from a photo editor with "progressive" off. Test: tools/tests/phtest.c (fake FAT32 disk with a long file name).

## Встроенные 3D-обои
- `tools/render_wallpapers.py` (numpy) рендерит 5 сцен (Sahara Dusk, Ocean Sunset, Misty Forest 3D, Blue Ridge, Desert Night) в `assets/wall/*.jpg` и пакует в `assets/wall.pak` (~650 КБ).
- Пак вшивается в ядро (`objcopy`, `$(BLOB)/wall_pak.o`), `gui/scenery.c` декодирует JPEG тем же декодером (cover-crop, `photos_paint_mem`). Это готовые картинки, не живой 3D.
- Settings → Wallpaper показывает только эти сцены (миниатюры грузятся по одной за кадр); id = `WALL_COUNT + k`, фото пользователя теперь с `WALL_PHOTO0`. Тест: `tools/tests/sctest.c`.

## Разрешение
- Обои теперь рендерятся в 1920x1200 (пак ~1.3 МБ), кадр под экран режется по центру (cover).
- Settings → Displays → Resolution: режим на следующий запуск. GRUB задаёт режим до старта ядра, поэтому «на лету» его не сменить: `gui/resolution.c` переписывает `/boot/grub/grub.cfg` установленной системы (выбранный режим первым + запасные 1024x768 и 800x600). Работает только при запуске «installed»; если grub.cfg на диске нет, ничего не пишется. Тест: `tools/tests/rstest.c`.
