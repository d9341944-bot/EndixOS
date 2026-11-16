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

## Software Update
- Settings → Software Update: проверка, загрузка и установка новых версий (English UI, как в macOS). Автопроверка раз за запуск, переключатель сохраняется в prefs.
- Источник: `https://raw.githubusercontent.com/d9341944-bot/EndixOS/main/endixos-v8/update/` (`latest.txt` + `endixos.elf`), адрес задаётся `UPDATE_BASE` в gui/update.c.
- Выпуск: увеличь `ENDIX_BUILD` в gui/settings.h, `make`, затем `python3 tools/mkupdate.py 8.1 "Пункт 1|Пункт 2"` и закоммить папку `update/` внутрь `endixos-v8/` в репозитории.
- Установка только на установленной системе: старое ядро сохраняется как `/boot/previous.elf` и появляется пунктом «EndixOS (previous version)» в GRUB; после записи ядро читается обратно и сверяется по SHA-256, при расхождении возвращается копия.
- TLS пока не проверяет сертификат сервера: SHA-256 из манифеста защищает от повреждений, но не от подмены манифеста на пути. Подпись выпусков - следующий шаг.
- Тест: `tools/tests/uptest.c`.

## Скорость (build 3)
- Профиль на хосте: больше всего кадра съедают fb_glass (размытие), fb_shadow (тени окон) и вывод на экран. На нетбуке с растяжкой 800→1024 вывод делает усреднение каждого пикселя: теперь это 4 точки с весами и упакованные R/B, результат совпадает с эталоном до 2/255 (`tools/tests/sctest2.c`).
- Режим скорости (Settings → Displays → Performance): Auto сам снижает качество, если кадр дольше 40 мс (6 подряд): уровень 1 - без теней и лёгкое размытие, уровень 2 - без размытия стекла. Назад сам не возвращается. High / Balanced / Fast - вручную. Хранится в prefs. Тест: `tools/tests/pftest.c`.
- Время кадра меряется rdtsc, такты на тик PIT калибруются при старте.
- Makefile теперь следит за заголовками (-MMD): смена `settings.h` пересобирает зависящие файлы.

## Wi-Fi RTL8188CE (build 4, этап 1)
- `drivers/rtlwifi.c`: поиск 10EC:8176/8177/8178/8191 на PCI, BAR2 (32-битная память), чтение SYS_CFG/CR/MAC, начало `_rtl92ce_init_mac` (выход из standby, CR=0x2ff). Регистры сверены с `rtlwifi/rtl8192ce` из ядра Linux (справка; код не копировался). Команды терминала: `wifi`, `wifi on`.
- Тест на модели регистров: `tools/tests/rwtest.c`. На железе не проверялось.
- Дальше: этап 2 - прошивка rtl8192cfw.bin (лицензия linux-firmware) и таблицы PHY/RF; этап 3 - сканирование; этап 4 - WPA2 (PBKDF2-SHA1, AES-CCMP), DHCP.

## Флешка
- `tools/mkusb.sh`: единственный способ делать загрузочную флешку. Только съёмные диски, подтверждение вводом имени, сборка, MBR + активный FAT32 + `grub-install --target=i386-pc`, сверка ядра `cmp`. `--refresh` - заменить только ядро, `--dry-run` - показать шаги. `make usb` / `make usb-refresh` вызывают его.
- Гибридный ISO (`make iso`, `make usb-iso`) оставлен как запасной: старый Phoenix BIOS на нетбуке его не принимал.

## Wi-Fi, этап 2 (прошивка)
- Файл `rtl8192cfw.bin` берётся из пакета linux-firmware (CachyOS: /usr/lib/firmware/rtlwifi/). В проект не входит (лицензия Realtek). `tools/mkusb.sh` сам кладёт его на флешку в /boot, GRUB отдаёт ядру как второй модуль (только для флешки; установленная система пока без прошивки).
- Команда `wifi fw`: питание -> таблица LLT -> загрузка прошивки по страницам -> ждём контрольную сумму и WINTINI_RDY. Коды: -2 файл, -3 LLT, -4 нет суммы, -5 не стартовала.
- Проверено только на модели регистров (tools/tests/rwtest.c). На железе не проверялось. Дальше: этап 3 (кольца DMA, таблицы PHY/RF, скан), этап 4 (WPA2, DHCP).

