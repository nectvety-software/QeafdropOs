# .qeapp package + dlopen loader

A `.qeapp` is a ZIP with one manifest at the root:

```
app/
  manifest.json   {"id":"com.example.hello","name":"Hello","version":"1.0",
                   "entry":"src/main.c","min_os":"540x1170","kind":"app"}
  src/            main.c (`lv_obj_t *create(lv_obj_t *parent)` + `q_create`)
  assets/icon.png 64x64 (or reuse homo/custom/assets/ios/*.png)
  README.md
```

Build on host:

```
python homo/scripts/make_qeapp.py <app dir> out/apps/<name>.qeapp
```

Current 12 apps (all rebuilt in this format): bootui, browser, fastboot,
gallery, installer, keyboard, launcher, lock, messages, recovery, settings, usbui.
`out/installer.qeapp` is a copy of `out/apps/installer.qeapp` (8246 B).

Package:

```
python homo/scripts/package_violet.py   # stages /apps/*.qeapp into initramfs
```

On device:
- Persistent: initramfs `/apps/*.qeapp` (survives reboot, browse via Files → `/apps`)
- LUN RAM: `F:\VQEAF\apps\*.qeapp` (copy via Windows, loses on reboot)
- Loader (`homo/framework/app_loader.c`) validates `manifest.json` (`id`/`entry`),
  finds `code/*.so`, then `dlopen` + `dlsym` `q_apps` / `q_create`.

**Static limitation (honest):** `homo-init` is static aarch64-musl (`-static`,
no `PT_INTERP`). musl static cannot `dlopen`; loader reports
`dlopen unavailable in static build` instead of crashing. To run `.so` code
you need PIE/dynamic + staged linker. For now install = list + validate;
running new code = copy `src/` into tree and rebuild.
