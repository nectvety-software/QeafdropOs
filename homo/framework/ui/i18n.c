#include "i18n.h"

#include "lvgl.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char * zh;
    const char * en;
    const char * vi;
} homo_text_t;

/* The system shell and Settings are covered here; any Chinese string that is
 * not listed falls back to the original text, so a partly translated screen
 * never turns into an empty label. Vietnamese carries diacritics: the UI face
 * is Liberation Sans (fonts.c), whose cmap covers every precomposed Vietnamese
 * character, with PingFang chained behind it for CJK. */
static const homo_text_t CATALOG[] = {
    /* Settings navigation and About */
    {"设置", "Settings", "Cài đặt"},
    {"关于设备", "About device", "Giới thiệu"},
    {"网络与连接", "Network & connection", "Mạng & kết nối"},
    {"显示与亮度", "Display & brightness", "Màn hình & độ sáng"},
    {"时间与日期", "Time & date", "Thời gian & ngày"},
    {"语言", "Language", "Ngôn ngữ"},
    {"管理 Wi-Fi、蓝牙、以太网与个人热点。", "Manage Wi-Fi, Bluetooth, Ethernet and hotspot.", "Quản lý Wi-Fi, Bluetooth, Ethernet và hotspot."},
    {"查看设备信息、显示规格与软件版本。", "Device information, display and software version.", "Thông tin thiết bị, màn hình và phiên bản phần mềm."},
    {"设备名称", "Device name", "Tên thiết bị"},
    {"中央处理器", "Processor", "CPU"},
    {"Linux 版本", "Linux version", "Phiên bản Linux"},
    {"序列号", "Serial number", "Số serial"},
    {"未提供", "Not provided", "Không có"},
    {"显示分辨率", "Resolution", "Độ phân giải"},
    {"图形框架", "Graphics", "Đồ họa"},
    {"UI 版本", "UI version", "Phiên bản UI"},
    {"内存", "Memory", "Bộ nhớ"},
    {"存储空间", "Storage", "Lưu trữ"},
    {"读取中…", "Loading...", "Đang đọc…"},
    {"暂不可用", "Unavailable", "Không khả dụng"},
    {"关于设备 about device Hachimi", "About device Hachimi", "Giới thiệu Hachimi"},
    {"网络与连接 network wifi Wi-Fi bluetooth", "Network wifi Wi-Fi bluetooth", "Mạng wifi Wi-Fi bluetooth"},
    {"显示与亮度 display brightness backlight", "Display brightness backlight", "Màn hình độ sáng"},
    {"时间与日期 time date clock timezone 时区 校时", "Time date clock timezone", "Thời gian ngày giờ múi giờ"},
    {"language 语言 ngon ngu", "language", "ngôn ngữ"},

    /* Search and generic actions */
    {"搜索", "Search", "Tìm kiếm"},
    {"取消", "Cancel", "Hủy"},
    {"确定", "OK", "Đồng ý"},
    {"关闭", "Close", "Đóng"},
    {"返回", "Back", "Quay lại"},
    {"删除", "Delete", "Xóa"},
    {"保存", "Save", "Lưu"},
    {"完成", "Done", "Xong"},
    {"打开", "Open", "Mở"},
    {"刷新", "Refresh", "Làm mới"},
    {"今天", "Today", "Hôm nay"},
    {"明天", "Tomorrow", "Ngày mai"},
    {"昨天", "Yesterday", "Hôm qua"},

    /* Display page */
    {"调整屏幕亮度，让显示效果适合当前环境。", "Adjust screen brightness for the current light.", "Chỉnh độ sáng cho phù hợp môi trường."},
    {"亮度", "Brightness", "Độ sáng"},
    {"屏幕亮度", "Screen brightness", "Độ sáng màn hình"},

    /* Network page */
    {"未检测到无线网卡", "No wireless adapter", "Không có card Wi-Fi"},
    {"已关闭", "Off", "Đã tắt"},
    {"未连接 · 点击选择网络", "Not connected - tap to choose", "Chưa kết nối - chạm để chọn"},
    {"已开启", "On", "Đã bật"},
    {"Wi-Fi 已关闭", "Wi-Fi is off", "Wi-Fi đã tắt"},
    {"选择一个网络…", "Choose a network...", "Chọn mạng…"},
    {"连接 Wi-Fi，查看可用网络并管理无线连接。", "Join Wi-Fi, browse networks and manage connections.", "Kết nối Wi-Fi, xem mạng và quản lý kết nối."},
    {"密码需为 8–63 字节或 64 位十六进制密钥", "Password must be 8-63 bytes or 64 hex digits", "Mật khẩu phải 8-63 byte hoặc 64 ký tự hex"},
    {"此网络需要企业认证或不受支持的加密方式", "This network needs enterprise or unsupported security", "Mạng này cần xác thực doanh nghiệp hoặc mã hóa không hỗ trợ"},
    {"加入", "Join", "Tham gia"},
    {"输入密码", "Enter password", "Nhập mật khẩu"},
    {"密码", "Password", "Mật khẩu"},
    {"必填", "Required", "Bắt buộc"},
    {"显示", "Show", "Hiện"},
    {"这是一个开放网络，无需输入密码。", "This is an open network; no password is needed.", "Mạng mở, không cần mật khẩu."},
    {"请输入此 Wi-Fi 网络的密码。", "Enter the password for this Wi-Fi network.", "Nhập mật khẩu cho mạng Wi-Fi này."},
    {"以太网", "Ethernet", "Ethernet"},
    {"蓝牙", "Bluetooth", "Bluetooth"},
    {"个人热点", "Personal hotspot", "Hotspot cá nhân"},
    {"网络详情", "Network details", "Chi tiết mạng"},
    {"网络信息", "Network information", "Thông tin mạng"},
    {"其他网络", "Other networks", "Mạng khác"},
    {"开启 Wi-Fi 后查看可用网络", "Turn on Wi-Fi to see networks", "Bật Wi-Fi để xem mạng"},
    {"正在查找网络…", "Looking for networks...", "Đang tìm mạng…"},
    {"暂未发现其他网络，轻点右上角刷新", "No other networks; tap Refresh", "Không có mạng khác; chạm Làm mới"},
    {"选择网络以加入。已保存的网络会自动连接。", "Choose a network to join. Saved networks connect automatically.", "Chọn mạng để tham gia. Mạng đã lưu tự kết nối."},
    {"重新连接已保存的网络", "Reconnect a saved network", "Kết nối lại mạng đã lưu"},
    {"强", "Strong", "Mạnh"},
    {"中", "Medium", "Trung bình"},
    {"弱", "Weak", "Yếu"},
    {"网络名称", "Network name", "Tên mạng"},
    {"安全性", "Security", "Bảo mật"},
    {"信号", "Signal", "Tín hiệu"},
    {"开放网络", "Open network", "Mạng mở"},
    {"密码保护", "Password protected", "Có mật khẩu"},
    {"企业或其他认证", "Enterprise or other auth", "Doanh nghiệp hoặc xác thực khác"},
    {"加入此网络", "Join this network", "Tham gia mạng này"},
    {"IP 地址", "IP address", "Địa chỉ IP"},
    {"Wi-Fi 地址", "Wi-Fi address", "Địa chỉ Wi-Fi"},
    {"未连接", "Not connected", "Chưa kết nối"},
    {"DNS 与路由", "DNS and routing", "DNS và định tuyến"},
    {"未设置 DNS", "DNS not set", "Chưa đặt DNS"},
    {"未设置默认路由", "No default route", "Chưa có route mặc định"},
    {"断开此网络", "Disconnect this network", "Ngắt mạng này"},
    {"未检测到以太网接口", "No Ethernet interface", "Không có giao diện Ethernet"},
    {"默认路由", "Default route", "Route mặc định"},
    {"未设置", "Not set", "Chưa đặt"},
    {"DNS 服务器", "DNS server", "Máy chủ DNS"},
    {"蓝牙服务可用", "Bluetooth service available", "Dịch vụ Bluetooth sẵn sàng"},
    {"未检测到可用的蓝牙适配器", "No Bluetooth adapter", "Không có adapter Bluetooth"},
    {"热点服务尚未配置", "Hotspot service is not configured", "Dịch vụ hotspot chưa cấu hình"},
    {"此设备尚未安装热点服务", "Hotspot service is not installed", "Chưa cài dịch vụ hotspot"},
    {"网络服务不可用，请安装 Hachimi 网络适配服务", "Network service unavailable; install the adapter", "Dịch vụ mạng không có; hãy cài adapter"},
    {"正在读取设备网络状态…", "Reading network state...", "Đang đọc trạng thái mạng…"},
    {"电脑预览模式 · 请在开发板上管理网络连接", "Desktop preview - manage networks on the board", "Chế độ xem trước - quản lý mạng trên board"},
    {"无法启动网络操作", "Could not start the network action", "Không thể bắt đầu thao tác mạng"},
    {"正在扫描附近网络…", "Scanning nearby networks...", "Đang quét mạng gần đây…"},
    {"正在连接，请稍候…", "Connecting, please wait...", "Đang kết nối, vui lòng đợi…"},
    {"正在更新网络设置…", "Updating network settings...", "Đang cập nhật cài đặt mạng…"},
    {"其他连接", "Other connections", "Kết nối khác"},
    {"正在读取…", "Reading...", "Đang đọc…"},
    {"不可用", "Unavailable", "Không khả dụng"},

    /* Time page */
    {"%04d年%02d月%02d日", "%04d-%02d-%02d", "%04d-%02d-%02d"},
    {"联网校对已关闭", "Network time is off", "Đồng bộ mạng đã tắt"},
    {"已与网络时间同步", "Synced with network time", "Đã đồng bộ với giờ mạng"},
    {"正在等待网络时间同步", "Waiting for network time", "Đang đợi đồng bộ giờ mạng"},
    {"无法读取系统时间服务", "Could not read the time service", "Không đọc được dịch vụ thời gian"},
    {"无法读取系统时区列表", "Could not read the timezone list", "Không đọc được danh sách múi giờ"},
    {"系统设置更新失败", "System settings update failed", "Cập nhật cài đặt thất bại"},
    {"电脑预览模式 · 时间设置在开发板上生效", "Desktop preview - time settings apply on the board", "Chế độ xem trước - cài đặt giờ áp dụng trên board"},
    {"无法启动系统时间服务", "Could not start the time service", "Không thể bắt đầu dịch vụ thời gian"},
    {"设置本地时区，并使用网络时间保持时钟准确。", "Set the local timezone and keep the clock accurate with network time.", "Đặt múi giờ và giữ đồng hồ chính xác bằng giờ mạng."},
    {"日期与时间", "Date & time", "Ngày & giờ"},
    {"正在读取日期…", "Reading the date...", "Đang đọc ngày…"},
    {"时区", "Timezone", "Múi giờ"},
    {"自动校对", "Automatic", "Tự động"},
    {"联网校对时间", "Set time from network", "Đồng bộ giờ qua mạng"},
    {"正在读取系统时间服务…", "Reading the time service...", "Đang đọc dịch vụ thời gian…"},

    /* Control center */
    {"状态不可用", "Status unavailable", "Trạng thái không có"},
    {"未检测到网卡", "No network adapter", "Không có card mạng"},
    {"已连接", "Connected", "Đã kết nối"},
    {"未检测到接口", "No interface", "Không có giao diện"},
    {"网线未连接", "Cable not connected", "Chưa cắm dây mạng"},
    {"预览 · 已开启", "Preview - on", "Xem trước - bật"},
    {"预览 · 已关闭", "Preview - off", "Xem trước - tắt"},
    {"无法启动网络操作，请稍后重试", "Could not start the network action; try again", "Không thể bắt đầu thao tác mạng; thử lại"},
    {"暂无播放", "Nothing playing", "Không phát gì"},
    {"本地音乐", "Local music", "Nhạc trong máy"},
    {"亮度调整失败", "Brightness update failed", "Đổi độ sáng thất bại"},
    {"网络状态读取失败", "Could not read network status", "Không đọc được trạng thái mạng"},
    {"音乐", "Music", "Nhạc"},
    {"周日", "Sun", "CN"},
    {"周一", "Mon", "T2"},
    {"周二", "Tue", "T3"},
    {"周三", "Wed", "T4"},
    {"周四", "Thu", "T5"},
    {"周五", "Fri", "T6"},
    {"周六", "Sat", "T7"},
    {"%d月%d日  %s", "%d/%d  %s", "%d/%d  %s"},

    /* Language page */
    {"系统语言", "System language", "Ngôn ngữ hệ thống"},
    {"选择系统显示语言。应用内容可能仍为原文。", "Choose the system display language. App content may still use its original language.", "Chọn ngôn ngữ hiển thị. Nội dung ứng dụng có thể vẫn là ngôn ngữ gốc."},
    {"重启后仍然生效", "Saved across restarts", "Được lưu qua lần khởi động"},
    {"当前", "Current", "Hiện tại"},
#include "i18n_apps.inc"
};

