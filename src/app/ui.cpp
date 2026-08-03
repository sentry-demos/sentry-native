#include "app/ui.h"

#include "app/console_log.h"
#include "app/fleet_model.h"
#include "app/icons.h"
#include "app/theme.h"
#include "app/telemetry_feed.h"
#include "chaos/chaos.h"
#include "core/sentry_manager.h"
#include "core/offline_queue_monitor.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace empower {

namespace {

struct Nav { const char* icon; const char* label; const char* subtitle; };
const Nav kNav[] = {
    {ICON_SEEDLING, "Fleet", "Live status across every Empower Plant device"},
    {ICON_CHART, "Telemetry", "Metrics and logs streaming to Sentry"},
    {ICON_PIPELINE, "Pipelines", "Background jobs processing device data"},
    {ICON_BUG, "Chaos Lab", "Trigger faults - Go Offline to queue envelopes locally"},
    {ICON_GEAR, "Settings", "SDK configuration and enabled features"},
};

ImU32 u32(ImVec4 c) { return ImGui::GetColorU32(c); }
ImVec4 with_alpha(ImVec4 c, float a) { c.w = a; return c; }

ImVec4 status_color(Device::Status s) {
    switch (s) {
        case Device::Status::Ok: return theme::color::ok;
        case Device::Status::Warning: return theme::color::warn;
        case Device::Status::Error: return theme::color::danger;
        case Device::Status::Offline: return theme::color::text_faint;
    }
    return theme::color::text_faint;
}

void dim_text(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

void heading(ImFont* font, const char* text, ImVec4 col = theme::color::text) {
    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

std::string with_icon(const char* icon, const char* label) {
    if (theme::has_icons()) return std::string(icon) + "   " + label;
    return label;
}

struct FeedbackWidget {
    bool expanded = false;
    char message[2048] = {};
    float fab_x = 0.0f;
    float fab_y = 0.0f;
    float fab = 36.0f;
};

FeedbackWidget g_feedback;

bool draw_feedback_fab(float x, float y, float btn, bool active) {
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                          ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##feedback_fab", nullptr, wf);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + btn, p0.y + btn);
    ImGui::InvisibleButton("##feedback_toggle", ImVec2(btn, btn));
    const bool hov = ImGui::IsItemHovered();
    const ImVec4 fill = active || hov ? theme::color::accent_hi : theme::color::accent;
    const float round = btn * 0.22f;
    dl->AddRectFilled(p0, p1, u32(fill), round);
    dl->AddRect(p0, p1, u32(with_alpha(theme::color::text, 0.12f)), round);
    ImGui::PushFont(theme::fonts().caption);
    if (theme::has_icons()) {
        ImVec2 ts = ImGui::CalcTextSize(ICON_BULLHORN);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(p0.x + (btn - ts.x) * 0.5f, p0.y + (btn - ts.y) * 0.5f),
                    u32(theme::color::bg), ICON_BULLHORN);
    } else {
        ImVec2 ts = ImGui::CalcTextSize("!");
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(p0.x + (btn - ts.x) * 0.5f, p0.y + (btn - ts.y) * 0.5f),
                    u32(theme::color::bg), "!");
    }
    ImGui::PopFont();
    const bool clicked = ImGui::IsItemClicked();

    ImGui::End();
    ImGui::PopStyleVar(2);
    return clicked;
}

void render_feedback_panel_body(AppState& st, float panel_w) {
    ImGui::PushFont(theme::fonts().h2);
    ImGui::TextUnformatted("Give Feedback");
    ImGui::PopFont();
    const float close_x = ImGui::GetWindowContentRegionMax().x - 26.0f;
    ImGui::SameLine(close_x);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, with_alpha(theme::color::text, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, with_alpha(theme::color::text, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    if (ImGui::Button("X##feedback_close", ImVec2(24, 24))) {
        g_feedback.expanded = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::PushFont(theme::fonts().caption);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panel_w - 28.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
    ImGui::TextUnformatted("What's on your mind?");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::PopFont();

    if (!st.dsn_configured) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushFont(theme::fonts().caption);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::warn);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panel_w - 28.0f);
        ImGui::TextUnformatted("SENTRY_DSN is not set - feedback cannot be sent.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::color::surface_hi);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::color::border);
    ImGui::InputTextMultiline("##feedback_message", g_feedback.message,
                              sizeof(g_feedback.message), ImVec2(-FLT_MIN, 72),
                              ImGuiInputTextFlags_None);
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0, 8));
    const bool has_message = g_feedback.message[0] != '\0';
    const bool can_send = has_message && st.dsn_configured;
    if (!can_send) {
        ImGui::BeginDisabled();
    }
    ImGui::PushStyleColor(ImGuiCol_Button, theme::color::accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::color::accent_hi);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::color::accent);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::bg);
    if (ImGui::Button("Send Feedback", ImVec2(-FLT_MIN, 34))) {
        const char* attachment = st.screenshot_path.empty() ? nullptr
                                                            : st.screenshot_path.c_str();
        if (SentryManager::capture_feedback(g_feedback.message, nullptr, st.operator_name.c_str(),
                                            attachment)) {
            if (st.console) {
                st.console->push(ConsoleLog::Level::Info, "sentry",
                                 "User feedback captured (sentry_capture_feedback_with_hint)");
            }
            g_feedback.message[0] = '\0';
            g_feedback.expanded = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::PopStyleColor(4);
    if (!can_send) {
        ImGui::EndDisabled();
    }

    if (!has_message) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushFont(theme::fonts().caption);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
        ImGui::TextUnformatted("Enter a message to send.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
}

// Megaphone in the header; popup is anchored bottom-right as an overlay.
void render_header_feedback(float center_y, float fab, float rx) {
    if (!SentryManager::initialized()) {
        return;
    }

    g_feedback.fab = fab;
    g_feedback.fab_x = rx - fab;
    g_feedback.fab_y = center_y - fab * 0.5f;

    if (draw_feedback_fab(g_feedback.fab_x, g_feedback.fab_y, fab, g_feedback.expanded)) {
        g_feedback.expanded = !g_feedback.expanded;
    }
}

void render_feedback_overlay(AppState& st) {
    if (!SentryManager::initialized() || !g_feedback.expanded) {
        return;
    }

    const float margin = 24.0f;
    const float panel_w = 340.0f;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x - margin,
               vp->WorkPos.y + vp->WorkSize.y - margin),
        ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(panel_w, 0), ImGuiCond_Always);

    ImGui::OpenPopup("##feedback_popup");

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::color::surface);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::color::border);
    ImGuiWindowFlags popup_wf = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::BeginPopup("##feedback_popup", popup_wf)) {
        render_feedback_panel_body(st, panel_w);
        ImGui::EndPopup();
    } else {
        g_feedback.expanded = false;
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void status_dot(ImVec4 col, float radius = 4.5f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float cy = p.y + ImGui::GetTextLineHeight() * 0.5f;
    ImVec2 center(p.x + radius + 1, cy);
    dl->AddCircleFilled(center, radius + 2.5f, u32(with_alpha(col, 0.22f)), 20);
    dl->AddCircleFilled(center, radius, u32(col), 20);
    ImGui::Dummy(ImVec2(radius * 2 + 8, ImGui::GetTextLineHeight()));
}

void chip(const char* text, ImVec4 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pad(9, 3);
    ImVec2 sz = ImGui::CalcTextSize(text);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + sz.x + pad.x * 2, p0.y + sz.y + pad.y * 2);
    dl->AddRectFilled(p0, p1, u32(with_alpha(col, 0.16f)), 20.0f);
    dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y), u32(col), text);
    ImGui::Dummy(ImVec2(sz.x + pad.x * 2, sz.y + pad.y * 2));
}

