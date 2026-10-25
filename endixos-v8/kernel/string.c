/* memcpy/memset/memmove на rep-инструкциях: быстро и без циклов,
 * которые компилятор мог бы превратить обратно в вызов этих же функций. */
#include "types.h"

void *memset(void *dst, int c, size_t n)
{
    void *ret = dst;
    u32 pattern = (u8)c * 0x01010101u;
    size_t q = n >> 2, r = n & 3;
    __asm__ volatile("rep stosl" : "+D"(dst), "+c"(q) : "a"(pattern) : "memory");
    __asm__ volatile("rep stosb" : "+D"(dst), "+c"(r) : "a"(pattern) : "memory");
    return ret;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    void *ret = dst;
    size_t q = n >> 2, r = n & 3;
    __asm__ volatile("rep movsl" : "+D"(dst), "+S"(src), "+c"(q) : : "memory");
    __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(r) : : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, size_t n)
{
    u8 *d = dst;
    const u8 *s = src;
    if (d <= s || d >= s + n)
        return memcpy(dst, src, n);
    /* перекрытие с копированием назад: идём с конца */
    d += n;
    s += n;
    while (n--)
        *--d = *--s;
    return dst;
}
