#pragma once
// UI Pages - Dashboard, Add Device, Layout Manager, Mouse Settings, Settings
#include "imgui/imgui.h"
#include "i18n.h"
#include "ConfigManager.h"
#include "PlayerManager.h"
#include "DeviceManager.h"
#include "ViGEmManager.h"
#include "UI_Theme.h"
#include "UpdateChecker.h"
#include "version.h"
#include <string>
#include <cmath>
#include <algorithm>
#include "imgui_internal.h"

// DPI-scaled pixel helper (delegates to UITheme::S)
inline float S(float v) { return UITheme::S(v); }

// Current wizard state for Add Device
struct AddDeviceWizard {
    int step = 0;  // 0 = select type, 1 = configure, 2 = scanning, 3 = dual right success, 4 = dual left scan
    ControllerType selectedType = ControllerType::SingleJoyCon;
    JoyConSide selectedSide = JoyConSide::Left;
    JoyConOrientation selectedOrientation = JoyConOrientation::Upright;
    GyroSource selectedGyro = GyroSource::Both;
    bool scanStarted = false;
    float scanTimer = 0.0f;
    std::string statusMessage;
    bool dualFirstDone = false;

    void Reset() {
        step = 0; scanStarted = false; scanTimer = 0.0f;
        statusMessage.clear(); dualFirstDone = false;
        PlayerManager::Instance().ClearPendingDual();  // Release pending right Joy-Con BLE reference
    }
};

inline AddDeviceWizard g_wizard;
inline int g_selectedLayoutIndex = 0;
inline char g_layoutNameBuf[128] = {};
inline bool g_renamingLayout = false;

// Xbox emulation warning popup state
static bool g_xboxWarningPopupOpen = false;
static uint64_t g_xboxWarningBleAddress = 0;  // BLE address of controller being toggled
static int g_xboxWarningPlayerType = 0;  // 0=single, 1=dual, 2=pro
static int g_xboxWarningPlayerIndex = 0; // index within the player type vector

// Helper: Draw a card background with built-in padding
inline void BeginCard(float width = 0, float minHeight = 0) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, UITheme::SurfaceCard);
    ImGui::PushStyleColor(ImGuiCol_Border, UITheme::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), S(12)));
    if (width <= 0) width = ImGui::GetContentRegionAvail().x;
    float h = (minHeight > 0) ? minHeight : 0;
    ImGui::BeginChild(ImGui::GetID("card"), ImVec2(width, h), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
}

inline void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

// Helper: Draw a section label
inline void SectionLabel(const char* text) {
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
    ImGui::TextColored(UITheme::TextSecondary, "%s", text);
    if (ImGui::GetIO().Fonts->Fonts.Size > 1) ImGui::PopFont();
    ImGui::Spacing();
}

// Helper: Get MD3 button size (height = 40dp, min width = 88dp)
inline ImVec2 MD3ButtonSize(ImVec2 requested = ImVec2(0, 0)) {
    float h = (requested.y > 0) ? requested.y : S(40);
    float w = requested.x; // 0 = auto-fit
    return ImVec2(w, h);
}

// Helper: Primary button (MD3 Filled Button)
inline bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    size = MD3ButtonSize(size);
    ImGui::PushStyleColor(ImGuiCol_Button, UITheme::Primary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::PrimaryHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::PrimaryActive);
    ImGui::PushStyleColor(ImGuiCol_Text, UITheme::OnPrimary);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(24), S(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(20));
    bool result = ImGui::Button(label, size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return result;
}

// Helper: Secondary button (MD3 Tonal Button)
inline bool SecondaryButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    size = MD3ButtonSize(size);
    ImGui::PushStyleColor(ImGuiCol_Button, UITheme::ButtonSecondary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::ButtonSecondaryHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::SurfaceDim);
    ImGui::PushStyleColor(ImGuiCol_Text, UITheme::TextPrimary);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(24), S(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(20));
    bool result = ImGui::Button(label, size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return result;
}

// Helper: Danger button (MD3 Outlined/Error)
inline bool DangerButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    size = MD3ButtonSize(size);
    ImGui::PushStyleColor(ImGuiCol_Button, UITheme::ButtonDanger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::ButtonDangerHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::Error);
    ImGui::PushStyleColor(ImGuiCol_Text, UITheme::Error);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(24), S(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(20));
    bool result = ImGui::Button(label, size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return result;
}

// Helper: Status chip
inline void StatusChip(const char* label, ImVec4 bg, ImVec4 textColor) {
    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float pad = S(8);
    float chipH = textSize.y + pad * 2;
    float chipW = textSize.x + pad * 3;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + chipW, pos.y + chipH), ImGui::GetColorU32(bg), chipH * 0.5f);
    float dotR = S(4);
    dl->AddCircleFilled(ImVec2(pos.x + pad + dotR, pos.y + chipH * 0.5f), dotR, ImGui::GetColorU32(textColor));
    dl->AddText(ImVec2(pos.x + pad + dotR * 2 + S(6), pos.y + pad), ImGui::GetColorU32(textColor), label);
    ImGui::Dummy(ImVec2(chipW, chipH));
}

// Helper: Icon button (gear/settings) - MD3 tonal icon button
inline bool IconButton(const char* id, float size = 0) {
    if (size <= 0) size = S(40);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, UITheme::ButtonSecondary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::ButtonSecondaryHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::SurfaceDim);
    ImGui::PushStyleColor(ImGuiCol_Text, UITheme::TextPrimary);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(12));
    bool result = ImGui::Button(u8"\u2699", ImVec2(size, size));  // ⚙ gear unicode
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    return result;
}

// Helper: Draw GitHub Invertocat mark icon — accurate SVG-path-based rendering.
// Draws a filled circle with the Octocat body carved out as a concave polygon.
// Body polygon points sampled from the official GitHub mark SVG path, normalized to [-1,1].
inline void DrawGitHubMark(ImDrawList* dl, ImVec2 center, float radius, ImU32 color) {
    // 78 body polygon points derived from the official GitHub Invertocat SVG path.
    // Coordinate system: origin at circle center, range [-1,1] maps to circle radius.
    static const float gh_body[][2] = {
        {-0.316f,  0.949f}, {-0.263f,  0.938f}, {-0.248f,  0.902f}, {-0.248f,  0.839f},
        {-0.248f,  0.733f}, {-0.390f,  0.742f}, {-0.487f,  0.712f}, {-0.546f,  0.663f},
        {-0.576f,  0.618f}, {-0.585f,  0.599f}, {-0.628f,  0.514f}, {-0.698f,  0.450f},
        {-0.728f,  0.397f}, {-0.691f,  0.389f}, {-0.602f,  0.423f}, {-0.538f,  0.493f},
        {-0.463f,  0.574f}, {-0.381f,  0.601f}, {-0.304f,  0.595f}, {-0.247f,  0.576f},
        {-0.227f,  0.503f}, {-0.183f,  0.442f}, {-0.250f,  0.432f}, {-0.316f,  0.418f},
        {-0.379f,  0.397f}, {-0.439f,  0.368f}, {-0.494f,  0.329f}, {-0.542f,  0.280f},
        {-0.582f,  0.219f}, {-0.613f,  0.144f}, {-0.632f,  0.054f}, {-0.639f, -0.052f},
        {-0.633f, -0.125f}, {-0.613f, -0.196f}, {-0.580f, -0.262f}, {-0.535f, -0.320f},
        {-0.555f, -0.409f}, {-0.551f, -0.499f}, {-0.526f, -0.585f}, {-0.510f, -0.587f},
        {-0.460f, -0.582f}, {-0.374f, -0.553f}, {-0.251f, -0.483f}, {-0.180f, -0.499f},
        {-0.109f, -0.510f}, {-0.037f, -0.516f}, { 0.036f, -0.516f}, { 0.108f, -0.510f},
        { 0.179f, -0.499f}, { 0.250f, -0.483f}, { 0.373f, -0.553f}, { 0.459f, -0.582f},
        { 0.509f, -0.587f}, { 0.525f, -0.585f}, { 0.550f, -0.499f}, { 0.554f, -0.409f},
        { 0.534f, -0.320f}, { 0.579f, -0.262f}, { 0.612f, -0.196f}, { 0.631f, -0.125f},
        { 0.637f, -0.052f}, { 0.630f,  0.054f}, { 0.611f,  0.144f}, { 0.581f,  0.219f},
        { 0.541f,  0.280f}, { 0.493f,  0.329f}, { 0.438f,  0.367f}, { 0.378f,  0.396f},
        { 0.314f,  0.417f}, { 0.248f,  0.432f}, { 0.181f,  0.441f}, { 0.235f,  0.527f},
        { 0.249f,  0.627f}, { 0.249f,  0.751f}, { 0.249f,  0.846f}, { 0.249f,  0.902f},
        { 0.264f,  0.942f}, { 0.318f,  0.948f},
    };
    static const int gh_body_count = sizeof(gh_body) / sizeof(gh_body[0]);

    // 1) Outer filled circle
    dl->AddCircleFilled(center, radius, color, 40);

    // 2) Build the body polygon in screen coordinates, then carve it out
    ImU32 bg = ImGui::GetColorU32(UITheme::SurfaceCard);
    ImVec2 pts[80];  // slightly larger than gh_body_count
    for (int i = 0; i < gh_body_count; i++) {
        pts[i] = ImVec2(center.x + gh_body[i][0] * radius,
                         center.y + gh_body[i][1] * radius);
    }
    dl->AddConcavePolyFilled(pts, gh_body_count, bg);
}

