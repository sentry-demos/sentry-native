#pragma once

#include "imgui.h"

// Fleet Control Center palette — Sentry brand identity colors.
// https://brand.getsentry.com/d/4A7NQz1aXA1i/brand-identity#/brand-guide/colors
namespace empower::theme {

namespace color {
// Backgrounds
constexpr ImVec4 bg         = ImVec4(0.122f, 0.086f, 0.200f, 1.00f); // #1F1633 Deep Purple
constexpr ImVec4 bg_deep    = ImVec4(0.082f, 0.059f, 0.137f, 1.00f); // #150F23 Midnight Violet
constexpr ImVec4 surface    = ImVec4(0.212f, 0.176f, 0.349f, 1.00f); // #362D59 Border / surface
constexpr ImVec4 surface_hi = ImVec4(0.259f, 0.125f, 0.510f, 1.00f); // #422082 Deep Violet
constexpr ImVec4 border     = ImVec4(0.212f, 0.176f, 0.349f, 1.00f); // #362D59

// Interactive
constexpr ImVec4 accent     = ImVec4(0.416f, 0.373f, 0.757f, 1.00f); // #6A5FC1 Sentry Purple
constexpr ImVec4 accent_hi  = ImVec4(0.475f, 0.384f, 0.549f, 1.00f); // #79628C Muted Purple
constexpr ImVec4 sentry     = ImVec4(0.980f, 0.498f, 0.667f, 1.00f); // #FA7FAA Hot Pink (links)
constexpr ImVec4 blue       = ImVec4(0.243f, 0.863f, 1.000f, 1.00f); // #3EDCFf Lt Blue
constexpr ImVec4 pink       = ImVec4(1.000f, 0.439f, 0.737f, 1.00f); // #FF70BC Lt Pink
constexpr ImVec4 purple     = ImVec4(0.655f, 0.216f, 0.706f, 1.00f); // #A737B4 Lt Purple

// Text
constexpr ImVec4 text       = ImVec4(1.000f, 1.000f, 1.000f, 1.00f); // #FFFFFF
constexpr ImVec4 text_dim   = ImVec4(0.898f, 0.906f, 0.922f, 1.00f); // #E5E7EB
constexpr ImVec4 text_faint = ImVec4(0.475f, 0.384f, 0.549f, 1.00f); // #79628C

// Semantic (bright brand accents)
constexpr ImVec4 ok         = ImVec4(0.573f, 0.867f, 0.000f, 1.00f); // #92DD00 Dk Green
constexpr ImVec4 warn       = ImVec4(0.933f, 0.502f, 0.098f, 1.00f); // #EE8019 Dk Orange
constexpr ImVec4 danger     = ImVec4(0.784f, 0.220f, 0.322f, 1.00f); // #C83852 Brand red
constexpr ImVec4 info       = ImVec4(0.416f, 0.373f, 0.757f, 1.00f); // #6A5FC1
} // namespace color

struct Fonts {
    ImFont* body = nullptr;   // default UI text
    ImFont* bold = nullptr;   // medium-weight body (nav, emphasis)
    ImFont* small = nullptr;  // captions / labels
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
