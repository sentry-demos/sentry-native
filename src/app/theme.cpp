#include "app/theme.h"
#include "core/platform.h"

#include <cstdio>
#include <string>

namespace empower::theme {

namespace {
Fonts g_fonts;
bool g_have_icons = false;
std::string g_fa_path;

std::string font_path(const char* file) {
    std::string p = path_join(path_join(executable_dir(), "assets"), "fonts");
    p = path_join(p, file);
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
        std::fclose(f);
        return p;
    }
    return "";
}

ImFont* add(const std::string& path, float size, float scale) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;
    ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size * scale, &cfg);

    // Merge the Font Awesome glyphs into this font, sized close to the text.
    if (!g_fa_path.empty()) {
        static const ImWchar range[] = {0xf000, 0xf8ff, 0};
        ImFontConfig icfg;
        icfg.MergeMode = true;
        icfg.PixelSnapH = true;
        icfg.GlyphMinAdvanceX = size * scale * 1.15f;
        icfg.GlyphOffset.y = 2.0f * scale;
        io.Fonts->AddFontFromFileTTF(g_fa_path.c_str(), size * scale * 0.95f, &icfg, range);
        g_have_icons = true;
    }
    return font;
}
} // namespace

const Fonts& fonts() { return g_fonts; }
bool has_icons() { return g_have_icons; }

void load_fonts(float dpi_scale) {
    ImGuiIO& io = ImGui::GetIO();
    if (dpi_scale < 1.0f) dpi_scale = 1.0f;

    const std::string regular = font_path("Rubik-Regular.ttf");
    const std::string medium = font_path("Rubik-Medium.ttf");
    g_fa_path = font_path("fa-solid-900.ttf");

    if (!regular.empty()) {
        const std::string& med = medium.empty() ? regular : medium;
        g_fonts.body = add(regular, 15.5f, dpi_scale);
        g_fonts.bold = add(med, 15.5f, dpi_scale);
        g_fonts.caption = add(regular, 13.0f, dpi_scale);
        g_fonts.h2 = add(med, 18.0f, dpi_scale);
        g_fonts.h1 = add(med, 25.0f, dpi_scale);
    } else {
        ImFontConfig cfg;
        cfg.SizePixels = 15.0f * dpi_scale;
        g_fonts.body = io.Fonts->AddFontDefault(&cfg);
        g_fonts.bold = g_fonts.body;
        g_fonts.caption = g_fonts.body;
        g_fonts.h1 = g_fonts.body;
        g_fonts.h2 = g_fonts.body;
    }

    // Glyphs are rasterized at dpi_scale and drawn back down, so layout stays in
    // logical units while text renders crisply on high-DPI displays.
    io.FontGlobalScale = 1.0f / dpi_scale;
}

void apply() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding = 12.0f;
    s.ChildRounding = 12.0f;
    s.FrameRounding = 8.0f;
    s.PopupRounding = 8.0f;
    s.GrabRounding = 6.0f;
    s.TabRounding = 8.0f;
    s.ScrollbarRounding = 9.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;
    s.WindowPadding = ImVec2(20, 20);
    s.FramePadding = ImVec2(14, 9);
    s.CellPadding = ImVec2(10, 8);
    s.ItemSpacing = ImVec2(12, 12);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 11.0f;
    s.GrabMinSize = 12.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = color::bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = color::surface;
    c[ImGuiCol_Border] = color::border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Text] = color::text;
    c[ImGuiCol_TextDisabled] = color::text_faint;
    c[ImGuiCol_TextLink] = color::sentry;

    c[ImGuiCol_FrameBg] = color::surface_hi;
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.180f, 0.149f, 0.286f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.212f, 0.176f, 0.329f, 1.0f);

    c[ImGuiCol_Button] = color::surface_hi;
    c[ImGuiCol_ButtonHovered] = color::border;
    c[ImGuiCol_ButtonActive] = color::accent;

    c[ImGuiCol_Header] = color::surface_hi;
    c[ImGuiCol_HeaderHovered] = color::border;
    c[ImGuiCol_HeaderActive] = color::accent;

    c[ImGuiCol_TitleBg] = color::surface;
    c[ImGuiCol_TitleBgActive] = color::surface;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = color::border;
    c[ImGuiCol_ScrollbarGrabHovered] = color::accent;
    c[ImGuiCol_ScrollbarGrabActive] = color::accent_hi;

    c[ImGuiCol_SliderGrab] = color::accent;
    c[ImGuiCol_SliderGrabActive] = color::accent_hi;
    c[ImGuiCol_CheckMark] = color::accent;

    c[ImGuiCol_Separator] = color::border;
    c[ImGuiCol_SeparatorHovered] = color::accent;
    c[ImGuiCol_PlotLines] = color::accent;
    c[ImGuiCol_PlotLinesHovered] = color::accent_hi;
    c[ImGuiCol_PlotHistogram] = color::accent;
    c[ImGuiCol_PlotHistogramHovered] = color::accent_hi;
    c[ImGuiCol_TextSelectedBg] = ImVec4(color::accent.x, color::accent.y, color::accent.z, 0.35f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0, 0, 0, 0);
}

} // namespace empower::theme