// Helper: Spinner animation
inline void Spinner(const char* label, float radius, float thickness, ImU32 color) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(radius * 2, (radius + ImGui::GetStyle().FramePadding.y) * 2);

    ImGui::Dummy(size);
    ImDrawList* dl = window->DrawList;

    float t = (float)ImGui::GetTime();
    int numSegments = 30;
    int start = (int)(fmod(t * 8.0f, (float)numSegments));

    float aMin = IM_PI * 2.0f * ((float)start) / (float)numSegments;
    float aMax = IM_PI * 2.0f * ((float)start + (float)(numSegments - 5)) / (float)numSegments;

    ImVec2 centre(pos.x + radius, pos.y + radius + ImGui::GetStyle().FramePadding.y);

    for (int i = 0; i < numSegments; i++) {
        float a0 = aMin + ((float)i / (float)numSegments) * (aMax - aMin);
        float a1 = aMin + ((float)(i + 1) / (float)numSegments) * (aMax - aMin);
        dl->AddLine(
            ImVec2(centre.x + cosf(a0) * radius, centre.y + sinf(a0) * radius),
            ImVec2(centre.x + cosf(a1) * radius, centre.y + sinf(a1) * radius),
            color, thickness);
    }
}

// ===== MD3-style controller icons (SVG-quality DrawList rendering) =====

inline void DrawControllerIcon(ControllerType type, ImVec2 pos, float scale, ImU32 color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float s = scale * UITheme::DpiScale;
    ImU32 bg = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.92f)); // cutout color
    ImU32 bgSoft = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.6f));

    switch (type) {
    case ControllerType::SingleJoyCon: {
        // Tall rounded pill — Joy-Con silhouette
        float w = 18 * s, h = 42 * s, r = 7 * s;
        dl->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + w, pos.y + h), color, r);
        // Side rail
        dl->AddRectFilled(ImVec2(pos.x, pos.y + 6 * s), ImVec2(pos.x + 2.5f * s, pos.y + h - 6 * s), bgSoft, 1 * s);
        // Analog stick
        dl->AddCircleFilled(ImVec2(pos.x + w * 0.5f, pos.y + 12 * s), 4.5f * s, bg);
        dl->AddCircleFilled(ImVec2(pos.x + w * 0.5f, pos.y + 12 * s), 2.5f * s, color);
        // Face buttons (4 dots in diamond)
        float bx = pos.x + w * 0.5f, by = pos.y + 26 * s;
        float bd = 3.5f * s, br = 1.5f * s;
        dl->AddCircleFilled(ImVec2(bx, by - bd), br, bg);       // top
        dl->AddCircleFilled(ImVec2(bx, by + bd), br, bg);       // bottom
        dl->AddCircleFilled(ImVec2(bx - bd, by), br, bg);       // left
        dl->AddCircleFilled(ImVec2(bx + bd, by), br, bg);       // right
        // SR/SL indicators
        dl->AddRectFilled(ImVec2(pos.x + w - 3 * s, pos.y + 14 * s), ImVec2(pos.x + w - 1 * s, pos.y + 18 * s), bgSoft, 0.5f * s);
        dl->AddRectFilled(ImVec2(pos.x + w - 3 * s, pos.y + 22 * s), ImVec2(pos.x + w - 1 * s, pos.y + 26 * s), bgSoft, 0.5f * s);
        break;
    }
    case ControllerType::DualJoyCon: {
        // Two Joy-Cons side by side
        float jw = 16 * s, jh = 40 * s, gap = 3 * s, jr = 6 * s;
        // Left Joy-Con
        dl->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + jw, pos.y + jh), color, jr);
        dl->AddCircleFilled(ImVec2(pos.x + jw * 0.5f, pos.y + 11 * s), 4 * s, bg);
        dl->AddCircleFilled(ImVec2(pos.x + jw * 0.5f, pos.y + 11 * s), 2 * s, color);
        // D-pad cross on left
        float dx = pos.x + jw * 0.5f, dy = pos.y + 25 * s;
        dl->AddRectFilled(ImVec2(dx - 1.2f * s, dy - 4 * s), ImVec2(dx + 1.2f * s, dy + 4 * s), bg, 0.5f * s);
        dl->AddRectFilled(ImVec2(dx - 4 * s, dy - 1.2f * s), ImVec2(dx + 4 * s, dy + 1.2f * s), bg, 0.5f * s);
        // Rail indicators
        dl->AddRectFilled(ImVec2(pos.x + jw - 2 * s, pos.y + 8 * s), ImVec2(pos.x + jw, pos.y + jh - 8 * s), bgSoft, 0.5f * s);

        // Right Joy-Con
        float rx = pos.x + jw + gap;
        dl->AddRectFilled(ImVec2(rx, pos.y), ImVec2(rx + jw, pos.y + jh), color, jr);
        // Face buttons (diamond)
        float fbx = rx + jw * 0.5f, fby = pos.y + 11 * s;
        float fbd = 3.5f * s, fbr = 1.5f * s;
        dl->AddCircleFilled(ImVec2(fbx, fby - fbd), fbr, bg);
        dl->AddCircleFilled(ImVec2(fbx, fby + fbd), fbr, bg);
        dl->AddCircleFilled(ImVec2(fbx - fbd, fby), fbr, bg);
        dl->AddCircleFilled(ImVec2(fbx + fbd, fby), fbr, bg);
        // Right stick
        dl->AddCircleFilled(ImVec2(rx + jw * 0.5f, pos.y + 25 * s), 4 * s, bg);
        dl->AddCircleFilled(ImVec2(rx + jw * 0.5f, pos.y + 25 * s), 2 * s, color);
        // Left rail
        dl->AddRectFilled(ImVec2(rx, pos.y + 8 * s), ImVec2(rx + 2 * s, pos.y + jh - 8 * s), bgSoft, 0.5f * s);
        break;
    }
    case ControllerType::ProController: {
        // Wide gamepad body with grips
        float bw = 44 * s, bh = 28 * s, br = 10 * s;
        float ox = pos.x + 2 * s, oy = pos.y + 4 * s;
        // Main body
        dl->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + bw, oy + bh), color, br);
        // Left grip extension
        dl->AddRectFilled(ImVec2(ox, oy + bh * 0.4f), ImVec2(ox + 8 * s, oy + bh + 4 * s), color, 4 * s);
        // Right grip extension
        dl->AddRectFilled(ImVec2(ox + bw - 8 * s, oy + bh * 0.4f), ImVec2(ox + bw, oy + bh + 4 * s), color, 4 * s);
        // Left analog stick
        float lx = ox + 13 * s, ly = oy + 10 * s;
        dl->AddCircleFilled(ImVec2(lx, ly), 5 * s, bg);
        dl->AddCircleFilled(ImVec2(lx, ly), 2.5f * s, color);
        // Right analog stick
        float rrx = ox + bw - 13 * s, rry = oy + 16 * s;
        dl->AddCircleFilled(ImVec2(rrx, rry), 5 * s, bg);
        dl->AddCircleFilled(ImVec2(rrx, rry), 2.5f * s, color);
        // D-pad (left side, below stick)
        float dpx = ox + 13 * s, dpy = oy + 20 * s;
        dl->AddRectFilled(ImVec2(dpx - 1.2f * s, dpy - 3.5f * s), ImVec2(dpx + 1.2f * s, dpy + 3.5f * s), bg, 0.5f * s);
        dl->AddRectFilled(ImVec2(dpx - 3.5f * s, dpy - 1.2f * s), ImVec2(dpx + 3.5f * s, dpy + 1.2f * s), bg, 0.5f * s);
        // ABXY buttons (right side, above stick)
        float abx = ox + bw - 13 * s, aby = oy + 8 * s;
        float abd = 3 * s, abr = 1.3f * s;
        dl->AddCircleFilled(ImVec2(abx, aby - abd), abr, bg);
        dl->AddCircleFilled(ImVec2(abx, aby + abd), abr, bg);
        dl->AddCircleFilled(ImVec2(abx - abd, aby), abr, bg);
        dl->AddCircleFilled(ImVec2(abx + abd, aby), abr, bg);
        // Center home button
        dl->AddCircleFilled(ImVec2(ox + bw * 0.5f, oy + 10 * s), 2.5f * s, bg);
        break;
    }
    case ControllerType::NSOGCController: {
        // GameCube style — distinctive rounded shape
        float bw = 42 * s, bh = 28 * s, br = 12 * s;
        float ox = pos.x + 2 * s, oy = pos.y + 4 * s;
        // Main body (more rounded)
        dl->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + bw, oy + bh), color, br);
        // Left grip
        dl->AddRectFilled(ImVec2(ox, oy + bh * 0.35f), ImVec2(ox + 7 * s, oy + bh + 3 * s), color, 4 * s);
        // Right grip
        dl->AddRectFilled(ImVec2(ox + bw - 7 * s, oy + bh * 0.35f), ImVec2(ox + bw, oy + bh + 3 * s), color, 4 * s);
        // Main analog stick (large, left)
        float lx = ox + 13 * s, ly = oy + 12 * s;
        dl->AddCircleFilled(ImVec2(lx, ly), 5.5f * s, bg);
        dl->AddCircleFilled(ImVec2(lx, ly), 3 * s, color);
        // C-stick (small, right-top)
        float cx = ox + bw - 12 * s, cy = oy + 9 * s;
        dl->AddCircleFilled(ImVec2(cx, cy), 3 * s, ImGui::GetColorU32(UITheme::ColorFromHex(0xFFCC00))); // Yellow C-stick
        dl->AddCircleFilled(ImVec2(cx, cy), 1.5f * s, color);
        // Big A button (signature GC feature)
        float ax = ox + bw - 14 * s, ay = oy + 18 * s;
        dl->AddCircleFilled(ImVec2(ax, ay), 5 * s, ImGui::GetColorU32(ImVec4(0.3f, 0.8f, 0.4f, 0.7f))); // Green A
        // Small B button
        dl->AddCircleFilled(ImVec2(ax - 6 * s, ay + 2 * s), 2 * s, ImGui::GetColorU32(ImVec4(0.9f, 0.2f, 0.2f, 0.5f))); // Red B
        // D-pad
        float dpx = ox + 13 * s, dpy = oy + 22 * s;
        dl->AddRectFilled(ImVec2(dpx - 1 * s, dpy - 3 * s), ImVec2(dpx + 1 * s, dpy + 3 * s), bg, 0.5f * s);
        dl->AddRectFilled(ImVec2(dpx - 3 * s, dpy - 1 * s), ImVec2(dpx + 3 * s, dpy + 1 * s), bg, 0.5f * s);
        break;
    }
    }
}

