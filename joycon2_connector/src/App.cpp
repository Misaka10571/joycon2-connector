// App.cpp - Main entry point for JoyCon2 Connector
// DirectX 11 + Dear ImGui — Material Design 3, frameless window, DPI-aware
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <tchar.h>
#include <string>
#include <algorithm>

#include <winrt/Windows.Foundation.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#include "UI_Theme.h"
#include "UI_Pages.h"
#include "ConfigManager.h"
#include "ViGEmManager.h"
#include "PlayerManager.h"
#include "i18n.h"
#include "app_icon.h"
#include "version.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

// D3D11 globals
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
static ID3D11ShaderResourceView* g_pIconTextureView = nullptr;
static bool                     g_resizeRequested = false;
static UINT                     g_resizeW = 0, g_resizeH = 0;

// Window globals
static HWND  g_hwnd = nullptr;
static float g_dpiScale = 1.0f;
static bool  g_fontRebuildNeeded = false;
static float g_pendingDpiScale = 1.0f;
static const float TITLE_BAR_HEIGHT_DP = 40.0f; // logical dp

// System tray icon
#define WM_TRAYICON      (WM_USER + 1)
#define IDM_TRAY_SHOW    2001
#define IDM_TRAY_EXIT    2002
#define IDM_TRAY_DISCONNECT_BASE 3000
static NOTIFYICONDATAW g_nid = {};
static bool g_trayIconCreated = false;
static bool g_forceQuit = false;  // true when user clicks Exit from tray menu

// Helper: build full path to a Windows system font file and check existence
static std::string GetSystemFontPath(const char* fontFile) {
    char winDir[MAX_PATH];
    GetWindowsDirectoryA(winDir, MAX_PATH);
    std::string path = std::string(winDir) + "\\Fonts\\" + fontFile;
    FILE* f = fopen(path.c_str(), "rb");
    if (f) { fclose(f); return path; }
    return "";
}

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();

// ---------- UTF-8 to Wide string helper ----------
static std::wstring Utf8ToWide(const char* utf8) {
    if (!utf8 || !utf8[0]) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], len);
    return result;
}

// ---------- System Tray Icon ----------
static void CreateTrayIcon(HWND hwnd) {
    if (g_trayIconCreated) return;
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101));
    wcscpy_s(g_nid.szTip, L"JoyCon2 Connector");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_trayIconCreated = true;
}

static void RemoveTrayIcon() {
    if (!g_trayIconCreated) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayIconCreated = false;
}

static void RestoreFromTray(HWND hwnd) {
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    // If window was minimized before hiding, restore it
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
}

static void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // 1. Show Main Window
    std::wstring showText = Utf8ToWide(T("tray_show"));
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SHOW, showText.c_str());

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 2. Connected controllers list
    auto& pm = PlayerManager::Instance();
    int globalIdx = 0;
    bool hasAnyPlayer = false;

    // Single Joy-Con players
    for (int i = 0; i < (int)pm.GetSinglePlayers().size(); ++i) {
        hasAnyPlayer = true;
        std::string label = std::string(T("dash_player")) + " " + std::to_string(globalIdx + 1) + " - " + T("type_single_joycon");
        std::wstring wLabel = Utf8ToWide(label.c_str());

        HMENU hSub = CreatePopupMenu();
        std::wstring dcText = Utf8ToWide(T("tray_disconnect"));
        AppendMenuW(hSub, MF_STRING, IDM_TRAY_DISCONNECT_BASE + globalIdx, dcText.c_str());
        AppendMenuW(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hSub, wLabel.c_str());
        globalIdx++;
    }

    // Dual Joy-Con players
    for (int i = 0; i < (int)pm.GetDualPlayers().size(); ++i) {
        hasAnyPlayer = true;
        int singleCount = (int)pm.GetSinglePlayers().size();
        std::string label = std::string(T("dash_player")) + " " + std::to_string(globalIdx + 1) + " - " + T("type_dual_joycon");
        std::wstring wLabel = Utf8ToWide(label.c_str());

        HMENU hSub = CreatePopupMenu();
        std::wstring dcText = Utf8ToWide(T("tray_disconnect"));
        AppendMenuW(hSub, MF_STRING, IDM_TRAY_DISCONNECT_BASE + globalIdx, dcText.c_str());
        AppendMenuW(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hSub, wLabel.c_str());
        globalIdx++;
    }

    // Pro / NSO GC players
    for (int i = 0; i < (int)pm.GetProPlayers().size(); ++i) {
        hasAnyPlayer = true;
        const auto& pp = pm.GetProPlayers()[i];
        const char* typeName = (pp.type == ControllerType::NSOGCController) ? T("type_nso_gc") : T("type_pro");
        std::string label = std::string(T("dash_player")) + " " + std::to_string(globalIdx + 1) + " - " + typeName;
        std::wstring wLabel = Utf8ToWide(label.c_str());

        HMENU hSub = CreatePopupMenu();
        std::wstring dcText = Utf8ToWide(T("tray_disconnect"));
        AppendMenuW(hSub, MF_STRING, IDM_TRAY_DISCONNECT_BASE + globalIdx, dcText.c_str());
        AppendMenuW(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hSub, wLabel.c_str());
        globalIdx++;
    }

    if (!hasAnyPlayer) {
        std::wstring noDevText = Utf8ToWide(T("tray_no_device"));
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, noDevText.c_str());
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 3. Exit
    std::wstring exitText = Utf8ToWide(T("tray_exit"));
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, exitText.c_str());

    // Required for proper menu dismiss behaviour
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessage(hwnd, WM_NULL, 0, 0);  // Ensure menu dismisses properly
    DestroyMenu(hMenu);
}
void CleanupRenderTarget();
bool LoadTextureFromMemory(ID3D11Device* pd3dDevice);
void LoadFonts(ImGuiIO& io, float dpiScale);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Helper: scaled value — delegates to UITheme which is set from g_dpiScale
// (S() is already defined in UI_Pages.h via UITheme::S)

