#ifndef L_STDLIB_H
#define L_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t a, size_t b);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int c) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int atoi(const char *s);
long strtol(const char *s, char **e, int base);
int abs(int x);
int system(const char *c);
void qsort(void *b, size_t n, size_t s, int (*cmp)(const void *, const void *));
char *getenv(const char *n);
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767
int rand(void);
void srand(unsigned s);
#endif