// =============================================================
// PAGE: Dashboard
// =============================================================
inline void RenderDashboard() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(24)));
    ImGui::BeginChild("DashboardContent", ImVec2(-S(8), 0), ImGuiChildFlags_None);
    ImGui::PopStyleVar();
    ImGui::SetCursorPos(ImVec2(S(24), S(16)));

    // ViGEm Status
    if (ViGEmManager::Instance().IsConnected()) {
        StatusChip(T("dash_vigem_ok"),
            UITheme::ColorFromHex(0xD4EDDA), UITheme::Success);
    } else {
        StatusChip(T("dash_vigem_fail"),
            UITheme::ColorFromHex(0xF8D7DA), UITheme::Error);
    }

    ImGui::Spacing(); ImGui::Spacing();

    auto& pm = PlayerManager::Instance();
    int total = pm.GetPlayerCount();

    if (total == 0) {
        // Empty state
        ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();
        float avail = ImGui::GetContentRegionAvail().x;
        float centerX = avail * 0.5f;

        // Center the empty state text
        const char* emptyText = T("dash_no_device");
        ImVec2 textSize = ImGui::CalcTextSize(emptyText);
        ImGui::SetCursorPosX(S(24) + centerX - textSize.x * 0.5f);
        ImGui::TextColored(UITheme::TextSecondary, "%s", emptyText);

        const char* hintText = T("dash_no_device_hint");
        textSize = ImGui::CalcTextSize(hintText);
        ImGui::SetCursorPosX(S(24) + centerX - textSize.x * 0.5f);
        ImGui::TextColored(UITheme::TextTertiary, "%s", hintText);
    } else {
        // Device cards
        int playerIndex = 1;

        // Single Joy-Con players
        for (int i = 0; i < (int)pm.GetSinglePlayers().size(); ++i) {
            auto& p = *pm.GetSinglePlayers()[i];
            ImGui::PushID(playerIndex * 100 + i);
            BeginCard(0, 0);

            // Icon
            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            DrawControllerIcon(ControllerType::SingleJoyCon, iconPos, 1.2f, ImGui::GetColorU32(UITheme::Primary));
            ImGui::Dummy(ImVec2(S(28), S(52)));
            ImGui::SameLine();

            // Info
            ImGui::BeginGroup();
            ImGui::Text("%s %d - %s", T("dash_player"), playerIndex, T("type_single_joycon"));
            ImGui::TextColored(UITheme::TextSecondary, "%s  |  %s: %s",
                p.isXboxMode ? T("dash_mapping_xbox") : T("dash_mapping_ds4"),
                p.side == JoyConSide::Left ? T("dash_side_left") : T("dash_side_right"),
                p.orientation == JoyConOrientation::Upright ? T("dash_orient_upright") : T("dash_orient_sideways"));
            if (p.mouseMode > 0) {
                const char* modeNames[] = { "", "FAST", "NORMAL", "SLOW" };
                ImGui::TextColored(UITheme::Warning, "Mouse: %s", modeNames[p.mouseMode]);
            }
            ImGui::EndGroup();

            // Right-aligned buttons: settings + disconnect
            float buttonsWidth = S(80) + S(48);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - buttonsWidth);
            if (IconButton("singleSettings")) {
                ImGui::OpenPopup("SingleJoyConSettings");
            }
            ImGui::SameLine();
            if (DangerButton(T("dash_disconnect"))) {
                pm.RemovePlayerByGlobalIndex(i);
                ImGui::PopID();
                EndCard();
                break;
            }

            // Settings popup
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), S(12)));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, S(12));
            if (ImGui::BeginPopup("SingleJoyConSettings")) {
                ImGui::TextColored(UITheme::TextSecondary, "%s", T("dash_settings"));
                ImGui::Spacing();
                bool swap = p.swapABXY;
                if (ImGui::Checkbox(T("dash_swap_abxy"), &swap)) {
                    p.swapABXY = swap;
                    ConfigManager::Instance().GetDeviceSettings(p.bleAddress).swapABXY = swap;
                    ConfigManager::Instance().Save();
                }
                ImGui::TextColored(UITheme::TextTertiary, "%s", T("dash_swap_abxy_hint"));

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Xbox emulation toggle
                bool xbox = ConfigManager::Instance().GetDeviceSettings(p.bleAddress).useXboxEmulation;
                if (ImGui::Checkbox(T("dash_xbox_emulation"), &xbox)) {
                    if (xbox && !ConfigManager::Instance().config.suppressXboxWarning) {
                        g_xboxWarningPopupOpen = true;
                        g_xboxWarningBleAddress = p.bleAddress;
                        g_xboxWarningPlayerType = 0;
                        g_xboxWarningPlayerIndex = i;
                        ImGui::OpenPopup("##XboxWarning");
                    } else {
                        ConfigManager::Instance().GetDeviceSettings(p.bleAddress).useXboxEmulation = xbox;
                        ConfigManager::Instance().Save();
                    }
                }
                ImGui::TextColored(UITheme::TextTertiary, "%s", T("dash_xbox_emulation_hint"));
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar(2);

            EndCard();
            ImGui::PopID();
            ImGui::Spacing();
            playerIndex++;
        }

        // Dual Joy-Con players
        for (int i = 0; i < (int)pm.GetDualPlayers().size(); ++i) {
            auto& p = pm.GetDualPlayers()[i];
            ImGui::PushID(playerIndex * 100 + 50 + i);
            BeginCard(0, 0);

            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            DrawControllerIcon(ControllerType::DualJoyCon, iconPos, 1.2f, ImGui::GetColorU32(UITheme::Primary));
            ImGui::Dummy(ImVec2(S(44), S(52)));
            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::Text("%s %d - %s", T("dash_player"), playerIndex, T("type_dual_joycon"));
            const char* gyroName = T("dash_gyro_both");
            if (p->gyroSource == GyroSource::Left) gyroName = T("dash_gyro_left");
            else if (p->gyroSource == GyroSource::Right) gyroName = T("dash_gyro_right");
            ImGui::TextColored(UITheme::TextSecondary, "%s  |  %s: %s",
                p->isXboxMode ? T("dash_mapping_xbox") : T("dash_mapping_ds4"),
                T("dash_gyro_source"), gyroName);
            ImGui::EndGroup();

            float dualButtonsWidth = S(80) + S(48);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - dualButtonsWidth);
            if (IconButton("dualSettings")) {
                ImGui::OpenPopup("DualJoyConSettings");
            }
            ImGui::SameLine();
            int globalIdx = (int)pm.GetSinglePlayers().size() + i;
            if (DangerButton(T("dash_disconnect"))) {
                pm.RemovePlayerByGlobalIndex(globalIdx);
                ImGui::PopID();
                EndCard();
                break;
            }

            // Settings popup
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), S(12)));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, S(12));
            if (ImGui::BeginPopup("DualJoyConSettings")) {
                ImGui::TextColored(UITheme::TextSecondary, "%s", T("dash_settings"));
                ImGui::Spacing();
                bool swap = p->swapABXY;
                if (ImGui::Checkbox(T("dash_swap_abxy"), &swap)) {
                    p->swapABXY = swap;
                    ConfigManager::Instance().GetDeviceSettings(p->bleAddress).swapABXY = swap;
                    ConfigManager::Instance().Save();
                }
                ImGui::TextColored(UITheme::TextTertiary, "%s", T("dash_swap_abxy_hint"));

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Xbox emulation toggle
                bool xbox = ConfigManager::Instance().GetDeviceSettings(p->bleAddress).useXboxEmulation;
                if (ImGui::Checkbox(T("dash_xbox_emulation"), &xbox)) {
                    if (xbox && !ConfigManager::Instance().config.suppressXboxWarning) {
                        g_xboxWarningPopupOpen = true;
                        g_xboxWarningBleAddress = p->bleAddress;
                        g_xboxWarningPlayerType = 1;
                        g_xboxWarningPlayerIndex = i;
                        ImGui::OpenPopup("##XboxWarning");
                    } else {
                        ConfigManager::Instance().GetDeviceSettings(p->bleAddress).useXboxEmulation = xbox;
                        ConfigManager::Instance().Save();
                    }
                }
                ImGui::TextColored(UITheme::TextTertiary, "%s", T("dash_xbox_emulation_hint"));
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar(2);

            EndCard();
            ImGui::PopID();
            ImGui::Spacing();
            playerIndex++;
        }

        // Pro / NSO GC players
        for (int i = 0; i < (int)pm.GetProPlayers().size(); ++i) {
            auto& p = pm.GetProPlayers()[i];
            ImGui::PushID(playerIndex * 100 + 80 + i);
            BeginCard(0, 0);

            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            DrawControllerIcon(p.type, iconPos, 1.2f, ImGui::GetColorU32(UITheme::Primary));
            ImGui::Dummy(ImVec2(S(52), S(44)));
            ImGui::SameLine();

            ImGui::BeginGroup();
            const char* typeName = (p.type == ControllerType::ProController) ? T("type_pro") : T("type_nso_gc");
            ImGui::Text("%s %d - %s", T("dash_player"), playerIndex, typeName);
            ImGui::TextColored(UITheme::TextSecondary, "%s",
                p.isXboxModeFlag->load(std::memory_order_relaxed) ? T("dash_mapping_xbox") : T("dash_mapping_ds4"));
            if (p.type == ControllerType::ProController) {
                auto& config = ConfigManager::Instance().config.proConfig;
                if (!config.layouts.empty()) {
                    auto& layout = config.layouts[config.activeLayoutIndex];
                    ImGui::TextColored(UITheme::TextTertiary, "GL: %s  GR: %s  [%s]",
                        ButtonMappingToString(layout.glMapping).c_str(),
                        ButtonMappingToString(layout.grMapping).c_str(),
                        layout.name.c_str());
                }
            }
            ImGui::EndGroup();

            float proButtonsWidth = S(80) + S(48);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - proButtonsWidth);
            if (IconButton("proSettings")) {
                ImGui::OpenPopup("ProSettings");
            }
            ImGui::SameLine();
            int globalIdx = (int)pm.GetSinglePlayers().size() + (int)pm.GetDualPlayers().size() + i;
            if (DangerButton(T("dash_disconnect"))) {
                pm.RemovePlayerByGlobalIndex(globalIdx);
                ImGui::PopID();
                EndCard();
                break;
            }

            // Settings popup
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16), S(12)));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, S(12));
            if (ImGui::BeginPopup("ProSettings")) {
                ImGui::TextColored(UITheme::TextSecondary, "%s", T("dash_settings"));
                ImGui::Spacing();
                bool swap = p.swapABXYFlag->load(std::memory_order_relaxed);
                if (ImGui::Checkbox(T("dash_swap_abxy"), &swap)) {
                    p.swapABXYFlag->store(swap, std::memory_order_relaxed);
                    ConfigManager::Instance().GetDeviceSettings(p.bleAddress).swapABXY = swap;
                    ConfigManager::Instance().Save();
                }
                ImGui::TextColored(UITheme::TextTertiary, "%s", T("dash_swap_abxy_hint"));

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Raw vibration toggle (Pro2 / NSO GC)
                bool rawVib = p.useRawVibrationFlag->load(std::memory_order_relaxed);
                if (ImGui::Checkbox(T("dash_raw_vibration"), &rawVib)) {
                    p.useRawVibrationFlag->store(rawVib, std::memory_order_relaxed);
                    ConfigManager::Instance().GetDeviceSettings(p.bleAddress).useRawVibration = rawVib;
                    ConfigManager::Instance().Save();
                    // Also update the live VibrationContext if it exists
                    if (p.vibCtx && p.vibCtx->useRawVibration) {
                        p.vibCtx->useRawVibration->store(rawVib, std::memory_order_relaxed);
                    }
                }
                ImGui::TextColored(UITheme::TextTertiary, "%s", T("dash_raw_vibration_hint"));

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Xbox emulation toggle
                bool xbox = ConfigManager::Instance().GetDeviceSettings(p.bleAddress).useXboxEmulation;
                if (ImGui::Checkbox(T("dash_xbox_emulation"), &xbox)) {
                    if (xbox && !ConfigManager::Instance().config.suppressXboxWarning) {
                        g_xboxWarningPopupOpen = true;
                        g_xboxWarningBleAddress = p.bleAddress;
                        g_xboxWarningPlayerType = 2;
                        g_xboxWarningPlayerIndex = i;
                        ImGui::OpenPopup("##XboxWarning");
                    } else {
                        ConfigManager::Instance().GetDeviceSettings(p.bleAddress).useXboxEmulation = xbox;
                        ConfigManager::Instance().Save();
                    }
                }
                ImGui::TextColored(UITheme::TextTertiary, "%s", T("dash_xbox_emulation_hint"));

                ImGui::EndPopup();
            }
            ImGui::PopStyleVar(2);

            EndCard();
            ImGui::PopID();
            ImGui::Spacing();
            playerIndex++;
        }
    }

    // ---- Xbox emulation warning modal ----
    if (g_xboxWarningPopupOpen) {
        ImGui::OpenPopup("##XboxWarning");
    }

    ImVec2 xboxWarnCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(xboxWarnCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(420), 0));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(20)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, UITheme::SurfaceCard);

    if (ImGui::BeginPopupModal("##XboxWarning", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
    {
        // Title
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
        ImGui::TextColored(UITheme::Primary, "%s", T("dash_xbox_warning_title"));
        if (ImGui::GetIO().Fonts->Fonts.Size > 1) ImGui::PopFont();

        ImGui::Spacing(); ImGui::Spacing();

        ImGui::TextWrapped("%s", T("dash_xbox_warning_msg"));

        ImGui::Spacing(); ImGui::Spacing();

        // "Don't show again" checkbox
        static bool suppressCheck = false;
        ImGui::Checkbox(T("dash_xbox_warning_suppress"), &suppressCheck);

        ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();

        // Buttons row — right-aligned
        float btnWidth1 = ImGui::CalcTextSize(T("dash_xbox_warning_confirm")).x + S(48);
        float btnWidth2 = ImGui::CalcTextSize(T("dash_xbox_warning_cancel")).x + S(48);
        float totalWidth = btnWidth1 + btnWidth2 + S(12);
        float availWidth = ImGui::GetContentRegionAvail().x;
        if (totalWidth < availWidth) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availWidth - totalWidth);
        }

        if (SecondaryButton(T("dash_xbox_warning_cancel"))) {
            g_xboxWarningPopupOpen = false;
            suppressCheck = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, S(12));
        if (PrimaryButton(T("dash_xbox_warning_confirm"))) {
            // Save suppress preference
            if (suppressCheck) {
                ConfigManager::Instance().config.suppressXboxWarning = true;
            }
            // Apply Xbox emulation setting
            ConfigManager::Instance().GetDeviceSettings(g_xboxWarningBleAddress).useXboxEmulation = true;
            ConfigManager::Instance().Save();

            g_xboxWarningPopupOpen = false;
            suppressCheck = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImGui::EndChild();
}

// =============================================================
// PAGE: Add Device
// =============================================================
inline void RenderAddDevice(int& activePage) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(24)));
    ImGui::BeginChild("AddDeviceContent", ImVec2(-S(8), 0), ImGuiChildFlags_None);
    ImGui::PopStyleVar();
    ImGui::SetCursorPos(ImVec2(S(24), S(16)));

    // Step indicators
    int displayStep = g_wizard.step + 1;
    const char* totalSteps = (g_wizard.selectedType == ControllerType::DualJoyCon) ? "5" : "3";
    ImGui::TextColored(UITheme::TextTertiary, "%d / %s", displayStep, totalSteps);
    ImGui::Spacing();

    if (g_wizard.step == 0) {
        // Step 1: Select controller type
        SectionLabel(T("add_step1_title"));
        ImGui::Spacing();

        struct TypeOption {
            ControllerType type;
            const char* nameKey;
        };
        TypeOption options[] = {
            { ControllerType::SingleJoyCon, "type_single_joycon" },
            { ControllerType::DualJoyCon,   "type_dual_joycon" },
            { ControllerType::ProController, "type_pro" },
            { ControllerType::NSOGCController, "type_nso_gc" }
        };

        for (auto& opt : options) {
            ImGui::PushID((int)opt.type);
            bool selected = g_wizard.selectedType == opt.type;

            ImVec4 bgCol = selected ? UITheme::SidebarActive : UITheme::SurfaceCard;
            ImVec4 borderCol = selected ? UITheme::Primary : UITheme::Border;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, bgCol);
            ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, selected ? 2.0f : 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));

            ImGui::BeginChild("typecard", ImVec2(ImGui::GetContentRegionAvail().x, S(70)), ImGuiChildFlags_Borders);

            ImGui::SetCursorPos(ImVec2(S(16), S(12)));
            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            DrawControllerIcon(opt.type, iconPos, 1.0f, ImGui::GetColorU32(selected ? UITheme::Primary : UITheme::TextSecondary));
            ImGui::Dummy(ImVec2(S(52), S(44)));
            ImGui::SameLine();
            float textH = ImGui::GetTextLineHeight();
            ImGui::SetCursorPosY((S(70) - textH) * 0.5f);
            ImGui::Text("%s", T(opt.nameKey));

            // Click detection
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
                g_wizard.selectedType = opt.type;
            }

            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
            ImGui::PopID();
            ImGui::Spacing();
        }

        ImGui::Spacing();
        if (PrimaryButton(T("add_next"))) {
            // Pro and NSO GC skip step 2 config
            if (g_wizard.selectedType == ControllerType::ProController ||
                g_wizard.selectedType == ControllerType::NSOGCController) {
                g_wizard.step = 2;
            } else {
                g_wizard.step = 1;
            }
        }

    } else if (g_wizard.step == 1) {
        // Step 2: Configure
        SectionLabel(T("add_step2_title"));
        ImGui::Spacing();

        if (g_wizard.selectedType == ControllerType::SingleJoyCon) {
            // Side selection
            ImGui::Text("%s", T("add_select_side"));
            if (ImGui::RadioButton(T("dash_side_left"), g_wizard.selectedSide == JoyConSide::Left))
                g_wizard.selectedSide = JoyConSide::Left;
            ImGui::SameLine();
            if (ImGui::RadioButton(T("dash_side_right"), g_wizard.selectedSide == JoyConSide::Right))
                g_wizard.selectedSide = JoyConSide::Right;

            ImGui::Spacing();

            // Orientation
            ImGui::Text("%s", T("add_select_orient"));
            if (ImGui::RadioButton(T("dash_orient_upright"), g_wizard.selectedOrientation == JoyConOrientation::Upright))
                g_wizard.selectedOrientation = JoyConOrientation::Upright;
            ImGui::SameLine();
            if (ImGui::RadioButton(T("dash_orient_sideways"), g_wizard.selectedOrientation == JoyConOrientation::Sideways))
                g_wizard.selectedOrientation = JoyConOrientation::Sideways;

        } else if (g_wizard.selectedType == ControllerType::DualJoyCon) {
            // Gyro source
            ImGui::Text("%s", T("add_select_gyro"));
            if (ImGui::RadioButton(T("dash_gyro_both"), g_wizard.selectedGyro == GyroSource::Both))
                g_wizard.selectedGyro = GyroSource::Both;
            ImGui::SameLine();
            if (ImGui::RadioButton(T("dash_gyro_left"), g_wizard.selectedGyro == GyroSource::Left))
                g_wizard.selectedGyro = GyroSource::Left;
            ImGui::SameLine();
            if (ImGui::RadioButton(T("dash_gyro_right"), g_wizard.selectedGyro == GyroSource::Right))
                g_wizard.selectedGyro = GyroSource::Right;
        }

        ImGui::Spacing(); ImGui::Spacing();
        if (SecondaryButton(T("add_back"))) g_wizard.step = 0;
        ImGui::SameLine();
        if (PrimaryButton(T("add_next"))) g_wizard.step = 2;

    } else if (g_wizard.step == 2) {
        // Step 3: Scanning
        if (g_wizard.selectedType == ControllerType::DualJoyCon && !g_wizard.dualFirstDone) {
            SectionLabel(T("add_scan_right_title"));
        } else {
            SectionLabel(T("add_step3_title"));
        }

        ImGui::Spacing();

        if (!g_wizard.scanStarted) {
            ImGui::TextColored(UITheme::TextSecondary, "%s", T("add_pre_scan_hint"));
            ImGui::Spacing();
            if (PrimaryButton(T("add_start_scan"))) {
                g_wizard.scanStarted = true;
                g_wizard.statusMessage.clear();

                DeviceManager::Instance().StartScan([](ConnectedJoyCon cj, ScanState state) {
                    if (state == ScanState::Found) {
                        auto& wiz = g_wizard;
                        bool ok = false;

                        if (wiz.selectedType == ControllerType::SingleJoyCon) {
                            ok = PlayerManager::Instance().AddSingleJoyCon(cj, wiz.selectedSide, wiz.selectedOrientation);
                        } else if (wiz.selectedType == ControllerType::DualJoyCon) {
                            if (!wiz.dualFirstDone) {
                                ok = PlayerManager::Instance().AddDualJoyConFirstStep(cj, wiz.selectedGyro);
                                if (ok) {
                                    wiz.dualFirstDone = true;
                                    wiz.scanStarted = false; // reset scan for second Joy-Con
                                    wiz.step = 3; // go to dual right success page
                                    return;
                                }
                            } else {
                                ok = PlayerManager::Instance().AddDualJoyConSecondStep(cj);
                            }
                        } else {
                            ok = PlayerManager::Instance().AddProOrGC(cj, wiz.selectedType);
                        }

                        if (ok) wiz.statusMessage = "OK";
                        else wiz.statusMessage = "FAIL";
                    } else if (state == ScanState::Timeout) {
                        g_wizard.statusMessage = "TIMEOUT";
                    } else if (state == ScanState::Error) {
                        g_wizard.statusMessage = "ERROR";
                    }
                });
            }
            ImGui::SameLine();
            if (SecondaryButton(T("add_back"))) {
                if (g_wizard.selectedType == ControllerType::ProController ||
                    g_wizard.selectedType == ControllerType::NSOGCController)
                    g_wizard.step = 0;
                else
                    g_wizard.step = 1;
            }
        } else {
            // Scanning in progress or complete
            if (g_wizard.statusMessage.empty()) {
                // Still scanning
                Spinner("##scan", 16.0f, 3.0f, ImGui::GetColorU32(UITheme::Primary));
                ImGui::SameLine();
                ImGui::TextColored(UITheme::TextSecondary, "%s", T("add_scanning_active_hint"));

                ImGui::Spacing();
                if (SecondaryButton(T("add_cancel"))) {
                    DeviceManager::Instance().StopScan();
                    g_wizard.Reset();
                }
            } else if (g_wizard.statusMessage == "OK") {
                // Success
                ImGui::TextColored(UITheme::Success, "%s", T("add_connected"));
                ImGui::Spacing();
                if (PrimaryButton("OK")) {
                    g_wizard.Reset();
                    activePage = 0; // go to dashboard
                }
            } else if (g_wizard.statusMessage == "TIMEOUT") {
                ImGui::TextColored(UITheme::Error, "%s", T("add_timeout"));
                ImGui::Spacing();
                if (SecondaryButton(T("add_back"))) g_wizard.scanStarted = false;
            } else {
                ImGui::TextColored(UITheme::Error, "Error connecting.");
                ImGui::Spacing();
                if (SecondaryButton(T("add_back"))) g_wizard.scanStarted = false;
            }
        }

    } else if (g_wizard.step == 3) {
        // Step 4 (Dual JoyCon only): Right Joy-Con connected success page
        SectionLabel(T("add_right_connected"));
        ImGui::Spacing();
        ImGui::TextColored(UITheme::Success, "%s", T("add_right_connected"));
        ImGui::Spacing();
        if (PrimaryButton(T("add_next"))) {
            g_wizard.step = 4; // proceed to scan left Joy-Con
        }

    } else if (g_wizard.step == 4) {
        // Step 5 (Dual JoyCon only): Scan LEFT Joy-Con
        SectionLabel(T("add_scan_left_title"));
        ImGui::Spacing();

        if (!g_wizard.scanStarted) {
            ImGui::TextColored(UITheme::TextSecondary, "%s", T("add_pre_scan_hint"));
            ImGui::Spacing();
            if (PrimaryButton(T("add_start_scan"))) {
                g_wizard.scanStarted = true;
                g_wizard.statusMessage.clear();

                DeviceManager::Instance().StartScan([&activePage](ConnectedJoyCon cj, ScanState state) {
                    if (state == ScanState::Found) {
                        bool ok = PlayerManager::Instance().AddDualJoyConSecondStep(cj);
                        g_wizard.statusMessage = ok ? "OK" : "FAIL";
                    } else if (state == ScanState::Timeout) {
                        g_wizard.statusMessage = "TIMEOUT";
                    }
                });
            }
        } else {
            if (g_wizard.statusMessage.empty()) {
                Spinner("##scan2", 16.0f, 3.0f, ImGui::GetColorU32(UITheme::Primary));
                ImGui::SameLine();
                ImGui::TextColored(UITheme::TextSecondary, "%s", T("add_scanning_active_hint"));
                ImGui::Spacing();
                if (SecondaryButton(T("add_cancel"))) {
                    DeviceManager::Instance().StopScan();
                    g_wizard.Reset();
                }
            } else if (g_wizard.statusMessage == "OK") {
                ImGui::TextColored(UITheme::Success, "%s", T("add_connected"));
                ImGui::Spacing();
                if (PrimaryButton("OK")) {
                    g_wizard.Reset();
                    activePage = 0;
                }
            } else {
                ImGui::TextColored(UITheme::Error, "%s", T("add_timeout"));
                ImGui::Spacing();
                if (SecondaryButton(T("add_back"))) g_wizard.scanStarted = false;
            }
        }
    }

    ImGui::EndChild();
}