## Wi-Fi, этап 3a (MAC / baseband / радио)
- `drivers/rtl8192c_tables.h` - таблицы Realtek (GPL-2.0, получены скриптом из Linux rtlwifi). Отсюда файл не MIT.
- `wifi phy` = питание, LLT, прошивка, затем MAC-таблица, baseband+AGC, радио по 3-проводной шине (путь A, у 92C ещё B), включение CCK/OFDM.
- Не сделано: таблица PG (мощность передачи из efuse), калибровки (IQK, LC), выбор канала, кольца DMA, приём/передача, скан.

## Настройки -> Wi-Fi
- `gui/wifi.c`: состояния NONE/OFF/STARTING/ON/ERROR, переключатель запускает цепочку питание -> LLT -> прошивка -> таблицы (в wifi_step при перерисовке, не при загрузке). Список сетей пока пуст: сканирование - этап 3b; пароль и подключение - этап 4.

## Wi-Fi: полный путь (build 8)
- Цепочка: Настройки -> Wi-Fi -> переключатель -> `rtlwifi_start()` (питание, LLT, DMA, прошивка, таблицы, канал 1) -> `sta_scan()` -> список сетей -> пароль -> `sta_connect()` -> рукопожатие -> `net_init(mac, sta_send_eth)` -> DHCP.
- Модули: drivers/rtlwifi.c (кольца TX/RX, опрос без прерываний, смена канала), kernel/sta.c (802.11, WPA2-PSK, CCMP), kernel/crypto.c (SHA-1, PBKDF2, AES-CCM), gui/wifi.c (менеджер), gui/settings.c (страница). Опрос: wifi_tick() из главного цикла.
- Проверено на хосте: tools/tests/rwtest.c (регистры, дескрипторы), statest.c (протокол против модели точки), wpacrypto.c (сверка с Python). На железе не запускалось.
- Допущения: мощность передачи фиксирована (индекс 0x28, efuse не читается), калибровки IQK/LC не делаются, таблица PG не применяется, MAC берётся из чипа или создаётся случайный, TKIP/WEP/WPA3 не поддерживаются, CCMP написан по стандарту без внешнего вектора. Прошивка работает только с флешки.
- Если не работает: терминал `wifi phy` показывает код этапа; страница Wi-Fi пишет причину.

## Wi-Fi на установленной системе (build 9)
- Прошивка лежит на диске как `/boot/rtl8192c.bin` (имя 8.3: наш FAT32 не умеет длинные имена); GRUB отдаёт её ядру модулем с именем `rtl8192cfw.bin`.
- Новая установка копирует её сама, если установщик запущен с флешки с прошивкой. Уже установленная система: загрузиться с флешки (сделанной `tools/mkusb.sh`) и в терминале выполнить `wifi fwinstall` - файл копируется на диск, строка `module` добавляется во все пункты меню. Смена разрешения и обновления сохраняют её.

## Сохранение настроек (8.11)
- Установленная система: настройки лежат в `/boot/endix.cfg` (512 байт, блок prefs). Обновление трогает только `endixos.elf`/`previous.elf`, поэтому настройки переживают обновление и перезагрузку.
- Сохраняется: обои, тема, акцент, Dock, Liquid Glass, виджеты, Пункт управления, скорость, автообновление, Wi-Fi (включён ли, имя сети, пароль).
- Пароль Wi-Fi лежит в файле открытым текстом (шифрования ключом пользователя нет).
- Wi-Fi после загрузки сам включается и подключается к запомненной сети (одна попытка на поиск). «Отключить» вручную - сеть забывается.
- Виртуальный диск QEMU работает как раньше (сектор 0). Запуск с флешки: файла нет, настройки не сохраняются.
- Разрешение хранится в grub.cfg и так уже переживало перезагрузку.
- Тест: tools/tests/ptest.c.