static int g_lang = HOMO_LANG_VI;

void homo_i18n_set(int lang)
{
    if(lang == HOMO_LANG_VI || lang == HOMO_LANG_EN || lang == HOMO_LANG_ZH)
        g_lang = lang;
    else
        g_lang = HOMO_LANG_EN;
}

int homo_i18n_get(void)
{
    return g_lang;
}

void homo_i18n_attach(void)
{
    lv_label_set_translate_cb(homo_i18n_tr);
}

const char *homo_i18n_tr(const char *text)
{
    size_t i;

    if(text == NULL || g_lang == HOMO_LANG_ZH) return text;
    for(i = 0; i < sizeof(CATALOG) / sizeof(CATALOG[0]); i++) {
        if(strcmp(text, CATALOG[i].zh) == 0)
            return g_lang == HOMO_LANG_VI ? CATALOG[i].vi : CATALOG[i].en;
    }
    return text;
}

const char *homo_i18n_app_title(int app_id)
{
    static const homo_text_t TITLES[] = {
        {"浏览器", "Browser", "Trình duyệt"},
        {"图书", "Books", "Sách"},
        {"计算器", "Calculator", "Máy tính"},
        {"日历", "Calendar", "Lịch"},
        {"相机", "Camera", "Máy ảnh"},
        {"时钟", "Clock", "Đồng hồ"},
        {"文件", "Files", "Tệp"},
        {"健康", "Health", "Sức khỏe"},
        {"LED 控制", "LED control", "Điều khiển LED"},
        {"地图", "Maps", "Bản đồ"},
        {"信息", "Messages", "Tin nhắn"},
        {"音乐", "Music", "Nhạc"},
        {"设置", "Settings", "Cài đặt"},
        {"天气", "Weather", "Thời tiết"},
        {"邮件", "Mail", "Thư"},
    };
    if(app_id < 0 || (size_t)app_id >= sizeof(TITLES) / sizeof(TITLES[0])) return "";
    if(g_lang == HOMO_LANG_VI) return TITLES[app_id].vi;
    if(g_lang == HOMO_LANG_EN) return TITLES[app_id].en;
    return TITLES[app_id].zh;
}