// =============================================================
// PAGE: Layout Manager
// =============================================================
inline void RenderLayoutManager() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(24)));
    ImGui::BeginChild("LayoutContent", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::PopStyleVar();
    ImGui::SetCursorPos(ImVec2(S(24), S(16)));

    SectionLabel(T("layout_title"));

    auto& config = ConfigManager::Instance().config.proConfig;
    float totalW = ImGui::GetContentRegionAvail().x;
    float leftW = (std::max)(totalW * 0.35f, S(320));
    float availH = ImGui::GetContentRegionAvail().y;

    // === Left column (layout list + buttons) ===
    ImGui::BeginChild("LeftPanel", ImVec2(leftW, availH), ImGuiChildFlags_None);

    // Layout list (scrollable)
    float btnRowH = S(56);
    ImGui::BeginChild("LayoutList", ImVec2(-1, ImGui::GetContentRegionAvail().y - btnRowH), ImGuiChildFlags_None);

    for (int i = 0; i < (int)config.layouts.size(); ++i) {
        ImGui::PushID(i);
        bool isActive = (i == config.activeLayoutIndex);
        bool isSelected = (i == g_selectedLayoutIndex);

        ImVec4 bg = isSelected ? UITheme::SidebarActive : UITheme::SurfaceCard;
        ImVec4 border = isActive ? UITheme::Primary : (isSelected ? UITheme::PrimaryHover : UITheme::Border);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
        ImGui::PushStyleColor(ImGuiCol_Border, border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isActive ? 2.0f : 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(8));

        ImGui::BeginChild("layoutitem", ImVec2(-1, S(50)), ImGuiChildFlags_Borders);
        ImGui::SetCursorPos(ImVec2(S(12), S(8)));

        if (g_renamingLayout && isSelected) {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - S(16));
            if (ImGui::InputText("##rename", g_layoutNameBuf, sizeof(g_layoutNameBuf),
                ImGuiInputTextFlags_EnterReturnsTrue)) {
                config.layouts[i].name = g_layoutNameBuf;
                g_renamingLayout = false;
                ConfigManager::Instance().Save();
            }
        } else {
            ImGui::Text("%s", config.layouts[i].name.c_str());
            if (isActive) {
                ImGui::SameLine();
                ImGui::TextColored(UITheme::Primary, "[%s]", T("layout_active_badge"));
            }
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            g_selectedLayoutIndex = i;
            g_renamingLayout = false;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        ImGui::Spacing();
    }

    ImGui::EndChild(); // LayoutList

    // Buttons row (fixed at bottom of left panel)
    ImGui::Spacing();
    if (PrimaryButton(T("layout_add"))) {
        GLGRLayout newLayout;
        newLayout.name = "Layout " + std::to_string(config.layouts.size() + 1);
        config.layouts.push_back(newLayout);
        g_selectedLayoutIndex = (int)config.layouts.size() - 1;
        ConfigManager::Instance().Save();
    }
    ImGui::SameLine();
    if (SecondaryButton(T("layout_rename")) && g_selectedLayoutIndex < (int)config.layouts.size()) {
        g_renamingLayout = true;
        strncpy_s(g_layoutNameBuf, config.layouts[g_selectedLayoutIndex].name.c_str(), sizeof(g_layoutNameBuf) - 1);
    }
    ImGui::SameLine();
    if (DangerButton(T("layout_delete")) && config.layouts.size() > 1) {
        config.layouts.erase(config.layouts.begin() + g_selectedLayoutIndex);
        if (config.activeLayoutIndex >= (int)config.layouts.size())
            config.activeLayoutIndex = (int)config.layouts.size() - 1;
        if (g_selectedLayoutIndex >= (int)config.layouts.size())
            g_selectedLayoutIndex = (int)config.layouts.size() - 1;
        ConfigManager::Instance().Save();
    }

    ImGui::EndChild(); // LeftPanel

    // === Right column (mapping details) - always visible ===
    ImGui::SameLine(0, S(24));
    ImGui::BeginChild("RightPanel", ImVec2(0, availH), ImGuiChildFlags_None);

    if (g_selectedLayoutIndex < (int)config.layouts.size()) {
        auto& layout = config.layouts[g_selectedLayoutIndex];

        ImGui::Spacing();

        // GL dropdown
        ImGui::Text("%s", T("layout_gl"));
        ImGui::SetNextItemWidth(S(220));
        int glIdx = static_cast<int>(layout.glMapping);
        if (ImGui::Combo("##gl", &glIdx, ButtonMappingNames, ButtonMappingCount)) {
            layout.glMapping = static_cast<ButtonMapping>(glIdx);
            ConfigManager::Instance().Save();
        }

        ImGui::Spacing(); ImGui::Spacing();

        // GR dropdown
        ImGui::Text("%s", T("layout_gr"));
        ImGui::SetNextItemWidth(S(220));
        int grIdx = static_cast<int>(layout.grMapping);
        if (ImGui::Combo("##gr", &grIdx, ButtonMappingNames, ButtonMappingCount)) {
            layout.grMapping = static_cast<ButtonMapping>(grIdx);
            ConfigManager::Instance().Save();
        }

        ImGui::Spacing(); ImGui::Spacing();

        // Set Active
        bool isActive = (g_selectedLayoutIndex == config.activeLayoutIndex);
        if (!isActive) {
            if (PrimaryButton(T("layout_set_active"))) {
                config.activeLayoutIndex = g_selectedLayoutIndex;
                ConfigManager::Instance().Save();
            }
        } else {
            ImGui::TextColored(UITheme::Success, ">> %s <<", T("layout_active_badge"));
        }
    }

    ImGui::Spacing(); ImGui::Spacing();
    ImGui::TextColored(UITheme::TextTertiary, "%s", T("layout_hint"));

    ImGui::EndChild(); // RightPanel

    ImGui::EndChild(); // LayoutContent
}