## Приложения Calculator и Notes (8.2, build 22)
- Calculator (`gui/calc.c`): фиксированная точка (4 знака), свой `udiv` вместо 64-битного деления libgcc. Клавиатура и мышь. Тест: tools/tests/calctest.c.
- Notes (`gui/notes.c`): перенос по словам, автосохранение в ~/Notes.txt и на диск (/boot/notes.txt на установленной системе). Только ASCII.
- Терминал: команды `calc`, `notes`. Значки: ICON_APP_CALC, ICON_APP_NOTES (подчиняются стилю значков).

## Files: отдельные окна, правка, копирование (build 23)
- Двойной клик или Enter по файлу: картинка (png/jpg/jpeg/gif) -> окно Preview (gui/viewer.c, стрелки листают соседние картинки папки), текст -> окно TextEdit (редактор из gui/notes.c, общий движок с Notes; Ctrl+S и автосохранение через ~3 с; файл с нулевыми байтами или больше 16 КБ как текст не открывается).
- Окна TextEdit и Preview живут в wm.c после APP_COUNT (WIN_COUNT): в Dock и Launchpad не показываются, сворачиваются под значок Files.
- Files: Ctrl+C / X / V / D, правая кнопка - контекстное меню (Open, Rename, Duplicate, Copy, Cut, Paste, Move to Trash, New Folder/File), копирование папок рекурсивно.
- Ограничение: ramfs держит файл до 16 КБ, поэтому большие фото из /Pictures на диске в Files не видны.

## Clock и Calendar (build 24)
- Clock (gui/clockapp.c): секундомер с кругами и таймер (+/- часы, минуты, секунды, пресеты 1/5/10 мин); по окончании - уведомление. Пробел/Enter - старт/стоп, R - круг/сброс, Tab - вкладка.
- Calendar (gui/calapp.c): месяц, стрелки/кнопки листают, T или Today - к сегодняшнему дню. Сегодняшняя дата приходит из wm.c.
- Оба в Dock и Launchpad; терминал: `clock`, `calendar` (команды wm.c). Значки добавлены в стили Mono/Tinted.

## Control Center в стиле macOS 26 (build 25)
- Общий фон панели убран: плитки парят по отдельности (стекло с лёгким размытием и тенью). Фон остаётся только у галереи "Add a Control".

## Music (build 26)
- gui/music.c: плеер. Восемь встроенных мелодий (синтез: Für Elise, Canon in D, Greensleeves, Jingle Bells, Mountain King, Ode to Joy, Twinkle, гамма), WAV (PCM 8/16 бит, моно/стерео, любая частота) из ~/Music и с диска из /Music (тот же FAT32-раздел, что и /Pictures). Трек целиком декодируется в 48 кГц стерео (до 60 с) и отдаётся драйверу: hda_play_buf. Пробел - пауза, стрелки - трек, вверх/вниз - громкость, R - пересканировать.
- Звук НЕ проверен на железе (драйвер HDA писался без нетбука); без устройства плеер идёт молча и пишет об этом.

## Тачпад и EC (build 28)
- drivers/ps2.c: детект Synaptics (E8-последовательность, id 0x47), абсолютный режим 0xC5, 6-байтовые пакеты. Свои жесты: движение с ускорением, тап = клик, тап двумя пальцами = правый клик, прокрутка двумя пальцами (EV_WHEEL), кнопка клик-пада. Не Synaptics (ELAN, Alps) - прежняя обычная PS/2-мышь. Тест: tools/tests/tptest.c. НЕ проверено на железе.
- Settings -> Trackpad: скорость, Tap to click, Natural scrolling (в prefs, поля tp_*, признак 0xA5). Терминал: `touchpad`.
- kernel/ec.c: чтение EC (порты 0x62/0x66). Терминал: `ec` печатает 256 байт. Батарею пока не показываем: у каждой модели раскладка своя, нужен дамп с нетбука (снять `ec` при заряженной и разряжающейся батарее, с адаптером и без).

## Батарея в строке меню (build 29)
- gui/battery.c: заряд (байт 0..100) и признак адаптера (байт и маска) читаются из EC раз в 5 с; значок с процентами слева от лупы, зелёный с молнией на адаптере, красный при <= 15 %. Пока не настроено - ничего не показывается.
- Смещения задаёт терминал: `battery set PCT [AC MASK]` (hex), `battery off`; хранятся в prefs (bat_*). Искать: `ec snap`, поменять состояние (воткнуть/выдернуть зарядку), `ec diff`. Как только будет дамп с нетбука - вшить таблицу по DMI/модели.