// One aligned meter line: [icon LABEL] [bar] [value%], all on one baseline.
void meter_row(const char* icon, const char* label, float v, ImVec4 col) {
    float full = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(theme::fonts().caption);
    float fs = ImGui::GetFontSize();
    float row_h = fs + 4;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float cy = p0.y + row_h * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    std::string lbl = with_icon(icon, label);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(p0.x, cy - fs * 0.5f),
                u32(theme::color::text_dim), lbl.c_str());

    const float label_w = 86, value_w = 40, bar_h = 6;
    float bx = p0.x + label_w, bw = full - label_w - value_w;
    dl->AddRectFilled(ImVec2(bx, cy - bar_h * 0.5f), ImVec2(bx + bw, cy + bar_h * 0.5f),
                      u32(theme::color::surface_hi), bar_h * 0.5f);
    dl->AddRectFilled(ImVec2(bx, cy - bar_h * 0.5f), ImVec2(bx + bw * v, cy + bar_h * 0.5f),
                      u32(col), bar_h * 0.5f);

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0f);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(p0.x + full - ts.x, cy - fs * 0.5f),
                u32(theme::color::text_dim), buf);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(full, row_h));
}

bool begin_card(const char* id, float height = 0.0f, ImVec2 pad = ImVec2(16, 14)) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::color::surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
    ImGuiChildFlags cf = ImGuiChildFlags_Borders;
    ImVec2 size(-FLT_MIN, height);
    if (height <= 0.0f) { cf |= ImGuiChildFlags_AutoResizeY; size.y = 0; }
    return ImGui::BeginChild(id, size, cf,
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}
void end_card() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

int column_count(float min_w, int max_cols) {
    float avail = ImGui::GetContentRegionAvail().x;
    int n = static_cast<int>((avail + 14) / (min_w + 14));
    return std::max(1, std::min(n, max_cols));
}

void center_cursor_x(float content_w) {
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > content_w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - content_w) * 0.5f);
}

// ---- Sidebar -------------------------------------------------------------
void brand_mark() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float s = 34;
    center_cursor_x(s);
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), u32(theme::color::accent), 10.0f);
    if (theme::has_icons()) {
        ImGui::PushFont(theme::fonts().h2);
        ImVec2 ts = ImGui::CalcTextSize(ICON_LEAF);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(p.x + (s - ts.x) * 0.5f, p.y + (s - ts.y) * 0.5f),
                    u32(theme::color::bg), ICON_LEAF);
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(s, s));
    ImGui::Dummy(ImVec2(0, 8));

    ImGui::PushFont(theme::fonts().h2);
    float w = ImGui::CalcTextSize("Empower").x + 6 + ImGui::CalcTextSize("Plant").x;
    center_cursor_x(w);
    ImGui::TextUnformatted("Empower");
    ImGui::SameLine(0, 6);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::accent);
    ImGui::TextUnformatted("Plant");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::PushFont(theme::fonts().caption);
    center_cursor_x(ImGui::CalcTextSize("FLEET CONTROL").x);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_faint);
    ImGui::TextUnformatted("FLEET CONTROL");
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void nav_item(const Nav& n, int i, AppState& st) {
    bool selected = st.page == i;
    const float right_margin = 12.0f, row_h = 34.0f;
    float w = ImGui::GetContentRegionAvail().x - right_margin;

    ImGui::PushFont(theme::fonts().bold);
    ImGui::PushStyleColor(ImGuiCol_Header, selected ? with_alpha(theme::color::accent, 0.18f)
                                                    : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::color::surface_hi);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, with_alpha(theme::color::accent, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? theme::color::accent_hi : theme::color::text_dim);
    std::string label = "  " + with_icon(n.icon, n.label); // left text padding inside the row
    if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(w, row_h))) st.page = i;
    if (selected) {
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(mn.x, mn.y + 7), ImVec2(mn.x + 3, mx.y - 7), u32(theme::color::accent), 2);
    }
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
}