// Active page
static int g_activePage = 0;  // 0=Dashboard, 1=AddDevice, 2=LayoutMgr, 3=MouseSettings

// Sidebar navigation items
struct NavItem {
    const char* labelKey;
};

static NavItem g_navItems[] = {
    { "nav_dashboard" },
    { "nav_add_device" },
    { "nav_layout_mgr" },
    { "nav_mouse_settings" },
    { "nav_settings" },
};
static const int NAV_COUNT = 5;

// Windows system font candidates
static const char* FONT_CJK_CANDIDATES[] = { "msyh.ttc", "msyhbd.ttc", "simsun.ttc", "malgun.ttf" };

bool LoadTextureFromMemory(ID3D11Device* pd3dDevice)
{
    // Create D3D11 Texture description
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = icon_width;
    desc.Height = icon_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    // Point to raw pixel data
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = icon_pixels;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;

    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);
    if (FAILED(hr)) return false;

    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    
    hr = pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, &g_pIconTextureView);
    pTexture->Release();
    
    return SUCCEEDED(hr);
}

// ---------- Joy-Con Icon drawing ----------
void DrawJoyConIcon(ImDrawList* dl, ImVec2 pos, float size) {
    // Stylized pair of Joy-Cons (Left + Right) as app icon
    float h   = size;                // total height
    float jw  = size * 0.38f;        // width of each Joy-Con body
    float gap = size * 0.08f;        // gap between L and R
    float totalW = jw * 2 + gap;
    float ox  = pos.x + (size - totalW) * 0.5f; // center horizontally in square

    // Round radii
    float rTop = jw * 0.50f;         // top fully rounded (pill ends)
    float rBot = jw * 0.30f;         // bottom a bit less rounded

    // Colors
    ImU32 colL    = IM_COL32(0x00, 0xC8, 0xD6, 255);  // neon blue  (Nintendo left)
    ImU32 colR    = IM_COL32(0xFF, 0x32, 0x32, 255);  // neon red   (Nintendo right)
    ImU32 colRail = IM_COL32(0x22, 0x22, 0x22, 200);  // dark rail
    ImU32 white   = IM_COL32(255, 255, 255, 220);

    float lx = ox;
    float rx = ox + jw + gap;

    // --- Left Joy-Con body ---
    dl->AddRectFilled(ImVec2(lx, pos.y), ImVec2(lx + jw, pos.y + h), colL, rTop);
    // Inner rail (dark stripe on right side of left JC)
    float railW = jw * 0.10f;
    dl->AddRectFilled(
        ImVec2(lx + jw - railW, pos.y + rTop * 0.4f),
        ImVec2(lx + jw,         pos.y + h - rBot * 0.4f),
        colRail, railW * 0.3f);

    // Left analog stick
    float stickR  = jw * 0.20f;
    float stickCx = lx + (jw - railW) * 0.5f;
    float stickCy = pos.y + h * 0.28f;
    dl->AddCircleFilled(ImVec2(stickCx, stickCy), stickR, IM_COL32(0x00, 0x96, 0xA0, 255));
    dl->AddCircleFilled(ImVec2(stickCx, stickCy), stickR * 0.55f, white);

    // D-pad (cross shape)
    float cx = stickCx;
    float cy = pos.y + h * 0.56f;
    float cArm = jw * 0.12f;   // arm length
    float cThk = jw * 0.09f;   // arm thickness
    dl->AddRectFilled(ImVec2(cx - cArm, cy - cThk), ImVec2(cx + cArm, cy + cThk), white, cThk * 0.3f);
    dl->AddRectFilled(ImVec2(cx - cThk, cy - cArm), ImVec2(cx + cThk, cy + cArm), white, cThk * 0.3f);

    // Minus button (small horizontal line)
    float minusY = pos.y + h * 0.14f;
    dl->AddLine(ImVec2(cx - jw * 0.06f, minusY), ImVec2(cx + jw * 0.06f, minusY),
        white, size * 0.02f > 0.5f ? size * 0.02f : 0.5f);

    // SR/SL small bumps on outer left edge
    float bumpW = jw * 0.05f;
    float bumpH = h * 0.06f;
    dl->AddRectFilled(
        ImVec2(lx - bumpW * 0.3f, pos.y + h * 0.35f),
        ImVec2(lx + bumpW,        pos.y + h * 0.35f + bumpH),
        colL, bumpW * 0.4f);
    dl->AddRectFilled(
        ImVec2(lx - bumpW * 0.3f, pos.y + h * 0.48f),
        ImVec2(lx + bumpW,        pos.y + h * 0.48f + bumpH),
        colL, bumpW * 0.4f);

    // --- Right Joy-Con body ---
    dl->AddRectFilled(ImVec2(rx, pos.y), ImVec2(rx + jw, pos.y + h), colR, rTop);
    // Inner rail (dark stripe on left side of right JC)
    dl->AddRectFilled(
        ImVec2(rx,          pos.y + rTop * 0.4f),
        ImVec2(rx + railW,  pos.y + h - rBot * 0.4f),
        colRail, railW * 0.3f);

    // Right analog stick (lower position — mirrors real layout)
    float rstickCx = rx + railW + (jw - railW) * 0.5f;
    float rstickCy = pos.y + h * 0.56f;
    dl->AddCircleFilled(ImVec2(rstickCx, rstickCy), stickR, IM_COL32(0xC8, 0x28, 0x28, 255));
    dl->AddCircleFilled(ImVec2(rstickCx, rstickCy), stickR * 0.55f, white);

    // Face buttons (4 circles in diamond — A B X Y)
    float fbCx = rstickCx;
    float fbCy = pos.y + h * 0.28f;
    float fbOff = jw * 0.13f;
    float fbR   = jw * 0.06f;
    dl->AddCircleFilled(ImVec2(fbCx,        fbCy - fbOff), fbR, white); // X (top)
    dl->AddCircleFilled(ImVec2(fbCx,        fbCy + fbOff), fbR, white); // B (bottom)
    dl->AddCircleFilled(ImVec2(fbCx - fbOff, fbCy),        fbR, white); // Y (left)
    dl->AddCircleFilled(ImVec2(fbCx + fbOff, fbCy),        fbR, white); // A (right)

    // Plus button (small cross)
    float plusY = pos.y + h * 0.14f;
    float pLen = jw * 0.05f;
    float pThk = size * 0.02f > 0.5f ? size * 0.02f : 0.5f;
    dl->AddLine(ImVec2(fbCx - pLen, plusY), ImVec2(fbCx + pLen, plusY), white, pThk);
    dl->AddLine(ImVec2(fbCx, plusY - pLen), ImVec2(fbCx, plusY + pLen), white, pThk);

    // Shoulder buttons (L/R bumps at very top)
    float shW   = jw * 0.55f;
    float shH   = h * 0.04f;
    float shR   = shH * 0.5f;
    float shTopY = pos.y - shH * 0.15f;
    // L shoulder
    dl->AddRectFilled(
        ImVec2(lx + jw * 0.10f, shTopY),
        ImVec2(lx + jw * 0.10f + shW, shTopY + shH),
        IM_COL32(0x00, 0xA0, 0xAA, 255), shR);
    // R shoulder
    dl->AddRectFilled(
        ImVec2(rx + jw * 0.35f, shTopY),
        ImVec2(rx + jw * 0.35f + shW, shTopY + shH),
        IM_COL32(0xCC, 0x28, 0x28, 255), shR);
}

