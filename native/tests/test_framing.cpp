// `images::fit` with a Framing, and the contract that matters most:
// adding framing must not change what the default call produces.
//
// The trap is one pixel wide. The old code cropped at
// `(scaled - target) / 2` -- integer division, i.e. truncation -- and
// the natural rewrite `lround(center * scaled - target / 2.0)` agrees
// for an even `scaled` and disagrees by one for an odd one. Every
// odd-intermediate picture would then shift a pixel, silently, for no
// reason the operator asked for.
//
// **The oracle has to be the old formula, reimplemented here.**
// Comparing `fit(img)` against `fit(img, Framing{})` proves nothing:
// the first delegates to the second, so it compares the function to
// itself and passes however wrong both are. (Verified -- that first
// draft passed with the `lround` mutation in place.) Nor can Python be
// the oracle: `tests/test_native_parity.py` deliberately does not
// compare `fit_image` on anything that needs resampling, because PIL's
// LANCZOS and stb's filter differ by design. So the reference below
// resamples through the *same* `images::resize`, which isolates the
// one thing under test -- the crop offset.

#include "images/images.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "check.hpp"
#include "images/types.hpp"

namespace check = sstvae::check;
using sstvae::images::Framing;
using sstvae::images::Picture;

namespace {

// A deterministic, non-uniform picture: every pixel encodes its own
// position, so a crop that lands one pixel out is visible as a
// mismatch rather than hidden by flat colour.
Picture ramp(int width, int height) {
    Picture p(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 3;
            p.rgb[i] = static_cast<std::uint8_t>(x % 256);
            p.rgb[i + 1] = static_cast<std::uint8_t>(y % 256);
            p.rgb[i + 2] = static_cast<std::uint8_t>((x + y) % 256);
        }
    }
    return p;
}

// The algorithm as it stood before framing existed, written out.
// Independent of the code under test except for `resize`, which is
// shared on purpose so the resampler cancels and only the crop offset
// is being compared.
Picture reference_fit(const Picture& img) {
    const int W = sstvae::images::IMG_W;
    const int H = sstvae::images::IMG_H;
    if (img.width == W && img.height == H) return img;
    const double scale = std::max(static_cast<double>(W) / img.width,
                                  static_cast<double>(H) / img.height);
    const int sw = std::max(W, static_cast<int>(std::lround(img.width * scale)));
    const int sh = std::max(H, static_cast<int>(std::lround(img.height * scale)));
    const Picture scaled = sstvae::images::resize(img, sw, sh);
    Picture out(W, H);
    const int left = (sw - W) / 2;  // the original integer division
    const int top = (sh - H) / 2;
    for (int y = 0; y < H; ++y) {
        const std::uint8_t* src =
            scaled.rgb.data() + (static_cast<std::size_t>(y + top) * sw + left) * 3;
        std::copy(src, src + static_cast<std::size_t>(W) * 3,
                  out.rgb.begin() + static_cast<std::size_t>(y) * W * 3);
    }
    return out;
}

void test_default_framing_matches_the_old_algorithm() {
    // Sizes chosen so both parities of the scaled intermediate appear --
    // that is where truncation and rounding disagree. 1920x1080 scales
    // to 853 wide (odd); 800x600 to 640 (even).
    const std::vector<std::pair<int, int>> sizes = {
        {640, 480},  {1920, 1080}, {800, 600},  {481, 640},
        {1001, 700}, {333, 250},   {1279, 721}, {320, 240},
    };
    int odd_seen = 0;
    for (const auto& [w, h] : sizes) {
        const Picture src = ramp(w, h);
        const Picture want = reference_fit(src);
        const std::string label = std::to_string(w) + "x" + std::to_string(h);

        // Record that the sample actually exercises the odd case; a
        // suite that only ever saw even intermediates would pass with
        // the bug present.
        const double scale =
            std::max(static_cast<double>(sstvae::images::IMG_W) / w,
                     static_cast<double>(sstvae::images::IMG_H) / h);
        const int sw = std::max(sstvae::images::IMG_W,
                                static_cast<int>(std::lround(w * scale)));
        if (sw % 2 == 1) ++odd_seen;

        for (const Picture& got :
             {sstvae::images::fit(src), sstvae::images::fit(src, Framing{})}) {
            check::equal(got.width, want.width, "framing/default: width " + label);
            check::equal(got.height, want.height, "framing/default: height " + label);
            check::is_true(got.rgb == want.rgb,
                           "framing/default: byte-identical to the old "
                           "algorithm for " +
                               label);
        }
    }
    check::is_true(odd_seen > 0,
                   "framing/default: the sample includes an odd scaled width, "
                   "which is the case that can regress");
}