void avatar(const char* initials) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = 19;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 c(p.x + r, p.y + r);
    dl->AddCircleFilled(c, r, u32(with_alpha(theme::color::accent, 0.22f)), 32);
    dl->AddCircle(c, r, u32(with_alpha(theme::color::accent, 0.55f)), 32, 1.5f);
    ImGui::PushFont(theme::fonts().body);
    ImVec2 ts = ImGui::CalcTextSize(initials);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), u32(theme::color::accent_hi), initials);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(r * 2, r * 2));
}

std::string initials_of(const std::string& name) {
    std::string out;
    bool at_start = true;
    for (char ch : name) {
        if (ch == ' ') { at_start = true; continue; }
        if (at_start && out.size() < 2) out.push_back((char)std::toupper(ch));
        at_start = false;
    }
    return out.empty() ? "?" : out;
}

void render_sidebar(AppState& st) {
    // Rendered directly into the sidebar wrapper (no nested child) so the brand
    // mark shares the same top padding as the page header and status pill.
    brand_mark();
    ImGui::Dummy(ImVec2(0, 18));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_faint);
    ImGui::PushFont(theme::fonts().caption);
    ImGui::TextUnformatted("MENU");
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 2));

    // Vertically center the icon + label within each (taller) nav row.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    for (int i = 0; i < IM_ARRAYSIZE(kNav); ++i) nav_item(kNav[i], i, st);
    ImGui::PopStyleVar(2);

    // Operator card, raised slightly off the bottom to match the content margin.
    float card_h = 84;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - card_h);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::color::surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(13, 12));
    ImGui::BeginChild("operator", ImVec2(0, card_h), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    avatar(initials_of(st.operator_name).c_str());
    ImGui::SameLine(0, 11);
    ImGui::BeginGroup();
    ImGui::TextUnformatted(st.operator_name.c_str());
    ImGui::PushFont(theme::fonts().caption);
    dim_text("Operator  -  %s", st.environment.c_str());
    status_dot(st.dsn_configured ? theme::color::ok : theme::color::warn, 3.5f);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, st.dsn_configured ? theme::color::ok : theme::color::warn);
    ImGui::TextUnformatted(st.dsn_configured ? "connected" : "no dsn");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---- Header --------------------------------------------------------------
void render_header(AppState& st) {
    FleetModel& f = *st.fleet;
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::PushFont(theme::fonts().h1);
    float h1h = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    heading(theme::fonts().h1, with_icon(kNav[st.page].icon, kNav[st.page].label).c_str());

    // Right-aligned status pill, vertically centered on the title.
    float cy = tp.y + h1h * 0.5f;
    char online[24];
    std::snprintf(online, sizeof(online), "%d online", f.online_count());
    const char* env = st.environment.c_str();
    ImGui::PushFont(theme::fonts().body);
    float fs = ImGui::GetFontSize();
    float ow = ImGui::CalcTextSize(online).x, ew = ImGui::CalcTextSize(env).x;
    const float padx = 14, dotr = 4, gap = 9, sep = 12, h = fs + 12;
    float w = padx + dotr * 2 + gap + ow + sep + 1 + sep + ew + padx;
    float rx = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float fab = h;
    const float cluster_gap = 8.0f;
    const float pill_r = rx - fab - cluster_gap;
    const float pill_l = pill_r - w;
    render_header_feedback(cy, fab, rx);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(pill_l, cy - h * 0.5f), b(pill_r, cy + h * 0.5f);
    dl->AddRectFilled(a, b, u32(theme::color::surface_hi), h * 0.5f);
    dl->AddRect(a, b, u32(theme::color::border), h * 0.5f);
    float x = a.x + padx;
    ImVec4 dc = f.alert_count() ? theme::color::warn : theme::color::ok;
    dl->AddCircleFilled(ImVec2(x + dotr, cy), dotr + 2, u32(with_alpha(dc, 0.3f)));
    dl->AddCircleFilled(ImVec2(x + dotr, cy), dotr, u32(dc));
    x += dotr * 2 + gap;
    dl->AddText(ImGui::GetFont(), fs, ImVec2(x, cy - fs * 0.5f), u32(theme::color::text), online);
    x += ow + sep;
    dl->AddLine(ImVec2(x, cy - 7), ImVec2(x, cy + 7), u32(theme::color::border));
    x += sep;
    dl->AddText(ImGui::GetFont(), fs, ImVec2(x, cy - fs * 0.5f), u32(theme::color::accent_hi), env);
    ImGui::PopFont();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
    ImGui::TextUnformatted(kNav[st.page].subtitle);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 8));
}

