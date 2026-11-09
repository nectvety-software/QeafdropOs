/* SPDX-License-Identifier: MIT */
/* libc_stubs.c - Minimal C library stubs for freestanding */

#include <stddef.h>
#include <stdint.h>

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n-- > 0) {
        if (*s1 != *s2) return *(unsigned char *)s1 - *(unsigned char *)s2;
        if (*s1 == '\0') return 0;
        s1++; s2++;
    }
    return 0;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char *strchr(const char *s, int c) {
    for (; *s; s++)
        if (*s == (char)c) return (char *)s;
    return c == '\0' ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *hit = NULL;
    for (; *s; s++)
        if (*s == (char)c) hit = s;
    if (!hit && c == '\0') hit = s;
    return (char *)hit;
}

char *strstr(const char *hay, const char *needle) {
    if (!*needle) return (char *)hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)hay;
    }
    return NULL;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s || n == 0) return dest;
    if (d > s && d < s + n) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    } else {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = (const uint8_t *)s1;
    const uint8_t *b = (const uint8_t *)s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

/* ---- aarch64 Linux syscall shims ------------------------------------------
 * build.ps1 compiles with the musl headers but links -nostdlib, so the C
 * library's open/read/close/reboot/syscall wrappers do not exist at link time.
 * fs/rescue.c genuinely needs them: the `kmsg` verb reads /dev/kmsg and the
 * `reboot recovery|bootloader` verb issues the kernel restart. These are thin
 * `svc #0` wrappers, not stubs that pretend to succeed.
 */
#if defined(__aarch64__)

#define QEAF_NR_CLOSE   57
#define QEAF_NR_READ    63
#define QEAF_NR_OPENAT  56
#define QEAF_NR_REBOOT  142
#define QEAF_AT_FDCWD   (-100)
#define QEAF_REBOOT_MAGIC1 0xfee1dead
#define QEAF_REBOOT_MAGIC2 672274793

static long qeaf_sys6(long n, long a, long b, long c, long d, long e, long f) {
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
                     : "memory");
    return x0;
}

long syscall(long n, long a, long b, long c, long d, long e, long f) {
    return qeaf_sys6(n, a, b, c, d, e, f);
}

int open(const char *path, int flags, ...) {
    return (int)qeaf_sys6(QEAF_NR_OPENAT, QEAF_AT_FDCWD, (long)path,
                          (long)flags, 0, 0, 0);
}

long read(int fd, void *buf, unsigned long count) {
    return qeaf_sys6(QEAF_NR_READ, (long)fd, (long)buf, (long)count, 0, 0, 0);
}

int close(int fd) {
    return (int)qeaf_sys6(QEAF_NR_CLOSE, (long)fd, 0, 0, 0, 0, 0);
}

/* glibc shape: reboot(RB_AUTOBOOT) expands to the four-argument syscall. */
int reboot(int howto) {
    return (int)qeaf_sys6(QEAF_NR_REBOOT, QEAF_REBOOT_MAGIC1,
                          QEAF_REBOOT_MAGIC2, (long)howto, 0, 0, 0);
}

#endif /* __aarch64__ */