// ---------- Title Bar rendering (frameless window) ----------
void RenderTitleBar() {
    float titleH = S(TITLE_BAR_HEIGHT_DP);
    float windowW = ImGui::GetWindowWidth();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wPos = ImGui::GetWindowPos();

    // Background
    dl->AddRectFilled(wPos, ImVec2(wPos.x + windowW, wPos.y + titleH),
        ImGui::GetColorU32(UITheme::Sidebar));
    // Bottom separator line
    dl->AddLine(ImVec2(wPos.x, wPos.y + titleH - 1),
        ImVec2(wPos.x + windowW, wPos.y + titleH - 1),
        ImGui::GetColorU32(UITheme::Divider));

    // App icon (Joy-Con pair)
    float iconSz = S(22);
    float iconY = (titleH - iconSz) * 0.5f;
    ImVec2 pos(wPos.x + S(10), wPos.y + iconY);
    if (g_pIconTextureView) {
        dl->AddImage((ImTextureID)g_pIconTextureView, pos, ImVec2(pos.x + iconSz, pos.y + iconSz));
    } else {
        DrawJoyConIcon(dl, pos, iconSz);
    }

    // Title text
    const char* title = "JoyCon2 Connector";
    float textY = wPos.y + (titleH - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(wPos.x + S(38), textY),
        ImGui::GetColorU32(UITheme::TextSecondary), title);

    // Window control buttons (right side)
    float btnW = S(46);
    float btnH = titleH;
    float btnStartX = wPos.x + windowW - btnW * 3;

    // --- Minimize ---
    {
        ImVec2 bMin(btnStartX, wPos.y);
        ImVec2 bMax(btnStartX + btnW, wPos.y + btnH);
        bool hovered = ImGui::IsMouseHoveringRect(bMin, bMax);
        if (hovered)
            dl->AddRectFilled(bMin, bMax, IM_COL32(0, 0, 0, 26));
        float cy = wPos.y + btnH * 0.5f;
        float cx = btnStartX + btnW * 0.5f;
        dl->AddLine(ImVec2(cx - S(5), cy), ImVec2(cx + S(5), cy),
            ImGui::GetColorU32(UITheme::TextPrimary), S(1.0f));
        ImGui::SetCursorScreenPos(bMin);
        if (ImGui::InvisibleButton("##titlemin", ImVec2(btnW, btnH)))
            ShowWindow(g_hwnd, SW_MINIMIZE);
    }

    // --- Maximize / Restore ---
    {
        float bx = btnStartX + btnW;
        ImVec2 bMin(bx, wPos.y);
        ImVec2 bMax(bx + btnW, wPos.y + btnH);
        bool hovered = ImGui::IsMouseHoveringRect(bMin, bMax);
        if (hovered)
            dl->AddRectFilled(bMin, bMax, IM_COL32(0, 0, 0, 26));
        float cx = bx + btnW * 0.5f;
        float cy = wPos.y + btnH * 0.5f;
        float sz = S(4.5f);
        bool maximized = IsZoomed(g_hwnd);
        if (maximized) {
            float off = S(1.5f);
            dl->AddRect(ImVec2(cx - sz + off, cy - sz - off), ImVec2(cx + sz + off, cy + sz - off),
                ImGui::GetColorU32(UITheme::TextPrimary), 0, 0, S(1.0f));
            dl->AddRectFilled(ImVec2(cx - sz - off, cy - sz + off), ImVec2(cx + sz - off, cy + sz + off),
                ImGui::GetColorU32(UITheme::Sidebar));
            dl->AddRect(ImVec2(cx - sz - off, cy - sz + off), ImVec2(cx + sz - off, cy + sz + off),
                ImGui::GetColorU32(UITheme::TextPrimary), 0, 0, S(1.0f));
        } else {
            dl->AddRect(ImVec2(cx - sz, cy - sz), ImVec2(cx + sz, cy + sz),
                ImGui::GetColorU32(UITheme::TextPrimary), 0, 0, S(1.0f));
        }
        ImGui::SetCursorScreenPos(bMin);
        if (ImGui::InvisibleButton("##titlemax", ImVec2(btnW, btnH)))
            ShowWindow(g_hwnd, maximized ? SW_RESTORE : SW_MAXIMIZE);
    }

    // --- Close ---
    {
        float bx = btnStartX + btnW * 2;
        ImVec2 bMin(bx, wPos.y);
        ImVec2 bMax(bx + btnW, wPos.y + btnH);
        bool hovered = ImGui::IsMouseHoveringRect(bMin, bMax);
        if (hovered)
            dl->AddRectFilled(bMin, bMax, IM_COL32(0xC4, 0x2B, 0x1C, 0xFF));
        float cx = bx + btnW * 0.5f;
        float cy = wPos.y + btnH * 0.5f;
        float xsz = S(5);
        ImU32 xCol = hovered ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(UITheme::TextPrimary);
        dl->AddLine(ImVec2(cx - xsz, cy - xsz), ImVec2(cx + xsz, cy + xsz), xCol, S(1.0f));
        dl->AddLine(ImVec2(cx + xsz, cy - xsz), ImVec2(cx - xsz, cy + xsz), xCol, S(1.0f));
        ImGui::SetCursorScreenPos(bMin);
        if (ImGui::InvisibleButton("##titleclose", ImVec2(btnW, btnH)))
            PostMessage(g_hwnd, WM_CLOSE, 0, 0);
    }
}