void crash_last_run_banner(AppState& st) {
    if (st.dismiss_crash_banner || !SentryManager::crashed_last_run()) {
        return;
    }

    if (begin_card("crash_banner", 64, ImVec2(16, 12))) {
        status_dot(theme::color::danger);
        ImGui::SameLine(0, 8);
        ImGui::BeginGroup();
        ImGui::PushFont(theme::fonts().h2);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::danger);
        ImGui::TextUnformatted(with_icon(ICON_BUG, "Last session crashed").c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PushFont(theme::fonts().caption);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
        ImGui::TextUnformatted(
            "sentry_get_crashed_last_run() detected a crash marker from the previous run.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::EndGroup();

        const float btn_w = 72.f;
        const float row_h = ImGui::GetFrameHeight();
        const float y0 = ImGui::GetWindowPos().y + ImGui::GetStyle().WindowPadding.y;
        const float y_mid = y0 + (ImGui::GetWindowHeight() - 2.f * ImGui::GetStyle().WindowPadding.y
                                  - row_h) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(
            ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - btn_w, y_mid));
        if (ImGui::Button("Dismiss", ImVec2(btn_w, row_h))) {
            st.dismiss_crash_banner = true;
        }
    }
    end_card();
    ImGui::Dummy(ImVec2(0, 6));
}

// ---- Fleet ---------------------------------------------------------------
void stat_tile(const char* label, const char* value, ImVec4 col) {
    if (begin_card(label, 96)) {
        // Label pinned to the top of the card.
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_faint);
        ImGui::PushFont(theme::fonts().caption);
        ImGui::TextUnformatted(label);
        ImGui::PopFont();
        ImGui::PopStyleColor();

        // Center the value within the empty space below the label.
        ImGui::PushFont(theme::fonts().h1);
        float hh = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        float availY = ImGui::GetContentRegionAvail().y;
        // Bias upward a little: the h1 line box has trailing descender space, so
        // a true center reads as sitting too low.
        if (availY > hh)
            ImGui::Dummy(ImVec2(0, std::max(0.0f, (availY - hh) * 0.5f - 7.0f)));
        heading(theme::fonts().h1, value, col);
    }
    end_card();
}

void page_fleet(AppState& st) {
    FleetModel& f = *st.fleet;
    const auto& devices = f.devices();

    float soil_sum = 0;
    for (const Device& d : devices) soil_sum += d.soil_moisture;
    float soil_avg = devices.empty() ? 0 : soil_sum / devices.size();

    char online[16], alerts[8], soil[8], queue[8];
    std::snprintf(online, sizeof(online), "%d/%d", f.online_count(), (int)devices.size());
    std::snprintf(alerts, sizeof(alerts), "%d", f.alert_count());
    std::snprintf(soil, sizeof(soil), "%.0f%%", soil_avg * 100.0f);
    std::snprintf(queue, sizeof(queue), "%d", f.queue_depth());

    if (ImGui::BeginTable("stats", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); stat_tile("DEVICES ONLINE", online, theme::color::ok);
        ImGui::TableNextColumn(); stat_tile("ACTIVE ALERTS", alerts,
            f.alert_count() ? theme::color::warn : theme::color::text_dim);
        ImGui::TableNextColumn(); stat_tile("AVG SOIL MOISTURE", soil, theme::color::accent_hi);
        ImGui::TableNextColumn(); stat_tile("JOB QUEUE", queue, theme::color::info);
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0, 4));

    int cols = column_count(248, 4);
    if (ImGui::BeginTable("devices", cols, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < (int)devices.size(); ++i) {
            ImGui::TableNextColumn();
            const Device& d = devices[i];
            ImGui::PushID(i);
            if (begin_card("card")) {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 9));
                status_dot(status_color(d.status));
                ImGui::SameLine();
                ImGui::PushFont(theme::fonts().h2);
                ImGui::TextUnformatted(d.name.c_str());
                ImGui::PopFont();
                ImGui::SameLine();
                ImGui::PushFont(theme::fonts().caption);
                ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x -
                                     ImGui::CalcTextSize(d.firmware.c_str()).x);
                dim_text("%s", d.firmware.c_str());
                dim_text("%s  -  %s", d.id.c_str(), d.location.c_str());
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0, 2));
                meter_row(ICON_DROPLET, "SOIL", d.soil_moisture, theme::color::ok);
                meter_row(ICON_SUN, "LIGHT", d.light, theme::color::warn);
                meter_row(ICON_BATTERY, "BATTERY", d.battery,
                          d.battery < 0.2f ? theme::color::danger : theme::color::accent);
                ImGui::PopStyleVar();
            }
            end_card();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ---- Telemetry: live metrics + logs streaming to Sentry ------------------
void series_stats(const Series& s, float& lo, float& hi, float& avg) {
    lo = 1e9f; hi = -1e9f; float sum = 0;
    for (float v : s.data) { lo = std::min(lo, v); hi = std::max(hi, v); sum += v; }
    avg = sum / Series::kLen;
}

// A circular arc gauge with a centered value and a label below.
void gauge_card(const char* label, float frac, ImVec4 col, const char* value, float height) {
    frac = std::max(0.0f, std::min(frac, 1.0f));
    if (begin_card(label, height)) {
        ImVec2 region = ImGui::GetContentRegionAvail();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float r = 40;
        ImVec2 c(p.x + region.x * 0.5f, p.y + r + 6);
        const float kPi = 3.14159265f;
        float a0 = kPi * 0.75f, a1 = kPi * 2.25f, th = 7;
        dl->PathArcTo(c, r, a0, a1, 64);
        dl->PathStroke(u32(theme::color::surface_hi), 0, th);
        if (frac > 0.001f) {
            dl->PathArcTo(c, r, a0, a0 + (a1 - a0) * frac, 64);
            dl->PathStroke(u32(col), 0, th);
        }
        ImGui::PushFont(theme::fonts().h2);
        ImVec2 ts = ImGui::CalcTextSize(value);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), u32(theme::color::text), value);
        ImGui::PopFont();
        ImGui::PushFont(theme::fonts().caption);
        float lw = ImGui::CalcTextSize(label).x;
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(c.x - lw * 0.5f, c.y + r + 12), u32(theme::color::text_dim), label);
        ImGui::PopFont();
    }
    end_card();
}

