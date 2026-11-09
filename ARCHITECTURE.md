# QeafdropOs source architecture

QeafdropOs is one OS project and one shipping image. The active shipping runtime
is the Homo-derived LVGL 9.4 system under `homo/`, linked with the shared root
`core/` and `fs/` code. The physical source directories previously exposed
through junctions in `homo/` have now been moved into their canonical locations:
`framework/`, `services/`, `platform/violet/` and `scripts/`. The root
`VQEAF_OS/` tree is no longer an active source root. Its remaining duplicate
snapshot is archived outside the repository at
`D:\QeafivelsOfficial\QeafdropOs-archive\VQEAF_OS-legacy-duplicates`.
The unique legacy installer source is preserved at `homo/legacy/installer/`
and is not part of the shipping build.

## Canonical source ownership

| Path | Responsibility |
| --- | --- |
| `core/` | Shared core primitives used by the shipping runtime and rescue/storage channel |
| `fs/` | Shared filesystem, initramfs, rescue command channel and persistent-storage code |
| `homo/platform/violet/` | Redmi Note 7 Pro (violet) board support, DRM/KMS, input, power, USB and radio integration |
| `homo/platform/lvgl/` | LVGL 9.4 graphics/UI dependency |
| `homo/framework/` | Desktop, window/UI framework, app API, app loader, manager and registry |
| `homo/services/` | System services, including network, clock, storage, power and media |
| `homo/apps/` | Built-in QeafdropOs applications and Settings pages |
| `homo/custom/` and `homo/config/` | Theme assets, fonts, palettes and platform configuration |
| `homo/scripts/` | Canonical build, packaging, QEAPP and asset tooling |
| `homo/tests/` | Shipping-runtime and service smoke/regression tests |
| `homo/build/violet/` | Generated build and staging output; never edit by hand |
| `out/` | Root-level build artifacts, test output and toolchain cache |
| `homo/legacy/` | Historical sources only; never included in the shipping build |

## Legacy UMOS fixtures

The root `hal/`, `bsp/`, `gui/`, `apps/`, `init/`, `libc_stubs.c` and
`build/build.ps1` belong to the historical UMOS userspace experiment. The root
`Makefile` explicitly keeps those targets as legacy compile/UI regression
fixtures; they are not the shipping QeafdropOs image. Shared `core/` and `fs/`
remain canonical because the shipping image links them. Do not copy current
Homo platform, service, framework or app code into the legacy fixtures.

## Canonical build

From the repository root:

```powershell
python homo/scripts/build_violet.py
python homo/scripts/package_violet.py
python scripts/validate_bootimg.py out/boot-qeafdropos-violet.img
```

`make os` runs the supported checks and the same build/package/validation flow.
No build command in this document flashes a device. Device writes require a
separate explicit operation and confirmation.
