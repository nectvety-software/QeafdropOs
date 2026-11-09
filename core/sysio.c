/* SPDX-License-Identifier: MIT */
/* core/sysio.c - file/netlink access under qeafinit.

 * Two worlds compile this one file. On the phone (__aarch64__) the link
 * carries no libc, so every kernel touch is an svc #0 with a hand-declared
 * ABI. On the host the same signatures run against fixture directories of
 * plain files, which is what lets the gates measure the parsers instead of
 * trusting them. Netlink has no host analogue: the host answers -38 and the
 * caller reports "no answer", never a made-up frame.
 */

#include "sysio.h"

#if defined(__aarch64__)

/* ---------- device: raw Linux aarch64 syscalls ---------------------------- */
/* Numbers and flags from the kernel's asm-generic uapi; the values used here
 * were taken from the same headers the phone's kernel was built with. */
#define SYS_ppoll 73
#define SYS_openat 56
#define SYS_getdents64 61
#define SYS_read 63
#define SYS_write 64
#define SYS_close 57
#define SYS_socket 198
#define SYS_bind 200
#define SYS_sendto 206
#define SYS_recvfrom 207
#define SYS_clock_gettime 113
#define SYS_nanosleep 101
#define SYS_ioctl 29
#define SYS_mknodat 33
#define SYS_mkdirat 34
#define SYS_symlinkat 36
#define SYS_mount 40
#define SYS_ftruncate 46
#define SYS_lseek 62
#define SYS_fsync 82
#define SYS_faccessat 48
#define SYS_exit_group 94
#define SYS_clone 220
#define SYS_mmap 222

#define CLOCK_MONOTONIC_ 1

#define O_WRONLY_ 1
#define O_RDWR_ 2
#define O_CREAT_ 64
#define O_TRUNC_ 512
#define AT_FDCWD_ (-100L)
#define PROT_RW_ 3
#define MAP_SHARED_ 1
#define SIGCHLD_ 17
#define ENOENT_ 2
#define EBUSY_ 16
#define EEXIST_ 17
#define AF_NETLINK_ 16
#define SOCK_DGRAM_ 2

struct sys_dirent64 {
    unsigned long d_ino;
    unsigned long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[1];
};

struct sys_sockaddr_nl {
    unsigned short nl_family;
    unsigned short nl_pad;
    unsigned int nl_pid;
    unsigned int nl_groups;
};

struct sys_pollfd {
    int fd;
    short events;
    short revents;
};

struct sys_timespec {
    long tv_sec;
    long tv_nsec;
};

static long sc6(long n, long a0, long a1, long a2, long a3, long a4, long a5) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x4 __asm__("x4") = a4;
    register long x5 __asm__("x5") = a5;
    __asm__ __volatile__("svc #0"
                         : "+r"(x0)
                         : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                         : "memory");
    return x0;
}

static long sc3(long n, long a0, long a1, long a2) {
    return sc6(n, a0, a1, a2, 0, 0, 0);
}

static long sc4(long n, long a0, long a1, long a2, long a3) {
    return sc6(n, a0, a1, a2, a3, 0, 0);
}

static long sc5(long n, long a0, long a1, long a2, long a3, long a4) {
    return sc6(n, a0, a1, a2, a3, a4, 0);
}

static int err_of(long r) { /* syscalls return -errno inline on aarch64 */
    return r >= 0 ? 0 : (int)r;
}

int sysio_read_file(const char *path, char *buf, int cap) {
    long fd, n = 0, r;
    if (cap <= 0) return -2;
    fd = sc4(SYS_openat, -100L /* AT_FDCWD */, (long)path, 0L /* O_RDONLY */, 0L);
    if (fd < 0) return err_of(fd);
    while (n < cap - 1) {
        r = sc3(SYS_read, fd, (long)buf + n, (long)(cap - 1) - n);
        if (r < 0) { sc3(SYS_close, fd, 0, 0); return err_of(r); }
        if (r == 0) break;
        n += r;
    }
    sc3(SYS_close, fd, 0, 0);
    buf[n] = '\0';
    return (int)n;
}

