/* Wi-Fi: запомненные сети (список по «свежести»), забывание, повторные попытки; сохранение в настройках. */
#include "gui/wifi.c"
extern int printf(const char *, ...);
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
int main(void)
{
    char a[33], b[64];
    remember("Home", "pass1234");
    CHECK(seq(sv_ssid, "Home") && seq(sv_pass, "pass1234"), "первая сеть запомнена");
    remember("Cafe", "cafepass1");
    CHECK(seq(sv_ssid, "Cafe") && seq(kn_ssid[0], "Home"), "новая встаёт первой, прошлая сдвигается");
    remember("Work", "workpass1"); remember("Phone", "phonepas1");
    CHECK(seq(sv_ssid, "Phone") && seq(kn_ssid[0], "Work") && seq(kn_ssid[1], "Cafe") && seq(kn_ssid[2], "Home"), "до 4 сетей в порядке свежести");
    remember("Office", "offpass12");
    CHECK(seq(sv_ssid, "Office") && seq(kn_ssid[2], "Cafe") && !known_pass("Home"), "пятая вытесняет самую старую");
    remember("Cafe", "newcafe12");
    CHECK(seq(sv_ssid, "Cafe") && seq(known_pass("Cafe"), "newcafe12") && seq(kn_ssid[0], "Office") && seq(kn_ssid[1], "Phone"), "повторный вход обновляет пароль и поднимает сеть, дублей нет");
    int dup = 0; for (int i = 0; i < 3; i++) if (seq(kn_ssid[i], "Cafe")) dup++;
    CHECK(!dup, "в запасных нет второй копии");
    forget("Cafe");
    CHECK(seq(sv_ssid, "Office") && seq(kn_ssid[0], "Phone") && !known_pass("Cafe"), "забыли последнюю: на её месте следующая");
    forget("Phone");
    CHECK(seq(sv_ssid, "Office") && !known_pass("Phone"), "забыли запасную");
    wifi_known_get(0, a, b); CHECK(a[0] == 0 || seq(a, kn_ssid[0]), "wifi_known_get отдаёт запасные сети");
    wifi_known_set(1, "X", "xxxxxxxx"); wifi_known_get(1, a, b); CHECK(seq(a, "X") && seq(b, "xxxxxxxx"), "wifi_known_set/get");
    wifi_known_get(7, a, b); CHECK(a[0] == 0, "неверный номер слота безопасен");
    printf("fails=%d\n", fails); return fails;
}