const char *homo_i18n_app_name(int app_id)
{
    static const homo_text_t APPS[] = {
        {"浏览器", "Browser", "Trình duyệt"},       /* HACHIMI_APP_BROWSER */
        {"图书", "Books", "Sách"},                  /* HACHIMI_APP_BOOKS */
        {"计算器", "Calculator", "Máy tính"},       /* HACHIMI_APP_CALCULATOR */
        {"日历", "Calendar", "Lịch"},               /* HACHIMI_APP_CALENDAR */
        {"相机", "Camera", "Máy ảnh"},              /* HACHIMI_APP_CAMERA */
        {"时钟", "Clock", "Đồng hồ"},               /* HACHIMI_APP_CLOCK */
        {"文件", "Files", "Tệp"},                   /* HACHIMI_APP_FILES */
        {"健康", "Health", "Sức khỏe"},             /* HACHIMI_APP_HEALTH */
        {"LED 控制", "LED control", "Điều khiển LED"}, /* HACHIMI_APP_LED_CONTROL */
        {"地图", "Maps", "Bản đồ"},                 /* HACHIMI_APP_MAPS */
        {"信息", "Messages", "Tin nhắn"},           /* HACHIMI_APP_MESSAGES */
        {"音乐", "Music", "Nhạc"},                  /* HACHIMI_APP_MUSIC */
        {"设置", "Settings", "Cài đặt"},            /* HACHIMI_APP_SETTINGS */
        {"天气", "Weather", "Thời tiết"},           /* HACHIMI_APP_WEATHER */
        {"邮件", "Mail", "Thư"},                    /* HACHIMI_APP_MAIL */
    };
    if(app_id < 0 || (size_t)app_id >= sizeof(APPS) / sizeof(APPS[0])) return "";
    if(g_lang == HOMO_LANG_VI) return APPS[app_id].vi;
    if(g_lang == HOMO_LANG_EN) return APPS[app_id].en;
    return APPS[app_id].zh;
}