void metric_table_row(const char* name, const char* value, const char* type,
                      bool blocked = false) {
    const ImVec4 faint = theme::color::text_faint;
    ImGui::TableNextColumn();
    ImGui::PushFont(theme::fonts().caption);
    ImGui::PushStyleColor(ImGuiCol_Text, blocked ? faint : theme::color::accent_hi);
    ImGui::TextUnformatted(name);
    ImGui::PopStyleColor();
    ImGui::TableNextColumn();
    if (blocked) ImGui::PushStyleColor(ImGuiCol_Text, faint);
    ImGui::TextUnformatted(value);
    if (blocked) ImGui::PopStyleColor();
    ImGui::TableNextColumn();
    if (blocked) {
        ImGui::PushStyleColor(ImGuiCol_Text, faint);
        ImGui::TextUnformatted("blocked");
        ImGui::PopStyleColor();
    } else {
        dim_text("%s", type);
    }
    ImGui::PopFont();
}

void telemetry_log_feed(const TelemetryFeed* feed) {
    ImGui::PushFont(theme::fonts().caption);
    if (!feed || feed->logs().empty()) {
        dim_text("waiting for sentry_log_* …");
        ImGui::PopFont();
        return;
    }
    for (const auto& ln : feed->logs()) {
        if (ln.blocked) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_faint);
            ImGui::Text("%s  blocked  %s", ln.time.c_str(), ln.body.c_str());
            ImGui::PopStyleColor();
            continue;
        }
        ImVec4 level_col = theme::color::text_dim;
        if (ln.level == "warn") level_col = theme::color::warn;
        else if (ln.level == "error" || ln.level == "fatal") level_col = theme::color::danger;
        else if (ln.level == "info") level_col = theme::color::ok;
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_faint);
        ImGui::Text("%s", ln.time.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, level_col);
        ImGui::Text("%-5s", ln.level.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(ln.body.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::PopFont();
}

void page_telemetry(AppState& st) {
    FleetModel& f = *st.fleet;
    char v0[16], v1[16], v2[16], v3[16];
    std::snprintf(v0, sizeof(v0), "%.0f", f.frame_time().latest());
    std::snprintf(v1, sizeof(v1), "%.0f%%", f.cpu_load().latest() * 100.0f);
    std::snprintf(v2, sizeof(v2), "%.0f", f.net_latency().latest());
    std::snprintf(v3, sizeof(v3), "%.0f%%", f.soil_avg().latest() * 100.0f);

    const float gauge_h = 150;
    if (ImGui::BeginTable("gauges", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); gauge_card("frame time (ms)", f.frame_time().latest() / 33.0f, theme::color::accent, v0, gauge_h);
        ImGui::TableNextColumn(); gauge_card("cpu load", f.cpu_load().latest(), theme::color::info, v1, gauge_h);
        ImGui::TableNextColumn(); gauge_card("backend latency (ms)", f.net_latency().latest() / 90.0f, theme::color::warn, v2, gauge_h);
        ImGui::TableNextColumn(); gauge_card("soil moisture", f.soil_avg().latest(), theme::color::ok, v3, gauge_h);
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0, 4));
    // Subtract the feeds table's cell padding so the cards end at the content
    // bottom (level with the sidebar operator card) instead of overshooting it.
    float row_h = ImGui::GetContentRegionAvail().y - 18.0f;
    if (ImGui::BeginTable("feeds", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (begin_card("metrics", row_h)) {
            heading(theme::fonts().h2, with_icon(ICON_CHART, "Metrics").c_str());
            ImGui::PushFont(theme::fonts().caption);
            dim_text("live from before_send_metric");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 6));
            const auto& metrics =
                st.telemetry ? st.telemetry->metrics()
                             : std::vector<TelemetryFeed::Metric>{};
            if (ImGui::BeginTable("mt", 3,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
                ImGui::TableSetupColumn("metric", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, 86);
                if (metrics.empty()) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    dim_text("waiting for sentry_metrics_* …");
                } else {
                    for (const auto& m : metrics) {
                        ImGui::TableNextRow();
                        metric_table_row(
                            m.name.c_str(), m.value.c_str(), m.type.c_str(), m.blocked);
                    }
                }
                ImGui::EndTable();
            }
        }
        end_card();

        ImGui::TableNextColumn();
        if (begin_card("logs", row_h)) {
            heading(theme::fonts().h2, with_icon(ICON_TERMINAL, "Logs").c_str());
            ImGui::PushFont(theme::fonts().caption);
            dim_text("live from before_send_log");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::BeginChild("logscroll", ImVec2(0, 0), false);
            telemetry_log_feed(st.telemetry);
            ImGui::EndChild();
        }
        end_card();
        ImGui::EndTable();
    }
}

// ---- Pipelines -----------------------------------------------------------
void page_pipelines(AppState&) {
    struct Row { const char* name; const char* op; float speed; ImVec4 col; };
    static const Row rows[] = {
        {"Image processing", "thumbnail and classify plant photos", 0.30f, theme::color::accent},
        {"Sensor pipeline", "ingest soil, light and temperature samples", 0.55f, theme::color::ok},
        {"Firmware flasher", "stage OTA firmware to devices", 0.12f, theme::color::danger},
        {"Telemetry sync", "push rollups to the backend", 0.42f, theme::color::warn},
    };
    float t = (float)ImGui::GetTime();
    for (int i = 0; i < IM_ARRAYSIZE(rows); ++i) {
        const Row& r = rows[i];
        ImGui::PushID(i);
        if (begin_card("p")) {
            ImGui::PushFont(theme::fonts().h2);
            ImGui::TextUnformatted(r.name);
            ImGui::PopFont();
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 64);
            chip("running", r.col);
            ImGui::PushFont(theme::fonts().caption);
            dim_text("%s", r.op);
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 4));
            float prog = 0.5f + 0.5f * std::sin(t * r.speed + i);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, r.col);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::color::surface_hi);
            ImGui::ProgressBar(prog, ImVec2(-FLT_MIN, 8), "");
            ImGui::PopStyleColor(2);
        }
        end_card();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 2));
    }
}

