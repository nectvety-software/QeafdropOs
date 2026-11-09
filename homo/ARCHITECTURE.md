# 桌面架构

```text
framework/          桌面、App 管理器、注册表接口、导航和动画
framework/ui/       字体、状态栏、控制中心及布局
product/            默认 App、图标、Home/Dock 排列及独立后台模块清单
apps/               App 视图与交互
services/           业务模型、服务接口与任务状态
platform/linux/     framebuffer/evdev、Linux 服务后端与 i.MX6ULL 适配
platform/preview/   无硬件预览/测试后端
platform/lvgl/      LVGL 9.4 第三方源码
config/             独立公共 LVGL 配置
cmake/              Make/CMake 共用源码清单及运行时库
custom/assets/      UI 资源与北京地图
tests/              服务、生命周期、UI 与实体板验证
tools/              地图制作、图标缓存、隐私检查与打包
```

`hachimi_desktop_create(catalog)` 接收产品提供的 `hachimi_app_catalog_t`。框架不包含具体 App 头文件。清单数组与模块指针在桌面运行期间必须有效；运行中拒绝重新配置，销毁后可换产品。`ios_desktop_create()` 是默认产品的兼容入口。

App 使用 create/on_open/on_close 管理视图，系统钩子管理跨应用后台任务。后台清单不依赖图标是否可见，拒绝重复模块，按启动的逆序停止。音乐和闹钟跨应用运行；时钟隐藏时暂停页面刷新，继续后台计时。当前采用单进程、静态注册、首次打开后缓存视图，尚无动态插件或缓存淘汰。

服务使用普通 C 数据结构，不依赖 LVGL。控件只在 UI 线程访问。LED App 通过 services/led 调用 Linux 后端；服务处理直接访问失败后的 helper 降级、40 ms 写入节流、读取与用户修改冲突和取消。网络/时间 helper 只允许固定操作。

状态栏控件随创建注册、删除注销，最后一个控件删除后释放定时器。地图通过产品描述的 reserve_left_edge 声明左边缘归 App 导航使用，桌面不判断地图 App ID。

Linux 与预览构建共享运行时源码清单；平台后端及链接依赖绑定到 CMake target。公共 LVGL 配置不依赖模拟器或 GUI Guider。Linux Make 默认启用 i.MX6ULL NEON/PXP；通用 Linux CMake 默认关闭，两种构建均复制 UI 和地图资源。

新增 App：实现 apps/<name>/ 模块，在 framework/app_registry.h 追加 App ID，在 cmake/hachimi_runtime_sources.list 加入源码，并在 product/default_apps.c 配置图标、排列、手势策略与后台模块。完整裁剪需要同时移除产品引用及源码条目；只移除图标不会裁掉代码。现有限制为 18 个 Home 图标、24 个 Home/Dock 入口和 16 个缓存 App 视图。

测试使用内存显示和独立临时 HOME。本机 CTest 使用 preview 后端，实体板 Make 测试使用 Linux 后端。测试网络数据为合成示例，不是项目所有者的网络配置。


## violet（Redmi Note 7 Pro）移植

`platform/violet/` 把同一套 LVGL 桌面搬到小米 Redmi Note 7 Pro（`violet`，SDM660，1080x2340）。它不是普通 Linux 用户态：`homo-init` 就是 PID 1，initramfs 就是根文件系统。

内核约束决定了这一层必须存在：没有 devtmpfs，`/dev` 由 `devnodes.c` 从 `/sys/class/*/dev` 建出；没有 fbdev 和 fbcon，`drm_kms.c` 直接用 DRM ioctl（dumb buffer + ADDFB/SETCRTC）扫描输出；configfs 与 `f_mass_storage` 内置，所以 QEAFRESCUE 是真正的 USB gadget，卷放在 `/run/qeaf-lun.img` 的 mmap 里，和内核共享 page cache。

外壳默认竖屏：540x1170 竖屏与 1170x540 横屏均为面板 2.0 倍精确拟合、无黑边。设置运行在外壳画布（竖/横跟随方向，responsive light 1 列列表），其余横屏应用使用 1170x540 画布；打开横屏应用时有效方向强制 landscape，关闭恢复外壳方向。旋转 90/270 度时同一套映射倒转。触摸走完全相同的逆变换。`panel_scale.c` 是纯函数，`tests/panel_scale_test.c` 在主机上逐像素验证两种画布、四种旋转、分块刷新与触摸往返。

竖屏 Home 网格按 iPhone 390x844 的比例在 `ui/ios_metrics.c` 里算出来（4 列、83 px 图标、44 px 列距、38 px 边距、15 px 标签、148 px 行距、1028 处 114 px dock、1152 处 186x7 home bar、46 px 状态栏），横屏保留 800x480 的旧网格；`tests/ios_metrics_test.c` 的 41 项同时锁定两套数字。控制中心模糊 Home 快照时临时隐藏 Home 的状态栏（`desktop.h` 的 `hachimi_desktop_set_status_bar_visible`），否则模糊副本会叠在 sheet 自己那行时间下面。布局细节见 `ui/UI_LAYOUT.md`。

构建与刷机：`python scripts/build_violet.py` 交叉编译，`python scripts/package_violet.py` 打包 `build/violet/boot-homo-violet.img`（约 28.6 MB，占 p77 的 64 MiB 的 43%）。镜像写入 p77（recovery），不写 p76/boot。细节与真机验证清单见 `platform/violet/README.md`。

SDCARD: LUN FAT16 32 MiB RAM (`fs/storage.c` `UMOS_STORAGE_COUNT=3` `VQEAF,UserData,Install`, `fat16_mkdir_in` 建嵌套), `/apps/*.qeapp` 12 个新 app (manifest+src+assets) 打进 initramfs, qua reboot 可见 (Files → `/apps`), LUN `F:\VQEAF\apps\` 临时副本. 当前镜像 `out/boot-qeafdropos-violet.img` `28590080 B` `sha256 c7edf677…ef26b3ee3cf` `PASS`.

Giới hạn (tra cứu 2026-11-10): hakios-xnu-kernel (XNU POCO X5 Pro SM7325, không có violet), hakios-metal (gộp kernel, Adreno 642L), hakios-uefi (Mu-Silicium, fw lấy từ xbl máy mình), luma (Fedora/GTK, không có violet), dipper-xnu (SDM845+WCN3990 Wi-Fi sống trên XNU khi có fw modem, gần violet nhất nhưng là XNU không phải Linux 4.14), omarchy-mobile (Arch mainline v7.3-rc6, fw từ factory image, nguyên tắc tách fw đúng nhưng kernel không phải 4.14).