- build 30: автоопределение батареи (bat_autodetect): ищет в EC слова "остаток, полная ёмкость" рядом со словом напряжения (5000..13500), берёт только единственного кандидата. Процент = остаток*100/полная, адаптер определяется по тренду (растёт - адаптер). Запускается через 6 с после старта, если ничего не задано вручную; `battery auto` - вручную. Не проверено на железе.

## Центр уведомлений (build 31)
- Клик по часам в строке меню открывает панель с историей (до 30, новые сверху): карточка с иконкой, заголовком, текстом; клик по карточке убирает её, кнопка Clear очищает всё; колесо прокручивает; Esc или клик мимо закрывает.
- Каждое notify_icon (терминал `notify`, таймер, обновления, корзина) пишется в историю (nh_add в gui/spotlight.inc). История не сохраняется между перезагрузками. Проверено только на хосте.

## Сочетания клавиш как в macOS (build 32)
- Ctrl+W/Ctrl+Q закрывают верхнее окно, Ctrl+M сворачивает, Ctrl+Tab переключает на следующее открытое окно (в Терминале не работают, чтобы не мешать его хоткеям). В меню справа показаны подсказки (Ctrl+W, Ctrl+Q, Ctrl+M, F3).

## USB 2.0 (build 33, этап 1)
- drivers/usb.c: EHCI (PCI класс 0C0320), опрос без прерываний, память 1:1. Забирает контроллер у BIOS, сбрасывает, включает порты; low/full-speed устройства отдаёт компаньон-контроллеру (PORT_OWNER). Перечисление: адрес, дескрипторы, интерфейс 08/06/50 (Bulk-Only), SCSI INQUIRY/TEST UNIT READY/READ CAPACITY/READ10/WRITE10, восстановление после ошибок. Результат - blk_t (usb_blk(i)), с ним работает fat_mount.
- Терминал: `usb` (скан и отчёт), `usb ls [путь]` (каталог FAT32 на флешке).
- tools/tests/usbtest.c: поддельный контроллер и флешка (проверяют структуры qTD/QH, DATA0/1, BOT, fat_format/fat_write/fat_read поверх флешки). На железе не проверялось.
- Не сделано: хабы, xHCI, клавиатуры/мыши USB, горячее подключение (скан по команде), интеграция во Files.

## Новые обои и «живые» обои день/ночь (build 34)
- Новые сцены в tools/render_wallpapers.py: Northern Lights (Aurora), Liquid Glass, Violet Dawn, а также пары Petals Day/Night и Layers Day/Night (в стиле macOS). Всего 12 в assets/wall.pak (~2,5 МБ). WALL_RENDERED = 12.
- Пары "<Имя> Day" и "<Имя> Night" - «живые» обои (gui/scenery.c): в Settings виден один пункт "<Имя> (Dynamic)", картинка выбирается по часу RTC (ночь с 19 до 6), смена происходит сама (wm_set_time -> scenery_set_hour). Выбранный id сохраняется как дневной.
- Сохранённые фото-обои сместились на +7 id (WALL_PHOTO0 растёт вместе с WALL_RENDERED): после обновления выбор «своего фото» в Settings нужно сделать заново.

## Значки и Browix (build 35)
- Браузер называется Browix (Dock, меню, заголовок окна, страницы about/start). Значок - собственный глобус вместо компаса, чтобы не напоминать значок чужого браузера.
- Значок Calendar показывает настоящий день месяца и день недели из RTC (icon_set_date из wm_set_time), меняется сам после полуночи.
- Остальные значки нарисованы с нуля в общей стилистике (скруглённая плитка, мягкий градиент, блик) и ни с чьими значками не совпадают.