const char *homo_i18n_lang_name(int lang)
{
    switch(lang) {
    case HOMO_LANG_VI: return "Vietnamese";
    case HOMO_LANG_EN: return "English";
    case HOMO_LANG_JA: return "Japanese";
    case HOMO_LANG_ZH: return "Chinese";
    case HOMO_LANG_ES: return "Spanish";
    case HOMO_LANG_DE: return "German";
    case HOMO_LANG_KO: return "Korean";
    case HOMO_LANG_AUTO: return "Automatic";
    default: return "English";
    }
}

const char *homo_i18n_lang_native(int lang)
{
    switch(lang) {
    case HOMO_LANG_VI: return "Tiếng Việt";
    case HOMO_LANG_EN: return "English";
    case HOMO_LANG_JA: return "日本語";
    case HOMO_LANG_ZH: return "中文";
    case HOMO_LANG_ES: return "Español";
    case HOMO_LANG_DE: return "Deutsch";
    case HOMO_LANG_KO: return "한국어";
    case HOMO_LANG_AUTO: return "Automatic";
    default: return "English";
    }
}

const char *homo_i18n_lang_code(int lang)
{
    switch(lang) {
    case HOMO_LANG_VI: return "VI";
    case HOMO_LANG_EN: return "EN";
    case HOMO_LANG_JA: return "JA";
    case HOMO_LANG_ZH: return "ZH";
    case HOMO_LANG_ES: return "ES";
    case HOMO_LANG_DE: return "DE";
    case HOMO_LANG_KO: return "KO";
    case HOMO_LANG_AUTO: return "A";
    default: return "EN";
    }
}
