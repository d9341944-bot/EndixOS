#ifndef L_STDIO_H
#define L_STDIO_H
#include <stdarg.h>
#include <stddef.h>
typedef struct FILE FILE;
extern FILE *stdout, *stderr, *stdin;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
FILE *fopen(const char *p, const char *m);
int fclose(FILE *f);
size_t fread(void *b, size_t s, size_t n, FILE *f);
size_t fwrite(const void *b, size_t s, size_t n, FILE *f);
int fseek(FILE *f, long off, int wh);
long ftell(FILE *f);
int fflush(FILE *f);
int fprintf(FILE *f, const char *fmt, ...);
int printf(const char *fmt, ...);
int snprintf(char *b, size_t n, const char *fmt, ...);
int sprintf(char *b, const char *fmt, ...);
int vsnprintf(char *b, size_t n, const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int sscanf(const char *s, const char *fmt, ...);
int putchar(int c);
int putc(int c, FILE *f);
int fputc(int c, FILE *f);
int puts(const char *s);
int fputs(const char *s, FILE *f);
int fgetc(FILE *f);
char *fgets(char *b, int n, FILE *f);
int remove(const char *p);
int rename(const char *a, const char *b);
int feof(FILE *f);
#endif