int sysio_write_str(const char *path, const char *text) {
    int n = 0;
    while (text[n]) n++;
    long fd = sc4(SYS_openat, -100L, (long)path, O_WRONLY_, 0L);
    if (fd < 0) return err_of(fd);
    long r = sc3(SYS_write, fd, (long)text, n);
    sc3(SYS_close, fd, 0, 0);
    if (r < 0) return err_of(r);
    return r == n ? 0 : -2;
}

/* Without devtmpfs the recovery partition is not guaranteed to appear under
 * the one name the rescue shell uses. On the 2026-11-09 boot the p77 write
 * failed with no diagnostics at all; try the by-name aliases and remember
 * the last errno so QEAFBOOT.LOG can name the reason. */
static int g_last_errno;

int sysio_last_errno(void) { return g_last_errno; }

static int path_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static long open_block(const char *path, long flags) {
    static const char *const aliases[] = {
        "/dev/block/mmcblk0p77",
        "/dev/block/by-name/recovery",
        "/dev/block/bootdevice/by-name/recovery",
        "/dev/mmcblk0p77",
    };
    long fd = sc4(SYS_openat, -100L, (long)path, flags, 0L);
    unsigned i;

    if (fd >= 0) { g_last_errno = 0; return fd; }
    g_last_errno = err_of(fd);
    if (!path_eq(path, "/dev/mmcblk0p77")) return fd;
    for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
        fd = sc4(SYS_openat, -100L, (long)aliases[i], flags, 0L);
        if (fd >= 0) { g_last_errno = 0; return fd; }
        g_last_errno = err_of(fd);
    }
    return fd;
}

static long open_writable(const char *path) {
    return open_block(path, O_WRONLY_);
}

static long open_readonly(const char *path) {
    return open_block(path, 0L);
}

int sysio_block_write(const char *path, const uint8_t *buf, size_t len) {
    long fd = open_writable(path);
    size_t done = 0;
    if (fd < 0) return err_of(fd);
    while (done < len) {
        size_t chunk = len - done;
        if (chunk > (1u << 20)) chunk = 1u << 20;
        long r = sc3(SYS_write, fd, (long)(buf + done), (long)chunk);
        if (r <= 0) {
            g_last_errno = r < 0 ? err_of(r) : -2;
            sc3(SYS_close, fd, 0, 0);
            return r < 0 ? err_of(r) : -2;
        }
        done += (size_t)r;
    }
    sc3(SYS_close, fd, 0, 0);
    return 0;
}

int sysio_block_read(const char *path, uint8_t *buf, size_t len) {
    long fd = sc4(SYS_openat, -100L, (long)path, 0L, 0L);
    size_t done = 0;
    if (fd < 0) return err_of(fd);
    while (done < len) {
        size_t chunk = len - done;
        if (chunk > (1u << 20)) chunk = 1u << 20;
        long r = sc3(SYS_read, fd, (long)(buf + done), (long)chunk);
        if (r <= 0) break;
        done += (size_t)r;
    }
    sc3(SYS_close, fd, 0, 0);
    return done == len ? 0 : -2;
}

static uint8_t g_verify_chunk[1 << 20];

int sysio_block_write_at(const char *path, size_t offset,
                         const uint8_t *buf, size_t len) {
    long fd = open_writable(path);
    size_t done = 0;
    if (fd < 0) return err_of(fd);
    {
        long at = sc3(SYS_lseek, fd, (long)offset, 0L);
        if (at < 0) {
            g_last_errno = err_of(at);
            sc3(SYS_close, fd, 0, 0);
            return g_last_errno;
        }
    }
    while (done < len) {
        long r = sc3(SYS_write, fd, (long)(buf + done), (long)(len - done));
        if (r <= 0) {
            g_last_errno = r < 0 ? err_of(r) : -2;
            sc3(SYS_close, fd, 0, 0);
            return g_last_errno;
        }
        done += (size_t)r;
    }
    {
        long f = sc3(SYS_fsync, fd, 0, 0);
        if (f < 0) {
            g_last_errno = err_of(f);
            sc3(SYS_close, fd, 0, 0);
            return g_last_errno;
        }
    }
    sc3(SYS_close, fd, 0, 0);
    return 0;
}