// ---- Chaos Lab -----------------------------------------------------------
ImVec4 severity_color(Severity s) {
    switch (s) {
        case Severity::Crash: return theme::color::danger;
        case Severity::Warning: return theme::color::warn;
        case Severity::Backend: return theme::color::info;
        case Severity::Message: return theme::color::ok;
    }
    return theme::color::accent;
}

// Drain animation. One envelope flies off per envelope that actually left the
// on-disk queue, so the visual tracks real upload progress: the SDK drains the
// retry outbox with one blocking request at a time on a single worker thread,
// so a fixed-length sweep would finish long before the uploads do.
struct DrainSprite {
    float t = 0.f;    // <0 while staggered, 0 -> 1 in flight, then retired
    float lane = 0.f; // lateral offset so a burst doesn't overlap exactly
};
constexpr int kMaxDrainSprites = 8;
DrainSprite g_drain[kMaxDrainSprites];
int g_drain_count = 0;
int g_last_queued = -1; // -1 = not observed yet, so the first frame never spawns
ImVec2 g_drain_origin{};

void spawn_drain_envelopes(int n) {
    for (int i = 0; i < n && g_drain_count < kMaxDrainSprites; ++i) {
        g_drain[g_drain_count].t = -0.06f * static_cast<float>(i);
        g_drain[g_drain_count].lane = static_cast<float>((g_drain_count % 3) - 1);
        ++g_drain_count;
    }
}

void draw_drain_envelopes(ImDrawList* dl) {
    if (g_drain_count <= 0) return;
    const float dt = ImGui::GetIO().DeltaTime;
    ImFont* font = theme::has_icons() ? theme::fonts().h2 : theme::fonts().body;
    const char* glyph = theme::has_icons() ? ICON_ENVELOPE : "E";

    int live = 0;
    for (int i = 0; i < g_drain_count; ++i) {
        DrainSprite s = g_drain[i];
        s.t += dt / 0.85f; // ~0.85s flight
        if (s.t >= 1.f) continue;
        g_drain[live++] = s; // compact in place; live <= i, so nothing unread is clobbered
        if (s.t < 0.f) continue;

        const float ease = s.t * s.t; // accelerate away
        const float x = g_drain_origin.x + 40.f + ease * 220.f;
        const float y = g_drain_origin.y - ease * 120.f + s.lane * 10.f;
        const float alpha = (1.f - s.t) * 0.95f;
        dl->AddText(font, font->FontSize, ImVec2(x, y),
                    u32(with_alpha(theme::color::info, alpha)), glyph);
    }
    g_drain_count = live;
}