// ---------- Sidebar rendering ----------
void RenderSidebar() {
    float titleH = S(TITLE_BAR_HEIGHT_DP);
    float sidebarW = S(220);
    ImGui::SetCursorPos(ImVec2(0, titleH));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, UITheme::Sidebar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("Sidebar", ImVec2(sidebarW, ImGui::GetWindowHeight() - titleH), ImGuiChildFlags_None);

    // App title
    ImGui::SetCursorPos(ImVec2(S(20), S(16)));
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
    ImGui::TextColored(UITheme::Primary, "JoyCon2");
    if (ImGui::GetIO().Fonts->Fonts.Size > 1) ImGui::PopFont();
    ImGui::SetCursorPosX(S(20));
    ImGui::TextColored(UITheme::TextTertiary, "Connector");

    ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();

    // Navigation items
    for (int i = 0; i < NAV_COUNT; ++i) {
        ImGui::PushID(i);
        bool selected = (g_activePage == i);

        float itemH = S(44);
        float padLeft = S(12);
        float padRight = S(12);
        float totalW = sidebarW - padLeft - padRight;

        ImGui::SetCursorPosX(padLeft);
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();

        // Background
        ImVec4 bgColor = selected ? UITheme::SidebarActive : ImVec4(0, 0, 0, 0);
        if (!selected && ImGui::IsMouseHoveringRect(cursorPos, ImVec2(cursorPos.x + totalW, cursorPos.y + itemH)))
            bgColor = UITheme::SidebarHover;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(
            ImVec2(cursorPos.x, cursorPos.y),
            ImVec2(cursorPos.x + totalW, cursorPos.y + itemH),
            ImGui::GetColorU32(bgColor), S(22));

        // Active indicator bar
        if (selected) {
            dl->AddRectFilled(
                ImVec2(cursorPos.x, cursorPos.y + S(8)),
                ImVec2(cursorPos.x + S(4), cursorPos.y + itemH - S(8)),
                ImGui::GetColorU32(UITheme::Primary), S(2));
        }

        // Text
        ImVec4 textColor = selected ? UITheme::Primary : UITheme::SidebarText;
        float textY = cursorPos.y + (itemH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(cursorPos.x + S(20), textY), ImGui::GetColorU32(textColor), T(g_navItems[i].labelKey));

        // Click
        ImGui::SetCursorPosX(padLeft);
        if (ImGui::InvisibleButton("navbtn", ImVec2(totalW, itemH))) {
            g_activePage = i;
            if (i == 1) g_wizard.Reset();
        }
        ImGui::PopID();
    }

    // Bottom: Version display
    float bottomY = ImGui::GetWindowHeight() - S(40);
    ImGui::SetCursorPos(ImVec2(S(20), bottomY));
    ImGui::TextColored(UITheme::TextTertiary, "v%s", APP_VERSION);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------- Font loading helper ----------
void LoadFonts(ImGuiIO& io, float dpiScale) {
    // Glyph ranges covering Latin + CJK + symbols
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, // Basic Latin
        0x2000, 0x206F, // General Punctuation
        0x2190, 0x21FF, // Arrows (contains ⇄ U+21C4)
        0x3000, 0x30FF, // CJK Symbols, Hiragana, Katakana
        0x31F0, 0x31FF, // Katakana Extension
        0x4E00, 0x9FFF, // CJK Unified Ideographs
        0xFF00, 0xFFEF, // Halfwidth and Fullwidth Forms
        0x2600, 0x26FF, // Miscellaneous Symbols (contains ⚙ U+2699)
        0x2700, 0x27BF, // Dingbats
        0xFE00, 0xFE0F, // Variation Selectors
        0,
    };
    // Latin-only subset for Segoe UI base font
    static const ImWchar latinRanges[] = {
        0x0020, 0x00FF, // Basic Latin
        0x2000, 0x206F, // General Punctuation
        0x2190, 0x21FF, // Arrows (contains ⇄ U+21C4)
        0x2600, 0x26FF, // Miscellaneous Symbols (contains ⚙ U+2699)
        0x2700, 0x27BF, // Dingbats
        0xFE00, 0xFE0F, // Variation Selectors
        0,
    };
    // Symbol ranges for Segoe UI Symbol fallback (⚙ ⇄ etc.)
    static const ImWchar symbolRanges[] = {
        0x2190, 0x21FF, // Arrows
        0x2600, 0x26FF, // Miscellaneous Symbols
        0x2700, 0x27BF, // Dingbats
        0,
    };

    const float sizes[] = { 16.0f, 22.0f };
    bool fontLoaded = false;

    // Resolve Segoe UI Symbol path once for symbol fallback
    std::string symPath = GetSystemFontPath("seguisym.ttf");

    // Strategy 1: Microsoft YaHei covers both Latin and CJK in one font
    std::string msyhPath = GetSystemFontPath("msyh.ttc");
    if (!msyhPath.empty()) {
        for (float sz : sizes) {
            ImFontConfig cfg;
            cfg.OversampleH = 2;
            cfg.OversampleV = 1;
            io.Fonts->AddFontFromFileTTF(msyhPath.c_str(), sz * dpiScale, &cfg, ranges);
            // Merge Segoe UI Symbol for reliable symbol coverage (⚙ ⇄)
            if (!symPath.empty()) {
                ImFontConfig symCfg;
                symCfg.MergeMode = true;
                symCfg.OversampleH = 2;
                symCfg.OversampleV = 1;
                io.Fonts->AddFontFromFileTTF(symPath.c_str(), sz * dpiScale, &symCfg, symbolRanges);
            }
        }
        fontLoaded = true;
    }

    // Strategy 2: Segoe UI (Latin) + merge a CJK font
    if (!fontLoaded) {
        std::string seguiPath = GetSystemFontPath("segoeui.ttf");
        std::string cjkPath;
        for (const char* candidate : FONT_CJK_CANDIDATES) {
            cjkPath = GetSystemFontPath(candidate);
            if (!cjkPath.empty()) break;
        }

        if (!seguiPath.empty()) {
            for (float sz : sizes) {
                ImFontConfig cfg;
                cfg.OversampleH = 2;
                cfg.OversampleV = 1;
                io.Fonts->AddFontFromFileTTF(seguiPath.c_str(), sz * dpiScale, &cfg,
                                             cjkPath.empty() ? ranges : latinRanges);
                if (!cjkPath.empty()) {
                    ImFontConfig mergeCfg;
                    mergeCfg.MergeMode = true;
                    mergeCfg.OversampleH = 2;
                    mergeCfg.OversampleV = 1;
                    io.Fonts->AddFontFromFileTTF(cjkPath.c_str(), sz * dpiScale, &mergeCfg, ranges);
                }
                // Merge Segoe UI Symbol for reliable symbol coverage (⚙ ⇄)
                if (!symPath.empty()) {
                    ImFontConfig symCfg;
                    symCfg.MergeMode = true;
                    symCfg.OversampleH = 2;
                    symCfg.OversampleV = 1;
                    io.Fonts->AddFontFromFileTTF(symPath.c_str(), sz * dpiScale, &symCfg, symbolRanges);
                }
            }
            fontLoaded = true;
        }
    }

    // Strategy 3: Any available CJK font as a standalone fallback
    if (!fontLoaded) {
        std::string cjkPath;
        for (const char* candidate : FONT_CJK_CANDIDATES) {
            cjkPath = GetSystemFontPath(candidate);
            if (!cjkPath.empty()) break;
        }
        if (!cjkPath.empty()) {
            for (float sz : sizes) {
                ImFontConfig cfg;
                cfg.OversampleH = 2;
                cfg.OversampleV = 1;
                io.Fonts->AddFontFromFileTTF(cjkPath.c_str(), sz * dpiScale, &cfg, ranges);
                // Merge Segoe UI Symbol for reliable symbol coverage (⚙ ⇄)
                if (!symPath.empty()) {
                    ImFontConfig symCfg;
                    symCfg.MergeMode = true;
                    symCfg.OversampleH = 2;
                    symCfg.OversampleV = 1;
                    io.Fonts->AddFontFromFileTTF(symPath.c_str(), sz * dpiScale, &symCfg, symbolRanges);
                }
            }
            fontLoaded = true;
        }
    }

    // Strategy 4: ImGui built-in font (no CJK support)
    if (!fontLoaded) {
        io.Fonts->AddFontDefault();
        io.Fonts->AddFontDefault();
    }

    io.Fonts->Build();
}