// =============================================================
// PAGE: Mouse Settings
// =============================================================
inline void RenderMouseSettings() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(24)));
    ImGui::BeginChild("MouseContent", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::PopStyleVar();
    ImGui::SetCursorPos(ImVec2(S(24), S(16)));

    SectionLabel(T("mouse_title"));

    ImGui::TextColored(UITheme::TextTertiary, "%s", T("mouse_hint"));
    ImGui::Spacing(); ImGui::Spacing();

    auto& mouseConfig = ConfigManager::Instance().config.mouseConfig;
    bool changed = false;

    // Enable toggle card — using built-in card padding (no SetCursorPos hack)
    BeginCard();
    if (ImGui::Checkbox(T("mouse_enable"), &mouseConfig.chatKeyEnabled))
        changed = true;
    EndCard();

    ImGui::Spacing(); ImGui::Spacing();

    // Sensitivity sliders card
    float sliderW = (std::max)(ImGui::GetContentRegionAvail().x - S(100), S(120));
    BeginCard();

    ImGui::Text("%s", T("mouse_fast"));
    ImGui::SetNextItemWidth(sliderW);
    if (ImGui::SliderFloat("##fast", &mouseConfig.fastSensitivity, 0.1f, 3.0f, "%.2f"))
        changed = true;

    ImGui::Spacing();

    ImGui::Text("%s", T("mouse_normal"));
    ImGui::SetNextItemWidth(sliderW);
    if (ImGui::SliderFloat("##normal", &mouseConfig.normalSensitivity, 0.1f, 2.0f, "%.2f"))
        changed = true;

    ImGui::Spacing();

    ImGui::Text("%s", T("mouse_slow"));
    ImGui::SetNextItemWidth(sliderW);
    if (ImGui::SliderFloat("##slow", &mouseConfig.slowSensitivity, 0.05f, 1.0f, "%.2f"))
        changed = true;

    ImGui::Spacing(); ImGui::Spacing();

    ImGui::Text("%s", T("mouse_scroll_speed"));
    ImGui::SetNextItemWidth(sliderW);
    if (ImGui::SliderFloat("##scroll", &mouseConfig.scrollSpeed, 5.0f, 120.0f, "%.0f"))
        changed = true;

    ImGui::Spacing(); ImGui::Spacing();

    ImGui::Text("%s", T("mouse_interpolation"));
    if (ImGui::Checkbox("##interp", &mouseConfig.interpolationEnabled))
        changed = true;

    if (mouseConfig.interpolationEnabled) {
        ImGui::Text("%s", T("mouse_interp_rate"));
        ImGui::SetNextItemWidth(sliderW);
        if (ImGui::SliderInt("##interpRate", &mouseConfig.interpolationRateHz, 100, 500, "%d Hz"))
            changed = true;
    }

    EndCard();

    ImGui::Spacing(); ImGui::Spacing();

    // Current mode indicator card
    BeginCard();
    ImGui::Text("%s: ", T("mouse_current_mode"));
    ImGui::SameLine();

    int currentMode = 0;
    for (auto& sp : PlayerManager::Instance().GetSinglePlayers()) {
        if (sp->side == JoyConSide::Right) {
            currentMode = sp->mouseMode;
            break;
        }
    }
    const char* modeKeys[] = { "mouse_mode_off", "mouse_mode_fast", "mouse_mode_normal", "mouse_mode_slow" };
    ImVec4 modeColors[] = { UITheme::TextTertiary, UITheme::Warning, UITheme::Primary, UITheme::Success };
    ImGui::TextColored(modeColors[currentMode], "%s", T(modeKeys[currentMode]));
    EndCard();

    if (changed) ConfigManager::Instance().Save();

    ImGui::EndChild();
}

