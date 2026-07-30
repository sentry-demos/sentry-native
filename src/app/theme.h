#pragma once

#include "imgui.h"

// Cohesive dark theme for the Fleet Control Center.
namespace empower::theme {

namespace color {
constexpr ImVec4 bg        = ImVec4(0.078f, 0.063f, 0.122f, 1.00f); // #14101F
constexpr ImVec4 surface   = ImVec4(0.110f, 0.090f, 0.188f, 1.00f); // #1C1730
constexpr ImVec4 surface_hi= ImVec4(0.145f, 0.118f, 0.235f, 1.00f); // #251E3C
constexpr ImVec4 border    = ImVec4(0.180f, 0.153f, 0.278f, 1.00f); // #2E2747
constexpr ImVec4 accent    = ImVec4(0.545f, 0.361f, 0.965f, 1.00f); // #8B5CF6
constexpr ImVec4 accent_hi = ImVec4(0.655f, 0.545f, 0.980f, 1.00f); // #A78BFA
constexpr ImVec4 text      = ImVec4(0.941f, 0.933f, 0.969f, 1.00f); // #F0EEF7
constexpr ImVec4 text_dim  = ImVec4(0.678f, 0.651f, 0.780f, 1.00f); // #ADA6C7
constexpr ImVec4 text_faint= ImVec4(0.451f, 0.431f, 0.545f, 1.00f); // #736E8B
constexpr ImVec4 ok        = ImVec4(0.306f, 0.843f, 0.627f, 1.00f); // #4ED7A0
constexpr ImVec4 warn      = ImVec4(0.984f, 0.706f, 0.271f, 1.00f); // #FBB445
constexpr ImVec4 danger    = ImVec4(0.984f, 0.431f, 0.447f, 1.00f); // #FB6E72
constexpr ImVec4 info      = ImVec4(0.357f, 0.659f, 1.000f, 1.00f); // #5BA8FF
} // namespace color

struct Fonts {
    ImFont* body = nullptr;   // default UI text
    ImFont* bold = nullptr;   // medium-weight body (nav, emphasis)
    ImFont* caption = nullptr;  // captions / labels
    ImFont* h1 = nullptr;     // page titles, big numbers
    ImFont* h2 = nullptr;     // card titles / section heads
};

const Fonts& fonts();

// True when the Font Awesome icon glyphs were merged successfully.
bool has_icons();

// Loads fonts into the current ImGui context. `dpi_scale` is the framebuffer/
// window ratio (e.g. 2.0 on macOS Retina) so glyphs render crisply.
void load_fonts(float dpi_scale);

// Applies the color palette and widget styling.
void apply();

} // namespace empower::theme