// ---------- Main entry ----------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    winrt::init_apartment();

    // Enable Per-Monitor DPI Awareness V2
    ImGui_ImplWin32_EnableDpiAwareness();

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    wc.lpszClassName = L"JoyCon2ConnectorClass";
    RegisterClassExW(&wc);

    // Get initial DPI for sizing
    // Use the primary monitor DPI for initial window placement
    HDC hdc = GetDC(NULL);
    float initDpi = (float)GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, hdc);
    g_dpiScale = initDpi;
    UITheme::DpiScale = g_dpiScale;

    int initW = (int)(960 * g_dpiScale);
    int initH = (int)(640 * g_dpiScale);

    // Frameless window: WS_POPUP with resize border support
    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"JoyCon2 Connector",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, initW, initH,
        NULL, NULL, hInstance, NULL);
    g_hwnd = hwnd;

    // Enable DWM shadow for frameless window
    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    // Update DPI from the actual window's monitor
    g_dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    UITheme::DpiScale = g_dpiScale;

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    // Load fonts at DPI-scaled sizes
    LoadFonts(io, g_dpiScale);

    // Init platform/renderer
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Initialize ImGui custom icon texture
    LoadTextureFromMemory(g_pd3dDevice);

    // Apply theme
    UITheme::Apply();

    // Increase system timer resolution to 1ms for responsive input
    timeBeginPeriod(1);

    // Init managers
    ViGEmManager::Instance().Initialize();
    ConfigManager::Instance().Load();
    ConfigManager::Instance().EnsureDefaults();

    // Initialize language: load from embedded data, then select from config or detect
    {
        I18nManager::Instance().InitFromEmbedded();

        auto& lang = ConfigManager::Instance().config.language;
        // Backward compatibility: map old short codes to new locale codes
        if (lang == "en") lang = "en_us";
        else if (lang == "zh") lang = "zh_cn";

        if (lang.empty() || !I18nManager::Instance().LoadLanguage(lang)) {
            // First launch or unknown locale: detect from system
            std::string detected = DetectSystemLanguage();
            if (!I18nManager::Instance().LoadLanguage(detected)) {
                // Fallback: try to load any available language
                const auto& langs = I18nManager::Instance().GetAvailableLanguages();
                if (!langs.empty()) {
                    detected = langs[0].locale;
                    I18nManager::Instance().LoadLanguage(detected);
                }
            }
            lang = detected;
            ConfigManager::Instance().Save();
        }
    }

    // Clear color
    float clearColor[4] = { 0.96f, 0.94f, 0.92f, 1.0f };

    // Main loop
    bool running = true;
    MSG msg;
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        // When window is hidden (minimized to tray), skip rendering to save CPU
        if (!IsWindowVisible(g_hwnd)) {
            Sleep(100);
            continue;
        }

        // Handle DPI change — rebuild fonts
        if (g_fontRebuildNeeded) {
            g_dpiScale = g_pendingDpiScale;
            UITheme::DpiScale = g_dpiScale;

            ImGui_ImplDX11_InvalidateDeviceObjects();
            io.Fonts->Clear();
            LoadFonts(io, g_dpiScale);
            ImGui_ImplDX11_CreateDeviceObjects();

            UITheme::Apply();
            g_fontRebuildNeeded = false;
        }

        // Handle resize
        if (g_resizeRequested) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
            g_resizeRequested = false;
        }

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Full-window ImGui panel
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("MainWindow", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();

        // Custom title bar
        RenderTitleBar();

        // Layout: Sidebar + Content (below title bar)
        float titleH = S(TITLE_BAR_HEIGHT_DP);
        RenderSidebar();
        ImGui::SameLine();

        // Content area
        ImGui::SetCursorPosY(titleH);
        ImGui::BeginChild("ContentArea", ImVec2(0, ImGui::GetWindowHeight() - titleH), ImGuiChildFlags_None);
        switch (g_activePage) {
        case 0: RenderDashboard(); break;
        case 1: RenderAddDevice(g_activePage); break;
        case 2: RenderLayoutManager(); break;
        case 3: RenderMouseSettings(); break;
        case 4: RenderSettings(); break;
        }
        ImGui::EndChild();

        ImGui::End();

        // Render
        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    RemoveTrayIcon();
    PlayerManager::Instance().Shutdown();
    ViGEmManager::Instance().Shutdown();
    ConfigManager::Instance().Save();
    timeEndPeriod(1);

    if (g_pIconTextureView) {
        g_pIconTextureView->Release();
        g_pIconTextureView = nullptr;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);

    return 0;
}

