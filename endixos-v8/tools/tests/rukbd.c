/* Русская раскладка: ps2 (скан-коды -> кодпоинты). */
#include "drivers/ps2.c"
extern int printf(const char *, ...);
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
void serial_puts(const char *s) { (void)s; } void serial_put_hex(u32 v) { (void)v; }
static i32 got[64]; static int ng;
void event_push(int t, i32 a, i32 b, i32 c) { (void)t; (void)b; (void)c; if (ng < 64) got[ng++] = a; }
static void key(u8 sc) { kbd_byte(sc); kbd_byte(sc | 0x80); }
int main(void)
{
    kbd_byte(0x38); kbd_byte(0x2A); kbd_byte(0xAA); kbd_byte(0xB8);              /* Alt+Shift */
    CHECK(kbd_layout() == 1, "Alt+Shift включает RU");
    ng = 0; key(0x10); key(0x11); key(0x29);
    CHECK(got[0] == 0x439 && got[1] == 0x446 && got[2] == 0x451, "q w ` -> й ц ё");
    ng = 0; kbd_byte(0x2A); key(0x10); key(0x29); kbd_byte(0xAA);
    CHECK(got[0] == 0x419 && got[1] == 0x401, "Shift -> Й Ё");
    ng = 0; key(0x28); kbd_byte(0x2A); key(0x28); key(0x35); kbd_byte(0xAA);
    CHECK(got[0] == 0x44D && got[1] == 0x42D && got[2] == ',', "э Э и Shift+/ = запятая");
    ng = 0; kbd_byte(0x1D); key(0x1F); kbd_byte(0x9D);
    CHECK(got[0] == 's', "Ctrl+буква остаётся латиницей");
    ng = 0; kbd_byte(0x3A); key(0x1E); kbd_byte(0x3A);
    CHECK(got[0] == 0x424, "Caps Lock -> Ф");
    kbd_byte(0x2A); kbd_byte(0x38); kbd_byte(0xB8); kbd_byte(0xAA);
    CHECK(kbd_layout() == 0, "Shift+Alt возвращает EN");
    printf("fails=%d\n", fails); return fails;
}
