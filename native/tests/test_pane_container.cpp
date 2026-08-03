// The receive/transmit layout switch.
//
// Four claims, each of which fails invisibly if it is wrong:
//
//   1. The panels survive a mode switch. They are not toys -- the real
//      ones own a decode thread and a transmit thread -- so a container
//      that deletes its children when it is replaced would crash a long
//      way from here, at exit or at the next callback.
//   2. They come back *visible*. Reparenting through a null parent sets
//      Qt's explicit-hidden flag, and a widget hidden that way stays
//      hidden when it is added to a visible layout. The window would
//      look empty and nothing would have failed.
//   3. Tabs really are narrower. This is the entire justification for
//      the feature: a QSplitter's minimum is the sum of its children's,
//      a QTabWidget's is the max. If it did not hold, "small screen
//      mode" would be a menu entry that changed nothing.
//   4. "auto" resolves against the screen and honours an explicit
//      setting. The dangerous case is a screen we could not measure --
//      it must not read as "tiny".
//
// Deliberately built from two plain widgets rather than the real
// panels: the switch is container logic, and pulling in AppState would
// start a model load and open the rig.

#include <QApplication>
#include <QLabel>
#include <QPointer>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

#include "check.hpp"
#include "pane_container.hpp"

using namespace sstvae;
using gui::PaneLayout;

namespace {

// A pane with a real minimum width, since that is what the third claim
// above is about. A bare QWidget has a minimum of zero and both layouts
// would agree on nothing.
QWidget* pane(int minimum_width) {
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    auto* label = new QLabel(QStringLiteral("x"));
    label->setMinimumWidth(minimum_width);
    layout->addWidget(label);
    return w;
}

void test_panels_survive_and_stay_visible() {
    auto* first = pane(200);
    auto* second = pane(200);
    QPointer<QWidget> first_alive(first);
    QPointer<QWidget> second_alive(second);

    gui::PaneContainer container(first, QStringLiteral("Receive"), second,
                                 QStringLiteral("Transmit"));
    container.resize(900, 500);
    container.show();

    check::equal(static_cast<int>(container.mode()),
                 static_cast<int>(PaneLayout::Split), "starts side by side");

    // Switch both ways twice: a container that leaks or deletes on one
    // direction only would pass a single switch.
    for (int round = 0; round < 2; ++round) {
        container.set_mode(PaneLayout::Tabs);
        check::is_true(!first_alive.isNull() && !second_alive.isNull(),
                       "panels survive the switch to tabs");
        check::is_true(first->isVisible(), "the current tab's panel is visible");
        // The second tab is *correctly* hidden -- that is what a tab
        // widget is -- so what is checked is that it is still owned and
        // becomes visible when selected, not that it is on screen.
        check::is_true(second->parentWidget() != nullptr,
                       "the background tab's panel is still parented");

        container.set_mode(PaneLayout::Split);
        check::is_true(!first_alive.isNull() && !second_alive.isNull(),
                       "panels survive the switch back to side by side");
        check::is_true(first->isVisible() && second->isVisible(),
                       "both panels are visible side by side");
    }
}

void test_tabs_are_narrower_than_side_by_side() {
    // Equal minimums, so the arithmetic is unambiguous: the sum is
    // twice the max, and any result between them would mean the layout
    // is not doing what the whole feature assumes.
    auto* first = pane(300);
    auto* second = pane(300);
    gui::PaneContainer container(first, QStringLiteral("Receive"), second,
                                 QStringLiteral("Transmit"));

    const int split_min = container.minimumSizeHint().width();
    container.set_mode(PaneLayout::Tabs);
    const int tabs_min = container.minimumSizeHint().width();

    // Not "smaller by some tolerance" -- the claim is structural. Tabs
    // hold one pane's minimum where the splitter holds two, so the gap
    // has to be about a whole pane. Checked loosely (chrome, handle and
    // group-box margins differ between the two) but not so loosely that
    // a few pixels would pass.
    check::is_true(tabs_min < split_min,
                   "tabs have a smaller minimum width than side by side");
    check::is_true(tabs_min < split_min * 3 / 4,
                   "the saving is about a pane, not a margin");
}

void test_resolve_layout() {
    // An explicit setting wins over the screen, in both directions:
    // wanting both halves on a small panel is allowed, and so is
    // wanting tabs on a large one.
    check::equal(static_cast<int>(gui::resolve_layout("split", 800, 1015)),
                 static_cast<int>(PaneLayout::Split),
                 "an explicit 'split' is honoured on a screen too small for it");
    check::equal(static_cast<int>(gui::resolve_layout("tabs", 3840, 1015)),
                 static_cast<int>(PaneLayout::Tabs),
                 "an explicit 'tabs' is honoured on a large screen");

    check::equal(static_cast<int>(gui::resolve_layout("auto", 1920, 1015)),
                 static_cast<int>(PaneLayout::Split), "auto: it fits");
    check::equal(static_cast<int>(gui::resolve_layout("auto", 800, 1015)),
                 static_cast<int>(PaneLayout::Tabs), "auto: it does not fit");
    // Exactly the available width is a fit: a maximised window is a
    // legitimate way to run this app, and availableGeometry has already
    // taken the panels and docks off.
    check::equal(static_cast<int>(gui::resolve_layout("auto", 1015, 1015)),
                 static_cast<int>(PaneLayout::Split), "auto: exactly fits");

    // The failure that must not read as "small screen". A screen we
    // could not measure is an unanswered question, and answering it
    // with the fallback layout would quietly demote every operator
    // whose platform did not report a geometry.
    check::equal(static_cast<int>(gui::resolve_layout("auto", 0, 1015)),
                 static_cast<int>(PaneLayout::Split),
                 "auto: an unmeasurable screen falls to side by side");
}

}  // namespace

int main(int argc, char** argv) {
    check::report_crashes_instead_of_prompting();
    qputenv("QT_QPA_PLATFORM", "offscreen");
    const QApplication app(argc, argv);

    test_panels_survive_and_stay_visible();
    test_tabs_are_narrower_than_side_by_side();
    test_resolve_layout();

    return check::report("pane container");
}