## Дата и время (build 36)
- Settings → Date & Time: текущее время, часовой пояс (шаг 30 минут), переключатель «Set automatically», кнопка Sync now, ручная установка (день, месяц, год, час, минуты + Set). Список страниц стал плотнее (шаг 30 px), чтобы поместились 13 пунктов.
- gui/timeset.c: синхронизация берёт заголовок `Date:` из HTTPS-ответа (raw.githubusercontent.com, kernel/net.c: net_fetch_date), переводит UTC в местное по выбранному поясу и пишет в CMOS (drivers/rtc.c: rtc_write). Автосинхронизация при включении: после 8 с и когда есть сеть, до 4 попыток; по умолчанию выключена (пока пояс не выбран, время UTC было бы неверным).
- Пояс и переключатель хранятся в настройках (prefs: ts_ok/ts_auto/ts_tz). Сертификаты TLS по дате не проверяются, поэтому работает и при неверных часах.
- tools/tests/tstest.c: разбор Date, переходы через год и високосный день, ручная установка и неверные значения, клики по странице.

## Русская раскладка (build 37)
- drivers/ps2.c: раскладка ЙЦУКЕН (kbd_layout/kbd_set_layout), переключение Alt+Shift или Win+Пробел; Caps/Shift дают заглавные, Ctrl+буква остаётся латиницей (шорткаты работают). Клавиатура шлёт кодпоинты Юникода (0x410–0x44F, Ё/ё); № заменён на #, его нет в шрифте.
- Меню-бар показывает EN/RU (событие KEY_LAYOUT).
- UTF-8 в редакторах: gui/font.h (utf8_put/clen/prev/cp/append/pop, is_text_cp). Русский ввод: Notes, TextEdit, Spotlight, Launchpad, поиск в Settings, переименование в Files, адресная строка Browix (поиск кодирует байты в %XX).
- Пароли (вход, установщик, Wi-Fi) и терминал остались ASCII. Поиск без учёта регистра пока только для латиницы.
- Тесты: tools/tests/rukbd.c, rutest.c.

## Язык и регион (build 38)
- Settings → Language & Region: язык интерфейса English/Русский и раскладка клавиатуры при запуске. Хранится в prefs (lg_ok/lg_ui/lg_kb); раскладка запоминается последней использованной.
- gui/lang.c: tr() переводит английские строки оболочки (меню-бар, меню, названия приложений и заголовки окон, Dock, Launchpad, Spotlight, боковая панель Settings, страницы Date & Time и Language). Неизвестные строки остаются английскими: остальные страницы и приложения переводятся по мере добавления в таблицу.
- Боковая панель Settings: шаг 27 px, чтобы поместились 14 страниц.
- Тест: tools/tests/lgshot.c.

## Оптимизация (build 39)
- fb_flip шлёт в видеопамять только изменившиеся отрезки строк (prevbuf хранит прошлый кадр): ввод буквы — 17 КБ вместо 3 МБ на кадр, неизменный кадр — 0. Видеопамять пишется медленно, на нетбуке это главный выигрыш. Курсор перед flip стирается из старой позиции (wm_frame).
- Сборка: -mtune=bonnell (расписание команд под Atom, ISA не меняется), без таблиц раскрутки стека.
- Уже было: тени и размытие отключаются автоматически (fb_perf), idle в hlt, кадр не чаще раза в 20 мс.
- Тесты: tools/tests/fliptest.c (счётчик записей, совпадение экрана с полным flip), perf.c (мс на кадр, gprof: основное время — mix/fb_shadow/fb_glass).

## Курсор не мигает при клике (build 40)
- Раньше сцена целиком уходила в видеопамять и стирала курсор на время записи — при клике он мигал. Теперь writing только изменений занимает доли миллисекунды, а старый курсор стирается перед flip лишь если он сдвинулся.

## Быстрее загрузка (build 41)
- Искусственная пауза заставки сокращена с 0,7 до 0,35 с, калибровка тактов PIT — с 5 до 3 тиков. Итого загрузка примерно на 0,4 с короче; остальное время — драйверы железа (его без замеров на нетбуке не трогаем).
- Профиль кадра (gprof на хосте): fb_glass ≈ 28%, заливки span ≈ 24%, fb_shadow ≈ 18%; размытие уже на бегущих суммах. В простое сцена не перерисовывается (кроме мигания курсора в Notes, 2 Гц). Дальше оптимизировать имеет смысл по замерам с нетбука.
- tools/tests/idle.c (перерисовок за 10 с по приложениям), boot.c.
