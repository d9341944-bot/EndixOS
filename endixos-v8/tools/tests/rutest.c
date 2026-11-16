/* Заметки: ввод и редактирование русского текста (UTF-8). */
#include "gui/notes.c"
#undef txt
#undef len
#undef cur
#undef nl
extern int printf(const char *, ...);
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
int main(void)
{
    notes_init();
    notes_key(0x41F, 0); notes_key(0x440, 0); notes_key(0x438, 0); notes_key(0x432, 0); notes_key(0x435, 0); notes_key(0x442, 0);
    notes_key(' ', 0); notes_key('a', 0); notes_key('b', 0);
    CHECK(NT.len == 6 * 2 + 3 && NT.txt[0] == (char)0xD0 && NT.txt[1] == (char)0x9F, "Привет ab лежит в UTF-8");
    notes_key('\b', 0); notes_key('\b', 0); notes_key('\b', 0);                  /* убрали 'b','a',' ' */
    notes_key('\b', 0);                                                           /* и 'т' целиком */
    CHECK(NT.len == 10 && NT.cur == 10, "Backspace удаляет русскую букву целиком");
    notes_key(KEY_LEFT, 0); notes_key(KEY_LEFT, 0);
    CHECK(NT.cur == 6, "стрелка влево двигает на символ");
    notes_key(KEY_DELETE, 0);
    CHECK(NT.len == 8 && NT.txt[6] == (char)0xD0 && NT.txt[7] == (char)0xB5, "Delete удаляет символ справа (в)");
    for (int i = 0; i < 100; i++) notes_key(0x44F, 0);                            /* перенос длинной строки */
    CHECK(NT.nl > 1, "длинный русский текст переносится");
    printf("fails=%d\n", fails); return fails;
}