// ---------- D3D11 Helpers ----------
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_resizeW = (UINT)LOWORD(lParam);
            g_resizeH = (UINT)HIWORD(lParam);
            g_resizeRequested = true;
        }
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        if (!g_forceQuit && ConfigManager::Instance().config.minimizeToTray) {
            // Minimize to tray instead of closing
            CreateTrayIcon(hWnd);
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        }
        // Normal close
        RemoveTrayIcon();
        DestroyWindow(hWnd);
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu(hWnd);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            RestoreFromTray(hWnd);
        }
        return 0;

    case WM_COMMAND: {
        WORD cmdId = LOWORD(wParam);
        if (cmdId == IDM_TRAY_SHOW) {
            RestoreFromTray(hWnd);
        } else if (cmdId == IDM_TRAY_EXIT) {
            g_forceQuit = true;
            PostMessage(hWnd, WM_CLOSE, 0, 0);
        } else if (cmdId >= IDM_TRAY_DISCONNECT_BASE && cmdId < IDM_TRAY_DISCONNECT_BASE + 100) {
            int globalIdx = cmdId - IDM_TRAY_DISCONNECT_BASE;
            PlayerManager::Instance().RemovePlayerByGlobalIndex(globalIdx);
        }
        return 0;
    }

    case WM_NCCALCSIZE: {
        // Remove the standard window frame entirely
        if (wParam == TRUE) {
            NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lParam;
            if (IsZoomed(hWnd)) {
                // When maximized, adjust to work area to avoid extending past screen
                HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = { sizeof(mi) };
                GetMonitorInfo(hMon, &mi);
                params->rgrc[0] = mi.rcWork;
            }
            return 0;
        }
        break;
    }

    case WM_NCHITTEST: {
        // Custom hit testing for frameless window
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);

        RECT rc;
        GetClientRect(hWnd, &rc);

        int borderW = (int)S(5);
        float titleH = S(TITLE_BAR_HEIGHT_DP);
        float btnRegionW = S(46 * 3);

        // Don't handle resize borders when maximized
        if (!IsZoomed(hWnd)) {
            if (pt.y < borderW) {
                if (pt.x < borderW) return HTTOPLEFT;
                if (pt.x > rc.right - borderW) return HTTOPRIGHT;
                return HTTOP;
            }
            if (pt.y > rc.bottom - borderW) {
                if (pt.x < borderW) return HTBOTTOMLEFT;
                if (pt.x > rc.right - borderW) return HTBOTTOMRIGHT;
                return HTBOTTOM;
            }
            if (pt.x < borderW) return HTLEFT;
            if (pt.x > rc.right - borderW) return HTRIGHT;
        }

        // Title bar area
        if (pt.y < (int)titleH) {
            // Window control buttons region → let ImGui handle
            if (pt.x > rc.right - (int)btnRegionW)
                return HTCLIENT;
            // Rest of title bar → draggable
            return HTCAPTION;
        }

        return HTCLIENT;
    }

    case WM_DPICHANGED: {
        float newDpi = HIWORD(wParam) / 96.0f;
        g_pendingDpiScale = newDpi;
        g_fontRebuildNeeded = true;
        RECT* suggested = (RECT*)lParam;
        SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        LPMINMAXINFO mmi = (LPMINMAXINFO)lParam;
        mmi->ptMinTrackSize.x = (LONG)S(700);
        mmi->ptMinTrackSize.y = (LONG)S(450);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
