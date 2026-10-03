#ifndef SYSCALL_H
#define SYSCALL_H

struct ude {
    char     name[13];
    unsigned char is_dir;
    unsigned int  size;
} __attribute__((packed));

static inline int sys_write(const char* buf, int len) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(2), "b"(buf), "c"(len) : "memory");
    return ret;
}
static inline void sys_exit(int code) {
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(code));
    __builtin_unreachable();
}
static inline int sys_getpid(void) {
    int ret; __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(3)); return ret;
}
static inline int sys_readchar(void) {
    int ret; __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(4)); return ret;
}
static inline int sys_open(const char* path) {
    int ret; __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(5), "b"(path) : "memory"); return ret;
}
static inline int sys_read(int fd, void* buf, int n) {
    int ret; __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(6), "b"(fd), "c"(buf), "d"(n) : "memory"); return ret;
}
static inline int sys_close(int fd) {
    int ret; __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(7), "b"(fd)); return ret;
}
static inline int sys_spawn(const char* path) {
    int ret; __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(9), "b"(path) : "memory"); return ret;
}
static inline int sys_create(const char* path) {
    int ret; __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(10), "b"(path) : "memory"); return ret;
}
static inline int sys_write_file(const char* path, const void* buf, int n) {
    int ret; __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(11), "b"(path), "c"(buf), "d"(n) : "memory"); return ret;
}
static inline int sys_unlink(const char* path) {
    int ret; __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(12), "b"(path) : "memory"); return ret;
}
static inline int sys_waitpid(int child_id) {
    int ret; __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(13), "b"(child_id) : "memory"); return ret;
}
static inline void sys_erase_cell(void) {
    __asm__ volatile ("int $0x80" : : "a"(14));
}
static inline int sys_readdir(int idx, struct ude* u) {
    int ret; __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(8), "b"(idx), "c"(u) : "memory"); return ret;
}

#endif