int sysio_block_read_at(const char *path, size_t offset,
                        uint8_t *buf, size_t len) {
    long fd = open_readonly(path);
    size_t done = 0;
    if (fd < 0) return err_of(fd);
    {
        long at = sc3(SYS_lseek, fd, (long)offset, 0L);
        if (at < 0) {
            g_last_errno = err_of(at);
            sc3(SYS_close, fd, 0, 0);
            return g_last_errno;
        }
    }
    while (done < len) {
        long r = sc3(SYS_read, fd, (long)(buf + done), (long)(len - done));
        if (r <= 0) break;
        done += (size_t)r;
    }
    sc3(SYS_close, fd, 0, 0);
    return done == len ? 0 : -2;
}

int sysio_block_verify(const char *path, const uint8_t *expected, size_t len) {
    long fd = sc4(SYS_openat, -100L, (long)path, 0L, 0L);
    size_t done = 0;
    int rc = 0;
    if (fd < 0) return err_of(fd);
    while (done < len) {
        size_t chunk = len - done;
        size_t got = 0;
        if (chunk > sizeof(g_verify_chunk)) chunk = sizeof(g_verify_chunk);
        while (got < chunk) {
            long r = sc3(SYS_read, fd,
                         (long)(g_verify_chunk + got), (long)(chunk - got));
            if (r <= 0) break;
            got += (size_t)r;
        }
        if (got != chunk) { rc = -2; break; }
        for (size_t i = 0; i < chunk; i++)
            if (g_verify_chunk[i] != expected[done + i]) { rc = -3; break; }
        if (rc) break;
        done += chunk;
    }
    sc3(SYS_close, fd, 0, 0);
    return rc;
}

int sysio_first_entry(const char *dir, const char *prefix, char *out, int cap) {
    static char raw[2048];
    long fd = sc4(SYS_openat, -100L, (long)dir, 1L << 19 /* O_DIRECTORY */, 0L);
    if (fd < 0) return -1;
    long n = sc3(SYS_getdents64, fd, (long)raw, (long)sizeof(raw));
    sc3(SYS_close, fd, 0, 0);
    if (n <= 0) return -1;

    int plen = 0;
    while (prefix[plen]) plen++;
    long off = 0;
    while (off < n) {
        struct sys_dirent64 *d = (struct sys_dirent64 *)(raw + off);
        const char *name = d->d_name;
        int i = 0;
        for (; i < plen; i++)
            if (name[i] != prefix[i]) break;
        if (i == plen && name[plen] != '\0') {
            int j = 0;
            while (name[j] && j + 1 < cap) { out[j] = name[j]; j++; }
            out[j] = '\0';
            return 0;
        }
        if (d->d_reclen == 0) break;
        off += d->d_reclen;
    }
    return -1;
}

int sysio_nl_open(int protocol) {
    struct sys_sockaddr_nl me;
    for (unsigned char *p = (unsigned char *)&me; p < (unsigned char *)&me + sizeof(me); p++)
        *p = 0;
    me.nl_family = AF_NETLINK_;

    long fd = sc3(SYS_socket, AF_NETLINK_, SOCK_DGRAM_, protocol);
    if (fd < 0) return err_of(fd);
    long b = sc3(SYS_bind, fd, (long)&me, sizeof(me));
    if (b < 0) { sc3(SYS_close, fd, 0, 0); return err_of(b); }
    return (int)fd;
}

int sysio_nl_send(int fd, const void *req, int reqlen) {
    long s = sc6(SYS_sendto, fd, (long)req, reqlen, 0L, 0L, 0L);
    if (s < 0) return err_of(s);
    return s == reqlen ? 0 : -2;
}

