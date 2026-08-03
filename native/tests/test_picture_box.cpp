// The received-picture box.
//
// The bug this exists for has now been shipped twice in different
// shapes, and both times it looked like a working preview:
//
//   1. **The ratchet.** `setFixedHeight(width * 3/4)` makes the box's
//      *minimum* height follow its width, so widening the pane raises a
//      floor under the whole window that narrowing it never lowers.
//      Invisible at split-pane widths; at a full-window tab it demanded
//      a 1405 px window. So the first and most important assertion here
//      is that the minimum does not move at any width.
//   2. **The aspect.** Two earlier attempts (`heightForWidth`, and
//      pinning from the panel's stale `resizeEvent`) produced a 2.5:1
//      box and a 460x90 strip. Both still contained a correctly
//      letterboxed picture, so only the *box* geometry says which one
//      you have.
//
// Neither has an oracle in a screenshot -- a wrong box is still a box
// -- which is why the geometry is checked arithmetically here.

#include <QApplication>
#include <QPixmap>
#include <QWidget>

#include <cmath>

#include "check.hpp"
#include "images/images.hpp"
#include "picture_box.hpp"

using namespace sstvae;

namespace {

constexpr double ASPECT =
    static_cast<double>(images::IMG_W) / static_cast<double>(images::IMG_H);

// A box inside a shown parent, so a resize is delivered and is not
// clipped by the screen.
//
// Both halves earned themselves. `resize()` on a widget that has never
// been shown *posts* the QResizeEvent rather than sending it, so the
// first draft of this file measured a box still at its default 100x30 --
// the ratchet test in particular went green having computed nothing,
// which is the one thing a regression test cannot afford. And a
// *top-level* box is clamped to the platform screen, so an 800x600
// request arrived as 800x399 and read as a geometry bug in the widget.
struct Host {
    QWidget parent;
    gui::PictureBox box{QStringLiteral("nothing yet"), &parent};

    Host() {
        parent.resize(3000, 2000);
        parent.show();
    }

    gui::PictureBox& sized(int w, int h) {
        // **Twice, deliberately.** The box's height cap is derived from
        // its width, so Qt clamps an incoming geometry against the
        // *previous* cap and the new one only applies on the next pass.
        // In the application a layout supplies that pass (the widget
        // calls `updateGeometry`); here nothing else would, so the
        // second call stands in for it. Asserting after one pass would
        // be asserting that a two-pass settle is a one-pass settle.
        for (int pass = 0; pass < 2; ++pass) {
            box.setGeometry(0, 0, w, h);
            QCoreApplication::processEvents();
        }
        check::equal(box.width(), w, "the resize was delivered");
        return box;
    }
};

// Within a pixel of 4:3, since the rectangle is integral.
bool is_four_by_three(const QRect& r) {
    if (r.height() <= 0) return false;
    const double got = static_cast<double>(r.width()) / r.height();
    return std::abs(got - ASPECT) < 0.02;
}

void test_the_minimum_never_follows_the_width() {
    Host host;
    gui::PictureBox& box = host.box;
    const int floor_h = box.minimumSizeHint().height();
    const int floor_w = box.minimumSizeHint().width();

    // Widths spanning a narrow split pane through a full-window tab on
    // a large monitor. The 1400 case is the one that demanded a 1405 px
    // window before this was geometry rather than a fixed height.
    for (const int w : {200, 500, 1000, 1400, 2400}) {
        host.sized(w, 400);
        check::equal(box.minimumSizeHint().height(), floor_h,
                     "the minimum height does not follow the width");
        check::equal(box.minimumSizeHint().width(), floor_w,
                     "the minimum width does not follow the width");
        check::is_true(box.minimumHeight() <= floor_h,
                       "no hard minimum height was installed either");
    }
}

void test_it_stays_four_by_three_in_both_directions() {
    Host host;
    gui::PictureBox& box = host.sized(800, 600);

    check::is_true(is_four_by_three(box.picture_rect()),
                   "width-limited: the picture box is 4:3");
    check::equal(box.picture_rect().width(), 800,
                 "width-limited: it uses the full width");

    // Height-limited -- the case a tab creates, and the one the old
    // fixed-height code could not produce at all because it simply
    // forced the window taller instead.
    host.sized(1400, 400);
    const QRect tall = box.picture_rect();
    check::is_true(is_four_by_three(tall), "height-limited: the picture box is 4:3");
    check::equal(tall.height(), 400, "height-limited: it uses the full height");
    check::is_true(tall.width() < 1400, "height-limited: and it narrows to suit");

    // Centred, not pinned to a corner: a dropped offset leaves a
    // picture that is the right shape in the wrong place.
    check::equal(tall.x(), (1400 - tall.width()) / 2, "the picture is centred");
}

void test_it_asks_for_four_by_three_but_caps_there() {
    Host host;
    gui::PictureBox& box = host.sized(800, 600);
    // The hint is what a layout gives it when there is room -- this is
    // what keeps the split-pane appearance unchanged.
    check::equal(box.sizeHint().height(), 800 * images::IMG_H / images::IMG_W,
                 "it asks for 4:3");
    // And it refuses to be taller, so surplus height goes to the
    // controls below rather than becoming grey margin.
    check::equal(box.maximumHeight(), 800 * images::IMG_H / images::IMG_W,
                 "and caps itself there");
}

void test_a_picture_is_scaled_into_the_box() {
    Host host;
    gui::PictureBox& box = host.sized(800, 600);
    QPixmap picture(images::IMG_W, images::IMG_H);
    picture.fill(Qt::red);
    box.set_picture(picture);
    // Setting a picture must not change the geometry -- a preview that
    // resized itself around its content would undo everything above.
    check::is_true(is_four_by_three(box.picture_rect()),
                   "the box keeps its shape once a picture is in it");
    check::equal(box.minimumSizeHint().height(), gui::PictureBox::MIN_H,
                 "and still imposes no floor");
}

}  // namespace

int main(int argc, char** argv) {
    check::report_crashes_instead_of_prompting();
    qputenv("QT_QPA_PLATFORM", "offscreen");
    const QApplication app(argc, argv);

    test_the_minimum_never_follows_the_width();
    test_it_stays_four_by_three_in_both_directions();
    test_it_asks_for_four_by_three_but_caps_there();
    test_a_picture_is_scaled_into_the_box();

    return check::report("picture box");
}