void chaos_offline_bar(AppState& st) {
    const bool demo_offline = SentryManager::is_offline();
    const bool consent = SentryManager::has_user_consent();
    const bool uploading = consent && !demo_offline;
    const int queued = static_cast<int>(OfflineQueueMonitor::queued_count());

    // Three visible states — consent revoked always wins the title.
    enum class Gate { Online, DemoOffline, ConsentRevoked };
    Gate gate = Gate::Online;
    if (!consent) gate = Gate::ConsentRevoked;
    else if (demo_offline) gate = Gate::DemoOffline;

    ImVec4 status_col = theme::color::ok;
    const char* title = "Online - uploading";
    const char* hint = "Toggle Offline, trigger non-fatal faults, then come back online to flush them.";
    switch (gate) {
        case Gate::Online:
            break;
        case Gate::DemoOffline:
            status_col = theme::color::warn;
            title = "Offline - caching envelopes";
            hint = "Demo offline. Trigger non-fatal faults to fill the local queue.";
            break;
        case Gate::ConsentRevoked:
            status_col = theme::color::danger;
            title = "Consent revoked - caching";
            hint = demo_offline
                ? "Consent blocks uploads (Settings). Demo offline is also on."
                : "Uploads blocked in Settings. Give consent there to drain.";
            break;
    }

    if (begin_card("offline_bar", 64, ImVec2(16, 12))) {
        status_dot(status_col);
        ImGui::SameLine(0, 8);
        ImGui::BeginGroup();
        ImGui::PushFont(theme::fonts().h2);
        ImGui::PushStyleColor(ImGuiCol_Text, status_col);
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PushFont(theme::fonts().caption);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
        ImGui::TextUnformatted(hint);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::EndGroup();

        // Right: envelope badge + Go Offline / Go Online (vertically centered).
        const float right_w = 180.f;
        const float row_h = ImGui::GetFrameHeight();
        const float y0 = ImGui::GetWindowPos().y + ImGui::GetStyle().WindowPadding.y;
        const float y_mid = y0 + (ImGui::GetWindowHeight() - 2.f * ImGui::GetStyle().WindowPadding.y
                                  - row_h) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(
            ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - right_w, y_mid));

        ImVec2 badge_min = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* ifont = theme::fonts().body;
        const char* envelope = theme::has_icons() ? ICON_ENVELOPE : "E";
        const float icon_sz = theme::fonts().body->FontSize + 2.f;
        const float pad_x = 4.f;
        const float hit_w = pad_x + icon_sz + 12.f;
        ImVec2 b0 = badge_min;
        ImVec2 b1(b0.x + hit_w, b0.y + row_h);

        const ImVec4 env_col = uploading ? theme::color::text_dim : status_col;
        const ImVec2 icon_pos(b0.x + pad_x, b0.y + (row_h - icon_sz) * 0.5f);
        dl->AddText(ifont, icon_sz, icon_pos, u32(env_col), envelope);

        // Fly one envelope off for each one that actually left the disk queue.
        g_drain_origin = ImVec2((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
        if (uploading && g_last_queued > queued) {
            spawn_drain_envelopes(g_last_queued - queued);
        }
        g_last_queued = queued;

        if (queued > 0) {
            char count_buf[8];
            if (queued > 99) std::snprintf(count_buf, sizeof(count_buf), "99+");
            else std::snprintf(count_buf, sizeof(count_buf), "%d", queued);

            ImFont* nfont = theme::fonts().caption;
            const float nsz = 11.f;
            ImVec2 ts = nfont->CalcTextSizeA(nsz, FLT_MAX, 0.f, count_buf);
            const float cr = (queued > 9) ? 8.5f : 7.5f;
            ImVec2 center(icon_pos.x + icon_sz - 1.f, icon_pos.y + 1.5f);
            dl->AddCircleFilled(center, cr, u32(theme::color::danger), 20);
            dl->AddText(nfont, nsz,
                        ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f - 0.5f),
                        u32(ImVec4(1.f, 1.f, 1.f, 1.f)), count_buf);
        }

        ImGui::Dummy(ImVec2(hit_w, row_h));
        ImGui::SameLine(0, 10);

        ImVec4 btn = demo_offline ? theme::color::ok : theme::color::warn;
        const char* label = demo_offline ? "Go Online" : "Go Offline";
        ImGui::PushStyleColor(ImGuiCol_Button, with_alpha(btn, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, with_alpha(btn, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, btn);
        ImGui::PushStyleColor(ImGuiCol_Text, btn);
        if (ImGui::Button(with_icon(demo_offline ? ICON_CHECK : ICON_BOLT, label).c_str(),
                          ImVec2(120.f, row_h))) {
            const bool next = !demo_offline;
            SentryManager::set_offline(next);
            if (st.console) {
                if (next) {
                    st.console->push(ConsoleLog::Level::Warn, "sentry",
                                     "Go Offline: demo pause - caching envelopes");
                } else if (!consent) {
                    st.console->push(ConsoleLog::Level::Warn, "sentry",
                                     "Go Online: demo cleared, but consent still revoked in Settings");
                } else {
                    st.console->push(ConsoleLog::Level::Info, "sentry",
                                     "Go Online: draining cached envelopes to Sentry");
                }
            }
        }
        ImGui::PopStyleColor(4);
    }
    end_card();

    // if drawn before end_card, envelope icons get clipped
    draw_drain_envelopes(ImGui::GetForegroundDrawList());

    ImGui::Dummy(ImVec2(0, 6));
}

void page_chaos(AppState& st) {
    chaos_offline_bar(st);

    const auto& actions = scenarios();
    int cols = column_count(258, 4);
    const int rows = std::max(1, (static_cast<int>(actions.size()) + cols - 1) / cols);

    // Fit the grid into the remaining viewport height (no page scroll).
    const float gap = 8.f;
    const float avail = ImGui::GetContentRegionAvail().y;
    const float pad_y = 3.f;
    float card_h = (avail - pad_y * 2.f * static_cast<float>(rows)) / static_cast<float>(rows);
    if (card_h > 150.f) card_h = 150.f;

    if (ImGui::BeginTable("chaos", cols,
                          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(gap * 0.5f, pad_y));
        for (int i = 0; i < (int)actions.size(); ++i) {
            ImGui::TableNextColumn();
            const ChaosScenario& a = actions[i];
            ImVec4 col = severity_color(a.severity);
            ImGui::PushID(a.id);
            if (begin_card("c", card_h, ImVec2(14, 10))) {
                heading(theme::fonts().h2, a.label, col);
                ImGui::Dummy(ImVec2(0, 1));
                ImGui::PushFont(theme::fonts().caption);
                ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(a.desc);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::PopFont();

                float bh = ImGui::GetFrameHeight() + 2;
                ImGui::SetCursorPosY(ImGui::GetWindowHeight() - bh - 12);
                ImGui::PushStyleColor(ImGuiCol_Button, with_alpha(col, 0.16f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, with_alpha(col, 0.30f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::PushFont(theme::fonts().h2);
                if (ImGui::Button(with_icon(ICON_BOLT, "Trigger fault").c_str(),
                                  ImVec2(-FLT_MIN, bh)) && st.on_chaos) {
                    st.on_chaos(a.id);
                }
                ImGui::PopFont();
                ImGui::PopStyleColor(4);
            }
            end_card();
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::EndTable();
    }
}

// ---- Settings ------------------------------------------------------------
void kv_row(const char* k, const char* v) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
    ImGui::TextUnformatted(k);
    ImGui::PopStyleColor();
    ImGui::SameLine(190);
    ImGui::TextUnformatted(v);
}

void section(const char* title) {
    heading(theme::fonts().h2, title);
    ImGui::Dummy(ImVec2(0, 6));
}

void feature_row(const char* label) {
    ImGui::PushFont(theme::fonts().body);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::ok);
    ImGui::TextUnformatted(theme::has_icons() ? ICON_CHECK : "+");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine(0, 12);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    float rx = ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("enabled").x;
    ImGui::SetCursorPosX(rx);
    ImGui::PushFont(theme::fonts().caption);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::color::ok);
    ImGui::TextUnformatted("enabled");
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void page_settings(AppState& st) {
    if (ImGui::BeginTable("set", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (begin_card("left")) {
            section("Sentry SDK");
            kv_row("SDK", "sentry.native 0.16.0");
            kv_row("Crash backend", "native (out-of-process)");
            kv_row("Minidump mode", "smart + client stackwalk");
            kv_row("Upload mode", "async");
            kv_row("Cache keep", "always");
            kv_row("HTTP retry", "enabled");
            ImGui::Dummy(ImVec2(0, 16));
            section("Connection");
            kv_row("Environment", st.environment.c_str());
            kv_row("Release", st.release.c_str());
            kv_row("Ingest host", st.dsn_configured ? st.dsn_host.c_str() : "(SENTRY_DSN not set)");
            kv_row("Backend", "flask.empower-plant.com");
            ImGui::Dummy(ImVec2(0, 16));

            // GDPR-style consent (Settings) — separate from Chaos Lab Go Offline.
            section("User consent");
            ImGui::PushFont(theme::fonts().caption);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::color::text_dim);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(
                "Upload consent for Sentry. Revoking caches new envelopes on "
                "disk until consent is given again. Chaos Lab's Go Offline is a "
                "separate demo switch; both must allow uploads for sending.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 8));

            const bool consent = SentryManager::has_user_consent();
            ImVec4 consent_col = consent ? theme::color::ok : theme::color::warn;
            status_dot(consent_col);
            ImGui::SameLine(0, 8);
            chip(consent ? "CONSENT GIVEN" : "CONSENT REVOKED", consent_col);
            ImGui::SameLine(0, 12);
            if (SentryManager::is_offline()) {
                chip("Chaos Lab offline", theme::color::text_faint);
            }

            ImGui::Dummy(ImVec2(0, 10));
            ImVec4 btn = consent ? theme::color::warn : theme::color::ok;
            const char* label = consent ? "Revoke consent" : "Give consent";
            ImGui::PushStyleColor(ImGuiCol_Button, with_alpha(btn, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, with_alpha(btn, 0.32f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, btn);
            ImGui::PushStyleColor(ImGuiCol_Text, btn);
            ImGui::PushFont(theme::fonts().h2);
            if (ImGui::Button(with_icon(consent ? ICON_BOLT : ICON_CHECK, label).c_str(),
                              ImVec2(-FLT_MIN, ImGui::GetFrameHeight() + 8))) {
                const bool next = !consent;
                SentryManager::set_user_consent(next);
                if (st.console) {
                    st.console->push(
                        next ? ConsoleLog::Level::Info : ConsoleLog::Level::Warn, "sentry",
                        next ? "User consent given - uploads allowed (if not demo-offline)"
                             : "User consent revoked - caching envelopes until given");
                }
            }
            ImGui::PopFont();
            ImGui::PopStyleColor(4);
        }
        end_card();

        ImGui::TableNextColumn();
        if (begin_card("right")) {
            section("Telemetry filters");
            ImGui::PushFont(theme::fonts().caption);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

            bool block_info = SentryManager::is_telemetry_blocked("info");
            if (ImGui::Checkbox("Block info logs", &block_info)) {
                SentryManager::set_telemetry_blocked("info", block_info);
            }
            bool block_cpu = SentryManager::is_telemetry_blocked("fleet.cpu_load");
            if (ImGui::Checkbox("Block fleet.cpu_load metric", &block_cpu)) {
                SentryManager::set_telemetry_blocked("fleet.cpu_load", block_cpu);
            }
            bool block_checkout = SentryManager::is_telemetry_blocked("checkout");
            if (ImGui::Checkbox("Block checkout transactions", &block_checkout)) {
                SentryManager::set_telemetry_blocked("checkout", block_checkout);
            }

            ImGui::PopStyleVar(2);
            ImGui::PopFont();

            ImGui::Dummy(ImVec2(0, 16));
            section("Enabled features");
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 11));
            const char* feats[] = {
                "Performance tracing", "Distributed tracing", "Structured logs",
                "Metrics", "Sessions / release health", "App-hang detection",
                "Screenshots", "External crash reporter", "Programmatic user feedback",
                "User consent", "Offline cache keep", "HTTP retry / drain"};
            for (const char* fe : feats) feature_row(fe);
            ImGui::PopStyleVar();
        }
        end_card();
        ImGui::EndTable();
    }
}

} // namespace

void render_ui(AppState& st) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr, flags);
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 28));
    ImGui::BeginChild("sidebar_wrap", ImVec2(252, 0), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    render_sidebar(st);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine(0, 16);

    // The content column spans the full window height (its bottom lines up with
    // the sidebar's operator card). Pages taller than that scroll inside here
    // rather than spilling past the bottom edge. Chaos Lab sizes itself to fit,
    // so it opts out of scrolling.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(34, 28));
    ImGuiWindowFlags content_flags = 0;
    if (st.page == 3) {
        content_flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }
    ImGui::BeginChild("content", ImVec2(0, 0), false, content_flags);
    render_header(st);
    crash_last_run_banner(st);
    switch (st.page) {
        case 0: page_fleet(st); break;
        case 1: page_telemetry(st); break;
        case 2: page_pipelines(st); break;
        case 3: page_chaos(st); break;
        case 4: page_settings(st); break;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    render_feedback_overlay(st);

    ImGui::End();
}

} // namespace empower
