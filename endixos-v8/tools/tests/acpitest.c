/* ACPI: разбор _S5_ из AML (типы SLP_TYP для выключения). */
#include "kernel/acpi.h"
extern int printf(const char *, ...);
static int fails;
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)
int main(void)
{
    u8 a, b;
    static const u8 intel[] = { 0x10, 0x20, 0x5C, 0x08, '_', 'S', '5', '_', 0x12, 0x06, 0x04, 0x0A, 0x07, 0x0A, 0x07, 0x00, 0x00, 0x00 };
    CHECK(acpi_parse_s5(intel, sizeof intel, &a, &b) == 0 && a == 7 && b == 7, "Intel: _S5_ = {7, 7} (байтовые константы)");
    static const u8 qemu[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    CHECK(acpi_parse_s5(qemu, sizeof qemu, &a, &b) == 0 && a == 0 && b == 0, "QEMU: _S5_ = {0, 0} (нули без префикса)");
    static const u8 mixed[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x07, 0x04, 0x01, 0x0A, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 };
    CHECK(acpi_parse_s5(mixed, sizeof mixed, &a, &b) == 0 && a == 1 && b == 5, "смешанный: {1, 5}");
    static const u8 none[] = { 0x08, '_', 'S', '3', '_', 0x12, 0x06, 0x04, 0x0A, 0x03, 0x0A, 0x03, 0, 0, 0, 0 };
    CHECK(acpi_parse_s5(none, sizeof none, &a, &b) != 0, "нет _S5_: отказ");
    printf("fails=%d\n", fails); return fails;
}