void test_panning_moves_the_window() {
    // 16:9: there is width to give up, so panning must change what
    // comes out -- and left must differ from right.
    const Picture src = ramp(1600, 900);
    Framing left;
    left.center_x = 0.0;  // clamped to the leftmost legal window
    Framing right;
    right.center_x = 1.0;
    const Picture a = sstvae::images::fit(src, left);
    const Picture b = sstvae::images::fit(src, right);
    check::is_true(a.rgb != b.rgb,
                   "framing/pan: opposite edges give different pictures");

    // And the centre is between them, not equal to either.
    const Picture mid = sstvae::images::fit(src, Framing{});
    check::is_true(mid.rgb != a.rgb, "framing/pan: centre differs from left");
    check::is_true(mid.rgb != b.rgb, "framing/pan: centre differs from right");
}

void test_zoom_crops_tighter() {
    const Picture src = ramp(1200, 900);  // already 4:3
    const Picture plain = sstvae::images::fit(src, Framing{});
    Framing zoomed;
    zoomed.zoom = 2.0;
    const Picture tight = sstvae::images::fit(src, zoomed);
    check::equal(tight.width, sstvae::images::IMG_W, "framing/zoom: still 640");
    check::equal(tight.height, sstvae::images::IMG_H, "framing/zoom: still 480");
    check::is_true(tight.rgb != plain.rgb,
                   "framing/zoom: a zoomed 4:3 picture is not the plain fit");

    // Zoom below 1 would expose edges with nothing behind them, so it
    // is clamped rather than honoured.
    Framing under;
    under.zoom = 0.25;
    check::is_true(sstvae::images::fit(src, under).rgb == plain.rgb,
                   "framing/zoom: below 1 is clamped to cover");
}

void test_out_of_range_centres_are_clamped() {
    const Picture src = ramp(1600, 900);
    Framing far;
    far.center_x = 5.0;
    far.center_y = -3.0;
    const Picture out = sstvae::images::fit(src, far);
    check::equal(out.width, sstvae::images::IMG_W, "framing/clamp: right size");
    // Equal to the rightmost legal window rather than reading out of
    // bounds -- which would be a crash or garbage, not a wrong picture.
    Framing edge;
    edge.center_x = 1.0;
    edge.center_y = 0.0;
    check::is_true(out.rgb == sstvae::images::fit(src, edge).rgb,
                   "framing/clamp: a far centre lands on the edge window");
}

void test_identity_shortcut_respects_framing() {
    // A 640x480 source with the default framing is returned untouched
    // (the parity path). With a zoom it must NOT be -- an operator who
    // zoomed an already-4:3 picture meant it.
    const Picture src = ramp(sstvae::images::IMG_W, sstvae::images::IMG_H);
    check::is_true(sstvae::images::fit(src, Framing{}).rgb == src.rgb,
                   "framing/identity: default framing returns the original");
    Framing zoomed;
    zoomed.zoom = 1.5;
    check::is_true(sstvae::images::fit(src, zoomed).rgb != src.rgb,
                   "framing/identity: a zoom is honoured on a 4:3 source");
}

}  // namespace

int main() {
    check::report_crashes_instead_of_prompting();
    check::Watchdog watchdog(120.0, "framing");

    check::current_step.store("default_matches_bare");
    test_default_framing_matches_the_old_algorithm();
    check::current_step.store("pan");
    test_panning_moves_the_window();
    check::current_step.store("zoom");
    test_zoom_crops_tighter();
    check::current_step.store("clamp");
    test_out_of_range_centres_are_clamped();
    check::current_step.store("identity");
    test_identity_shortcut_respects_framing();

    return check::report("framing");
}
