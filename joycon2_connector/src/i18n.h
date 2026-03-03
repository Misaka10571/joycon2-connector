#pragma once
#include <string>
#include <unordered_map>
#include <Windows.h>

enum class Lang { EN, ZH };

inline Lang g_currentLang = Lang::ZH;

// Detect system UI language: returns ZH for Chinese, EN for everything else
inline Lang DetectSystemLanguage() {
    LANGID langId = GetUserDefaultUILanguage();
    WORD primaryLang = PRIMARYLANGID(langId);
    if (primaryLang == LANG_CHINESE) {
        return Lang::ZH;
    }
    return Lang::EN;
}

inline const char* T(const char* key) {
    static const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> table = {
        // App
        {"app_title",           {{"en", "JoyCon2 Connector"},        {"zh", u8"JoyCon2 连接器"}}},

        // Navigation
        {"nav_dashboard",       {{"en", "Dashboard"},                {"zh", u8"仪表盘"}}},
        {"nav_add_device",      {{"en", "Add Device"},               {"zh", u8"添加设备"}}},
        {"nav_layout_mgr",     {{"en", "Layout Manager"},           {"zh", u8"背键布局"}}},
        {"nav_mouse_settings", {{"en", "Mouse Settings"},           {"zh", u8"鼠标设置"}}},
        {"nav_language",        {{"en", "Language"},                  {"zh", u8"语言"}}},

        // Dashboard
        {"dash_vigem_ok",       {{"en", "ViGEm Connected"},          {"zh", u8"ViGEm 已连接"}}},
        {"dash_vigem_fail",     {{"en", "ViGEm Not Found"},          {"zh", u8"ViGEm 未找到"}}},
        {"dash_no_device",      {{"en", "No controllers connected"}, {"zh", u8"暂无已连接的手柄"}}},
        {"dash_no_device_hint", {{"en", "Click \"Add Device\" to get started"}, {"zh", u8"点击「添加设备」开始连接"}}},
        {"dash_player",         {{"en", "Player"},                   {"zh", u8"玩家"}}},
        {"dash_mapping",        {{"en", "Mapping: DS4"},             {"zh", u8"映射: DS4"}}},
        {"dash_gyro_source",    {{"en", "Gyro Source"},              {"zh", u8"体感源"}}},
        {"dash_disconnect",     {{"en", "Disconnect"},               {"zh", u8"断开连接"}}},
        {"dash_side_left",      {{"en", "Left"},                     {"zh", u8"左"}}},
        {"dash_side_right",     {{"en", "Right"},                    {"zh", u8"右"}}},
        {"dash_orient_upright", {{"en", "Upright"},                  {"zh", u8"竖握"}}},
        {"dash_orient_sideways",{{"en", "Sideways"},                 {"zh", u8"横握"}}},
        {"dash_gyro_both",      {{"en", "Both"},                     {"zh", u8"双侧"}}},
        {"dash_gyro_left",      {{"en", "Left"},                     {"zh", u8"左侧"}}},
        {"dash_gyro_right",     {{"en", "Right"},                    {"zh", u8"右侧"}}},
        {"dash_settings",       {{"en", "Settings"},                 {"zh", u8"设置"}}},
        {"dash_swap_abxy",      {{"en", u8"Swap A\u21c4B / X\u21c4Y"},         {"zh", u8"交换 A\u21c4B / X\u21c4Y"}}},
        {"dash_swap_abxy_hint", {{"en", u8"Nintendo \u21c4 Xbox button layout"},{"zh", u8"任天堂 \u21c4 Xbox 按键布局"}}},

        // Controller Types
        {"type_single_joycon",  {{"en", "Single Joy-Con"},           {"zh", u8"单 Joy-Con"}}},
        {"type_dual_joycon",    {{"en", "Dual Joy-Con"},             {"zh", u8"双 Joy-Con"}}},
        {"type_pro",            {{"en", "Pro Controller"},           {"zh", u8"Pro 手柄"}}},
        {"type_nso_gc",         {{"en", "NSO GC Controller"},        {"zh", u8"NSO GC 手柄"}}},

        // Add Device Wizard
        {"add_step1_title",     {{"en", "Select Controller Type"},   {"zh", u8"选择手柄类型"}}},
        {"add_step2_title",     {{"en", "Configure Options"},        {"zh", u8"配置选项"}}},
        {"add_step3_title",     {{"en", "Scanning..."},              {"zh", u8"扫描中..."}}},
        {"add_select_side",     {{"en", "Select Side"},              {"zh", u8"选择方向"}}},
        {"add_select_orient",   {{"en", "Select Grip"},              {"zh", u8"选择握法"}}},
        {"add_select_gyro",     {{"en", "Select Gyro Source"},       {"zh", u8"选择体感源"}}},
        {"add_start_scan",      {{"en", "Start Scanning"},           {"zh", u8"开始扫描"}}},
        {"add_cancel",          {{"en", "Cancel"},                   {"zh", u8"取消"}}},
        {"add_back",            {{"en", "Back"},                     {"zh", u8"上一步"}}},
        {"add_next",            {{"en", "Next"},                     {"zh", u8"下一步"}}},
        {"add_scanning_hint",   {{"en", "Press the sync button on your controller..."}, {"zh", u8"请按下手柄上的同步按钮..."}}},
        {"add_pre_scan_hint",  {{"en", "Ensure the controller is powered off before scanning."},
                                                                     {"zh", u8"请确保手柄在开始扫描前处于关机状态。"}}},
        {"add_scanning_active_hint", {{"en", "Press and HOLD the sync button on the controller until connected."},
                                                                     {"zh", u8"请按住手柄上的同步按钮不松，直到连接成功。"}}},
        {"add_scan_right_title", {{"en", "Scan RIGHT Joy-Con"},      {"zh", u8"扫描右 Joy-Con"}}},
        {"add_scan_left_title",  {{"en", "Scan LEFT Joy-Con"},       {"zh", u8"扫描左 Joy-Con"}}},
        {"add_right_connected", {{"en", "RIGHT Joy-Con connected successfully!"},
                                                                     {"zh", u8"右 Joy-Con 连接成功！"}}},
        {"add_connected",       {{"en", "Connected!"},               {"zh", u8"已连接！"}}},
        {"add_timeout",         {{"en", "Scan timed out. Try again."}, {"zh", u8"扫描超时，请重试。"}}},

        // Layout Manager
        {"layout_title",        {{"en", "GL/GR Button Layout"},      {"zh", u8"GL/GR 背键布局"}}},
        {"layout_add",          {{"en", "New Layout"},               {"zh", u8"新建布局"}}},
        {"layout_delete",       {{"en", "Delete"},                   {"zh", u8"删除"}}},
        {"layout_rename",       {{"en", "Rename"},                   {"zh", u8"重命名"}}},
        {"layout_set_active",   {{"en", "Set Active"},               {"zh", u8"设为激活"}}},
        {"layout_active_badge", {{"en", "Active"},                   {"zh", u8"当前"}}},
        {"layout_gl",           {{"en", "GL Mapping"},               {"zh", u8"GL 映射"}}},
        {"layout_gr",           {{"en", "GR Mapping"},               {"zh", u8"GR 映射"}}},
        {"layout_hint",         {{"en", "Press C to cycle layouts in-game\nPress ZL+ZR+GL+GR to open management"}, {"zh", u8"游戏中按 C 键切换布局\n按 ZL+ZR+GL+GR 呼出管理"}}},
        {"layout_no_layouts",   {{"en", "No layouts configured"},    {"zh", u8"未配置布局"}}},
        {"layout_name_label",   {{"en", "Layout Name"},              {"zh", u8"布局名称"}}},

        // Mouse Settings
        {"mouse_title",         {{"en", "Optical Mouse Settings"},   {"zh", u8"光学鼠标设置"}}},
        {"mouse_enable",        {{"en", "CHAT key toggles mouse mode"}, {"zh", u8"CHAT 键切换鼠标模式"}}},
        {"mouse_fast",          {{"en", "Fast Sensitivity"},         {"zh", u8"高灵敏度"}}},
        {"mouse_normal",        {{"en", "Normal Sensitivity"},       {"zh", u8"中灵敏度"}}},
        {"mouse_slow",          {{"en", "Low Sensitivity"},          {"zh", u8"低灵敏度"}}},
        {"mouse_scroll_speed",  {{"en", "Scroll Speed"},             {"zh", u8"滚轮速度"}}},
        {"mouse_current_mode",  {{"en", "Current Mode"},             {"zh", u8"当前模式"}}},
        {"mouse_mode_off",      {{"en", "OFF"},                      {"zh", u8"关闭"}}},
        {"mouse_mode_fast",     {{"en", "FAST"},                     {"zh", u8"快速"}}},
        {"mouse_mode_normal",   {{"en", "NORMAL"},                   {"zh", u8"普通"}}},
        {"mouse_mode_slow",     {{"en", "SLOW"},                     {"zh", u8"慢速"}}},
        {"mouse_hint",          {{"en", "Only available for Right Joy-Con (Joy-Con 2)"},
                                                                     {"zh", u8"仅适用于右 Joy-Con (Joy-Con 2)"}}},
        {"mouse_interpolation",  {{"en", "Smooth Cursor (Interpolation)"},
                                                                     {"zh", u8"光标平滑（插值）"}}},
        {"mouse_interp_rate",    {{"en", "Interpolation Rate (Hz)"},
                                                                     {"zh", u8"插值频率 (Hz)"}}},

    };

    std::string langKey = (g_currentLang == Lang::EN) ? "en" : "zh";
    auto it = table.find(key);
    if (it != table.end()) {
        auto langIt = it->second.find(langKey);
        if (langIt != it->second.end()) {
            return langIt->second.c_str();
        }
    }
    return key;
}