int sysio_nl_recv(int fd, void *rsp, int rcap, int timeout_ms) {
    struct sys_pollfd pf = { fd, 1 /* POLLIN */, 0 };
    struct sys_timespec ts = { timeout_ms / 1000, (timeout_ms % 1000) * 1000000L };
    long pr = sc5(SYS_ppoll, (long)&pf, 1L, (long)&ts, 0L, 0L);
    if (pr <= 0) return -110;
    long r = sc6(SYS_recvfrom, fd, (long)rsp, rcap, 0L, 0L, 0L);
    if (r < 0) return err_of(r);
    return (int)r;
}

void sysio_nl_close(int fd) {
    sc3(SYS_close, fd, 0, 0);
}

uint64_t sysio_monotonic_ms(void) {
    struct sys_timespec ts = { 0, 0 };
    if (sc3(SYS_clock_gettime, CLOCK_MONOTONIC_, (long)&ts, 0L) < 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000L);
}

void sysio_sleep_ms(uint32_t ms) {
    struct sys_timespec req;
    struct sys_timespec rem;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    rem.tv_sec = 0;
    rem.tv_nsec = 0;
    /* A short syscall is retried against `rem`, which is why nanosleep writes
     * it; looping keeps the idle shell from spinning early. */
    while (sc3(SYS_nanosleep, (long)&req, (long)&rem, 0L) < 0) {
        if (rem.tv_sec <= 0 && rem.tv_nsec <= 0) break;
        req = rem;
    }
}

/* ---- bring-up primitives ------------------------------------------------ */

int sysio_exists(const char *path) {
    long r = sc4(SYS_faccessat, AT_FDCWD_, (long)path, 0L, 0L);
    return r == 0 ? 1 : 0;
}

int sysio_mkdir_p(const char *path) {
    char tmp[256];
    size_t n = 0, i;

    if (!path || !path[0]) return -ENOENT_;
    while (path[n] && n + 1 < sizeof(tmp)) { tmp[n] = path[n]; n++; }
    tmp[n] = '\0';
    /* Walk the separators and create each prefix. A leading '/' makes the first
     * prefix empty, which mkdirat refuses; start at 1 so it never is. */
    for (i = 1; i <= n; i++) {
        if (tmp[i] != '/' && tmp[i] != '\0') continue;
        {
            char keep = tmp[i];
            tmp[i] = '\0';
            long r = sc3(SYS_mkdirat, AT_FDCWD_, (long)tmp, 0755L);
            tmp[i] = keep;
            if (r < 0 && (int)r != -EEXIST_) return (int)r;
        }
    }
    return 0;
}

int sysio_symlink(const char *target, const char *path) {
    long r = sc3(SYS_symlinkat, (long)target, AT_FDCWD_, (long)path);
    if (r < 0 && (int)r != -EEXIST_) return (int)r;
    return 0;
}

int sysio_mount(const char *source, const char *target, const char *fstype) {
    long r = sc5(SYS_mount, (long)source, (long)target, (long)fstype, 0L, 0L);
    /* EBUSY means configfs is already there, which is the state the caller
     * wants; treating it as failure would refuse the second boot of a session. */
    if (r < 0 && (int)r != -EBUSY_) return (int)r;
    return 0;
}

int sysio_map_file(const char *path, size_t size, void **out) {
    long fd, addr;

    if (!path || !out || size == 0) return -ENOENT_;
    fd = sc4(SYS_openat, AT_FDCWD_, (long)path,
             (long)(O_RDWR_ | O_CREAT_ | O_TRUNC_), 0644L);
    if (fd < 0) return (int)fd;
    if (sc3(SYS_ftruncate, fd, (long)size, 0L) < 0) {
        sc3(SYS_close, fd, 0, 0);
        return -1;
    }
    addr = sc6(SYS_mmap, 0L, (long)size, PROT_RW_, MAP_SHARED_, fd, 0L);
    sc3(SYS_close, fd, 0, 0);
    if (addr < 0) return (int)addr;
    *out = (void *)addr;
    return 0;
}