// =============================================================
// PAGE: Settings
// =============================================================
inline void RenderSettings() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(24)));
    ImGui::BeginChild("SettingsContent", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::PopStyleVar();
    ImGui::SetCursorPos(ImVec2(S(24), S(16)));

    SectionLabel(T("settings_title"));
    ImGui::Spacing(); ImGui::Spacing();

    // ---- Language selection card ----
    BeginCard();

    ImGui::Text("%s", T("settings_language"));
    ImGui::TextColored(UITheme::TextTertiary, "%s", T("settings_language_hint"));
    ImGui::Spacing();

    const auto& langs = I18nManager::Instance().GetAvailableLanguages();
    int currentIdx = I18nManager::Instance().GetLocaleIndex(
        I18nManager::Instance().GetCurrentLocale());
    if (currentIdx < 0) currentIdx = 0;

    // Build display names for Combo — use a lambda with static storage
    // to keep the strings alive for ImGui
    static std::vector<std::string> langDisplayNames;
    static std::vector<const char*> langDisplayPtrs;
    langDisplayNames.clear();
    langDisplayPtrs.clear();
    for (const auto& lang : langs) {
        langDisplayNames.push_back(lang.displayName);
    }
    for (const auto& name : langDisplayNames) {
        langDisplayPtrs.push_back(name.c_str());
    }

    ImGui::SetNextItemWidth(S(220));
    if (!langDisplayPtrs.empty()) {
        if (ImGui::Combo("##lang", &currentIdx, langDisplayPtrs.data(), (int)langDisplayPtrs.size())) {
            if (currentIdx >= 0 && currentIdx < (int)langs.size()) {
                const std::string& newLocale = langs[currentIdx].locale;
                I18nManager::Instance().LoadLanguage(newLocale);
                ConfigManager::Instance().config.language = newLocale;
                ConfigManager::Instance().Save();
            }
        }
    }

    EndCard();

    ImGui::Spacing(); ImGui::Spacing();

    // ---- Minimize to tray card ----
    BeginCard();

    bool& trayVal = ConfigManager::Instance().config.minimizeToTray;
    if (ImGui::Checkbox(T("settings_minimize_to_tray"), &trayVal)) {
        ConfigManager::Instance().Save();
    }
    ImGui::TextColored(UITheme::TextTertiary, "%s", T("settings_minimize_to_tray_hint"));

    EndCard();

    ImGui::Spacing(); ImGui::Spacing();

    // ---- Update checker card ----
    BeginCard();

    bool& autoUpdate = ConfigManager::Instance().config.autoCheckUpdate;
    if (ImGui::Checkbox(T("settings_auto_check_update"), &autoUpdate)) {
        ConfigManager::Instance().Save();
    }
    ImGui::TextColored(UITheme::TextTertiary, "%s", T("settings_auto_check_update_hint"));
    ImGui::Spacing();

    {
        auto updateState = UpdateChecker::Instance().GetState();
        bool isChecking = (updateState == UpdateState::Checking);

        if (isChecking) {
            // Show disabled-style button while checking
            ImGui::PushStyleColor(ImGuiCol_Button, UITheme::SurfaceDim);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UITheme::SurfaceDim);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, UITheme::SurfaceDim);
            ImGui::PushStyleColor(ImGuiCol_Text, UITheme::TextTertiary);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(24), S(10)));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(20));
            ImGui::Button(T("update_checking"), MD3ButtonSize());
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        } else {
            if (SecondaryButton(T("settings_check_update"))) {
                UpdateChecker::Instance().CheckForUpdate();
            }
        }

        // Status text feedback (only for manual checks)
        if (updateState == UpdateState::UpToDate) {
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(10));
            ImGui::TextColored(UITheme::Success, "%s", T("update_up_to_date"));
        } else if (updateState == UpdateState::Error) {
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(10));
            ImGui::TextColored(UITheme::Error, "%s", T("update_error"));
        }
    }

    EndCard();

    ImGui::Spacing(); ImGui::Spacing();

    // ---- About card ----
    BeginCard();

    ImGui::Text("%s", T("settings_about"));
    ImGui::Spacing();

    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
    ImGui::TextColored(UITheme::Primary, "JoyCon2 Connector");
    if (ImGui::GetIO().Fonts->Fonts.Size > 1) ImGui::PopFont();

    ImGui::TextColored(UITheme::TextTertiary, "v%s", APP_VERSION);
    ImGui::Spacing();
    ImGui::TextColored(UITheme::TextTertiary, "%s", T("settings_about_desc"));

    ImGui::Spacing();

    // Author + GitHub icon on the same line
    ImGui::TextColored(UITheme::TextSecondary, "%s", T("settings_about_author"));
    ImGui::SameLine(0, S(8));

    // GitHub icon button — MD3 tonal icon button style (small circle)
    {
        float btnSize = S(28);
        float iconR = S(10);
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        // Vertically center with the text line
        float textH = ImGui::CalcTextSize(T("settings_about_author")).y;
        float offsetY = (textH - btnSize) * 0.5f;
        ImVec2 btnPos = ImVec2(cursor.x, cursor.y + offsetY);

        ImGui::SetCursorScreenPos(btnPos);
        ImGui::InvisibleButton("##github_icon", ImVec2(btnSize, btnSize));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 iconCenter = ImVec2(btnPos.x + btnSize * 0.5f, btnPos.y + btnSize * 0.5f);

        // Draw the GitHub mark
        ImVec4 fgColor = hovered ? UITheme::Primary : UITheme::TextSecondary;
        DrawGitHubMark(dl, iconCenter, iconR, ImGui::GetColorU32(fgColor));

        // Tooltip on hover
        if (hovered) {
            ImGui::SetTooltip("GitHub");
        }

        if (clicked) {
            ShellExecuteW(nullptr, L"open",
                L"https://github.com/Misaka10571/joycon2-connector",
                nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    EndCard();

    // ---- Update available popup ----
    static bool updatePopupOpen = false;
    if (UpdateChecker::Instance().ShouldShowPopup()) {
        UpdateChecker::Instance().PopupShown();
        updatePopupOpen = true;
        ImGui::OpenPopup("##UpdatePopup");
    }

    // Center the popup
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(420), 0));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24), S(20)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, UITheme::SurfaceCard);

    if (ImGui::BeginPopupModal("##UpdatePopup", &updatePopupOpen,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
    {
        // Title
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
        ImGui::TextColored(UITheme::Primary, "%s", T("update_available_title"));
        if (ImGui::GetIO().Fonts->Fonts.Size > 1) ImGui::PopFont();

        ImGui::Spacing(); ImGui::Spacing();

        // Message with version info
        char msgBuf[256];
        snprintf(msgBuf, sizeof(msgBuf), T("update_available_msg"),
            UpdateChecker::Instance().GetLatestVersion().c_str(), APP_VERSION);
        ImGui::TextWrapped("%s", msgBuf);

        ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();

        // Buttons row — right-aligned
        float btnWidth1 = ImGui::CalcTextSize(T("update_go_to_download")).x + S(48);
        float btnWidth2 = ImGui::CalcTextSize(T("update_later")).x + S(48);
        float totalWidth = btnWidth1 + btnWidth2 + S(12);
        float availWidth = ImGui::GetContentRegionAvail().x;
        if (totalWidth < availWidth) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availWidth - totalWidth);
        }

        if (SecondaryButton(T("update_later"))) {
            updatePopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, S(12));
        if (PrimaryButton(T("update_go_to_download"))) {
            UpdateChecker::Instance().OpenReleasePage();
            updatePopupOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImGui::EndChild();
}
