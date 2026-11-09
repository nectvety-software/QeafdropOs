/* SPDX-License-Identifier: MIT */
/* core/sysio.h - the seam between the OS and the kernel it runs on.

 * qeafinit is a userspace ELF: while the rescue kernel is up, /sys and
 * netlink are real, and the seams above this file must only ever report what
 * they read. The device path issues Linux syscalls directly (the link carries
 * no libc); the host gate compiles the same callers against a fixture tree of
 * ordinary files, so parsing is measured, not asserted.
 *
 * Every call returns a byte count / value on success and -errno style
 * negative below: -1 no such entry, -2 short read, -38 the host cannot
 * answer (netlink), -110 the kernel took too long.
 */

#ifndef UMOS_SYSIO_H
#define UMOS_SYSIO_H

#include <stddef.h>
#include <stdint.h>

/* Read at most cap bytes; NUL-terminates what it read. Files under /sys
 * report size 0, so this reads until EOF rather than trusting st_size. */
int sysio_read_file(const char *path, char *buf, int cap);

/* Parse one integer the way sysfs writes them ("84\n" -> 84). */
int sysio_read_int(const char *path, int *out);

int sysio_write_str(const char *path, const char *text);

/* Raw block I/O for the rescue flash verb: write/read a whole partition
 * image. Chunked so a 24 MiB write never depends on one huge kernel write.
 * Returns 0 on success, negative on failure. Host branch uses stdio so the
 * host gate can exercise the same code against a fixture file. */
int sysio_block_write(const char *path, const uint8_t *buf, size_t len);
int sysio_block_read(const char *path, uint8_t *buf, size_t len);
/* The same I/O at an absolute byte offset, for the preference slot parked in
 * the tail of p77. Neither call truncates: only the requested bytes change. */
int sysio_block_write_at(const char *path, size_t offset,
                         const uint8_t *buf, size_t len);
int sysio_block_read_at(const char *path, size_t offset,
                        uint8_t *buf, size_t len);
/* Reads the partition back in 1 MiB chunks and compares against `expected`,
 * so a 24 MiB verification never needs a second 24 MiB buffer. */
int sysio_block_verify(const char *path, const uint8_t *expected, size_t len);

/* First entry of `dir` whose name starts with `prefix`, minus any suffix
 * after a caller-supplied cut; "" when the directory is missing or empty.
 * out receives the bare name ("phy0"), returns 0 on a hit, -1 on none. */
int sysio_first_entry(const char *dir, const char *prefix, char *out, int cap);

/* A netlink socket the caller drives step by step: nl80211 scans are a
 * family lookup, a trigger, one event to wait for and a multi-part dump,
 * and all of it must land on the same portid or the kernel answers
 * somewhere else. Host has no netlink: open answers -38, and the seam
 * above it reports "no answer" instead of a fabricated frame. */
int  sysio_nl_open(int protocol);          /* protocol 16 = NETLINK_GENERIC */
int  sysio_nl_send(int fd, const void *req, int reqlen);
/* One datagram, waiting at most timeout_ms: byte count, or -110 on timeout. */
int  sysio_nl_recv(int fd, void *rsp, int rcap, int timeout_ms);
void sysio_nl_close(int fd);

/* Milliseconds since an unspecified boot instant, monotonic and never
 * rewinding. The shell's transition clock reads this: the wall clock the lock
 * screen shows can be set by the user or by a network, and a tween fed by a
 * clock that jumps backwards would freeze mid-flight.
 * Host: a plain process timer, enough for the gates to measure a frame. */
uint64_t sysio_monotonic_ms(void);

/* Block for up to ms. On the device this is nanosleep, so an idle shell loop
 * costs nothing; on the host the gates drive their own synthetic clock and
 * never call it, so it only has to exist and not hang. */
void sysio_sleep_ms(uint32_t ms);

/* ---- bring-up primitives: device-only, and honest about it ---------------
 * These exist for the USB gadget bind (bsp/violet/usb_gadget_bind.c), which is
 * the one thing in this tree that has to create kernel objects rather than
 * read them. On the host every call answers -38 (ENOSYS) and nothing is
 * pretended: the gates cannot bind a gadget and do not claim to. */

/* mkdir -p. Returns 0, or the negative kernel error. */
int sysio_mkdir_p(const char *path);
/* symlinkat(target, path); an existing link is success, not a failure. */
int sysio_symlink(const char *target, const char *path);
/* 1 = exists, 0 = does not. */
int sysio_exists(const char *path);
/* mount(source, target, fstype); an already-mounted target is success. */
int sysio_mount(const char *source, const char *target, const char *fstype);
/* Create (or truncate) `path` to `size` bytes and map it shared; writes land
 * in the page cache the kernel serves to the USB host. */
int sysio_map_file(const char *path, size_t size, void **out);
/* clone(SIGCHLD, no stack): the whole of fork() on aarch64. */
int sysio_fork(void);
/* exit_group; never returns. */
void sysio_exit(int status);

/* ---- card I/O: what a DRM/KMS modeset needs -----------------------------
 * Separate from the file helpers above because these take a descriptor and a
 * request number, and because the panel driver is the only caller. Host stubs
 * answer -38 / NULL so a gate cannot believe a modeset happened. */

/* errno of the last block-device open/write, for the rescue log. 0 on host. */
int  sysio_last_errno(void);

int  sysio_open(const char *path, int flags);
void sysio_close(int fd);
long sysio_ioctl(int fd, unsigned long request, void *arg);
/* Character device node, e.g. /dev/dri/card0, from major:minor the kernel
 * published. # CONFIG_DEVTMPFS is not set on violet, so qeafinit has to make
 * the node itself or there is nothing to open. */
int  sysio_mknod(const char *path, unsigned mode, unsigned dev);
/* Shared mapping of a descriptor at `offset`, or NULL. */
void *sysio_mmap_fd(int fd, size_t len, size_t offset);

#endif
