#ifndef QEAF_VERSION_H
#define QEAF_VERSION_H

/* Single source of truth for the OS identity. Every user-visible surface
 * (rescue banner, settings row, USB descriptors, boot log) builds its name
 * from these macros so there is exactly one OS version: QeafdropOs. */
#define QEAF_OS_NAME    "QeafdropOs"
#define QEAF_OS_VERSION "0.1.0"
#define QEAF_OS_STRING  QEAF_OS_NAME " " QEAF_OS_VERSION

#endif