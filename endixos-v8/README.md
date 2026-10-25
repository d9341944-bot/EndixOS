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