int sysio_fork(void) {
    /* aarch64 has no fork(): clone(SIGCHLD) with no stack argument is the same
     * thing, and it is what every libc on this target actually calls. */
    long r = sc6(SYS_clone, SIGCHLD_, 0L, 0L, 0L, 0L, 0L);
    return (int)r;
}

void sysio_exit(int status) {
    sc3(SYS_exit_group, (long)status, 0, 0);
    for (;;) { } /* exit_group does not return */
}

/* ---- card I/O: the DRM modeset path ------------------------------------- */

int sysio_open(const char *path, int flags) {
    long fd = sc4(SYS_openat, AT_FDCWD_, (long)path, (long)flags, 0L);
    return fd < 0 ? (int)fd : (int)fd;
}

void sysio_close(int fd) {
    if (fd >= 0) sc3(SYS_close, (long)fd, 0, 0);
}

long sysio_ioctl(int fd, unsigned long request, void *arg) {
    long r = sc3(SYS_ioctl, (long)fd, (long)request, (long)arg);
    return r < 0 ? r : 0;
}

int sysio_mknod(const char *path, unsigned mode, unsigned dev) {
    long r = sc4(SYS_mknodat, AT_FDCWD_, (long)path, (long)mode, (long)dev);
    if (r < 0 && (int)r != -EEXIST_) return (int)r;
    return 0;
}

void *sysio_mmap_fd(int fd, size_t len, size_t offset) {
    long p = sc6(SYS_mmap, 0L, (long)len, PROT_RW_, MAP_SHARED_, (long)fd,
                 (long)offset);
    if (p < 0) return NULL;
    return (void *)p;
}

#else /* !__aarch64__ */

int sysio_last_errno(void) { return 0; }

/* ---------- host gate: the same calls against fixture files --------------- */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

int sysio_read_file(const char *path, char *buf, int cap) {
    FILE *f;
    size_t n = 0;
    if (cap <= 0) return -2;
    f = fopen(path, "rb");
    if (!f) return -1;
    while (n + 1 < (size_t)cap) {
        size_t r = fread(buf + n, 1, (size_t)cap - 1 - n, f);
        if (r == 0) break;
        n += r;
    }
    fclose(f);
    buf[n] = '\0';
    return (int)n;
}

int sysio_write_str(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t len = strlen(text);
    size_t w = fwrite(text, 1, len, f);
    fclose(f);
    return w == len ? 0 : -2;
}

int sysio_block_write(const char *path, const uint8_t *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(buf, 1, len, f);
    fclose(f);
    return w == len ? 0 : -2;
}

int sysio_block_read(const char *path, uint8_t *buf, size_t len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t r = fread(buf, 1, len, f);
    fclose(f);
    return r == len ? 0 : -2;
}

int sysio_block_write_at(const char *path, size_t offset,
                         const uint8_t *buf, size_t len) {
    FILE *f = fopen(path, "r+b");
    size_t w;
    if (!f) return -1;
    if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return -2; }
    w = fwrite(buf, 1, len, f);
    if (fflush(f) != 0 || fclose(f) != 0) return -2;
    return w == len ? 0 : -2;
}

int sysio_block_read_at(const char *path, size_t offset,
                        uint8_t *buf, size_t len) {
    FILE *f = fopen(path, "rb");
    size_t r;
    if (!f) return -1;
    if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return -2; }
    r = fread(buf, 1, len, f);
    fclose(f);
    return r == len ? 0 : -2;
}

static uint8_t g_verify_chunk[1 << 20];

int sysio_block_verify(const char *path, const uint8_t *expected, size_t len) {
    FILE *f = fopen(path, "rb");
    size_t done = 0;
    int rc = 0;
    if (!f) return -1;
    while (done < len) {
        size_t chunk = len - done;
        if (chunk > sizeof(g_verify_chunk)) chunk = sizeof(g_verify_chunk);
        if (fread(g_verify_chunk, 1, chunk, f) != chunk) { rc = -2; break; }
        for (size_t i = 0; i < chunk; i++)
            if (g_verify_chunk[i] != expected[done + i]) { rc = -3; break; }
        if (rc) break;
        done += chunk;
    }
    fclose(f);
    return rc;
}

int sysio_first_entry(const char *dir, const char *prefix, char *out, int cap) {
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t plen;
    if (!d) return -1;
    plen = strlen(prefix);
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (strncmp(e->d_name, prefix, plen) != 0) continue;
        if (strlen(e->d_name) <= plen) continue;
        strncpy(out, e->d_name, (size_t)cap - 1);
        out[cap - 1] = '\0';
        closedir(d);
        return 0;
    }
    closedir(d);
    return -1;
}

int sysio_nl_open(int protocol) {
    (void)protocol;
    return -38; /* ENOSYS: the host has no netlink; the seam must say no answer */
}

int sysio_nl_send(int fd, const void *req, int reqlen) {
    (void)fd; (void)req; (void)reqlen;
    return -38;
}

int sysio_nl_recv(int fd, void *rsp, int rcap, int timeout_ms) {
    (void)fd; (void)rsp; (void)rcap; (void)timeout_ms;
    return -38;
}

void sysio_nl_close(int fd) {
    (void)fd;
}

uint64_t sysio_monotonic_ms(void) {
    /* The host has no single portable clock and the gates do not need a real
     * one: this only ever measures how long a frame took to draw. */
    return (uint64_t)((long long)clock() * 1000LL / (long long)CLOCKS_PER_SEC);
}

void sysio_sleep_ms(uint32_t ms) {
    uint64_t deadline = sysio_monotonic_ms() + ms;
    while (sysio_monotonic_ms() < deadline) { /* host-only; gates never call it */
    }
}

/* The host cannot create kernel objects, so every bring-up primitive answers
 * ENOSYS rather than succeeding at nothing. The gadget bind is the only caller
 * and it logs the refusal instead of reporting a LUN that does not exist. */

int sysio_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int sysio_mkdir_p(const char *path) { (void)path; return -38; }
int sysio_symlink(const char *target, const char *path) {
    (void)target; (void)path; return -38;
}
int sysio_mount(const char *source, const char *target, const char *fstype) {
    (void)source; (void)target; (void)fstype; return -38;
}
int sysio_map_file(const char *path, size_t size, void **out) {
    (void)path; (void)size; if (out) *out = NULL; return -38;
}
int sysio_fork(void) { return -38; }
void sysio_exit(int status) { (void)status; }

int sysio_open(const char *path, int flags) {
    (void)path; (void)flags; return -38;
}
void sysio_close(int fd) { (void)fd; }
long sysio_ioctl(int fd, unsigned long request, void *arg) {
    (void)fd; (void)request; (void)arg; return -38;
}
int sysio_mknod(const char *path, unsigned mode, unsigned dev) {
    (void)path; (void)mode; (void)dev; return -38;
}
void *sysio_mmap_fd(int fd, size_t len, size_t offset) {
    (void)fd; (void)len; (void)offset; return NULL;
}

#endif /* __aarch64__ */

/* ---------- shared, and the part the gates actually measure --------------- */

int sysio_read_int(const char *path, int *out) {
    char buf[24];
    int n = sysio_read_file(path, buf, (int)sizeof(buf));
    if (n < 0) return n;
    int i = 0, sign = 1, v = 0, any = 0;
    while (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n') i++;
    if (buf[i] == '-') { sign = -1; i++; }
    else if (buf[i] == '+') i++;
    while (buf[i] >= '0' && buf[i] <= '9') { v = v * 10 + (buf[i] - '0'); i++; any = 1; }
    if (!any) return -2;
    *out = sign * v;
    return 0;
}
