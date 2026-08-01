#include "pane_container.hpp"

#include "height_limited.hpp"

#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLayout>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace sstvae::gui {

PaneLayout resolve_layout(const std::string& setting, int available_width,
                          int split_minimum_width) {
    if (setting == "split") return PaneLayout::Split;
    if (setting == "tabs") return PaneLayout::Tabs;
    // "auto", and anything else -- settings.cpp has already normalised
    // an unrecognised value to "auto" and said so, so reaching here with
    // one means the caller bypassed it, and auto is the safe answer.
    //
    // A screen whose width we could not learn (0 or negative) is not a
    // small screen, it is an unanswered question; side by side is the
    // better layout, so an unknown falls to it rather than to the
    // fallback for a constraint nobody measured.
    if (available_width <= 0) return PaneLayout::Split;
    return split_minimum_width <= available_width ? PaneLayout::Split : PaneLayout::Tabs;
}

PaneContainer::PaneContainer(QWidget* first, QString first_title, QWidget* second,
                             QString second_title, QWidget* parent)
    : QWidget(parent),
      first_(first),
      second_(second),
      first_title_(std::move(first_title)),
      second_title_(std::move(second_title)) {
    layout_ = new QVBoxLayout(this);
    // No margins: this widget is pure structure, and the panes inside
    // carry their own spacing.
    layout_->setContentsMargins(0, 0, 0, 0);
    install(build_split());
    mode_ = PaneLayout::Split;
}

void PaneContainer::install(QWidget* container) {
    container_ = container;
    layout_->addWidget(container_);
    // **Not redundant, and measured.** Every `addWidget`/`addTab` above
    // performs a `QWidget::setParent`, which *hides* the widget; the
    // thing that normally un-hides it is the parent transitioning from
    // hidden to visible. On a mode switch the parent is already
    // visible, so that transition never happens and nothing shows the
    // new container -- the window renders empty and no call has failed.
    // `test_pane_container.cpp` caught exactly this on its first run.
    //
    // One `show()` on the container is enough: `setParent` marks a
    // widget hidden but not *explicitly* hidden, so showing an ancestor
    // recursively shows it -- and a tab widget's background page, which
    // its stacked layout hides deliberately, stays hidden.
    container_->show();
}

QWidget* PaneContainer::titled(QWidget* content, const QString& title) {
    auto* box = new QGroupBox(title);
    // Bold, because this is the label that answers "which half am I
    // looking at" and the boxes nested inside it are titled too. The
    // font is the only change -- no stylesheet, no colour.
    QFont heading = box->font();
    heading.setBold(true);
    box->setFont(heading);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(6, 4, 6, 6);
    // The content keeps the default weight; only the title is bold.
    QFont body = content->font();
    body.setBold(false);
    content->setFont(body);
    layout->addWidget(content);
    return box;
}

QWidget* PaneContainer::build_split() {
    tabs_ = nullptr;
    // **Locked equal, and a plain layout rather than a splitter**
    // (decided 2026-08-03). The complaint this answers is that the
    // composer had roughly 2.5x the received picture's area, and the
    // received picture is the one you cannot ask for again.
    //
    // A `QSplitter` cannot deliver "equal" on its own, and the reason is
    // worth keeping: it satisfies both children's *minimum* widths
    // before it applies any stretch factor. The transmit pane's minimum
    // was far larger than the receive pane's, so a 1:1 weighting still
    // produced 440 px against 1090 px -- which is what stalled this
    // layout the first time it was attempted.
    //
    // The fix is to **equalise the minimums** rather than to ignore
    // them. Give both panes the larger of the two floors and a 1:1
    // stretch, and equal width follows arithmetically at every size:
    // the layout satisfies two identical minimums and then splits what
    // is left in half. Ignoring the demands instead would have "worked"
    // by letting the wider pane's controls clip, which is trading a
    // visible problem for a hidden one.
    //
    // It only became affordable once the transmit pane stopped asking
    // for 1088 px (a duplicate properties box was sitting in its tool
    // row); it now asks 547 against the receive pane's 464, so the
    // shared floor costs the receive side 83 px rather than 624.
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    QWidget* boxes[2] = {titled(first_, first_title_), titled(second_, second_title_)};
    const int shared_floor =
        std::max({PANE_MIN_W, boxes[0]->minimumSizeHint().width(),
                  boxes[1]->minimumSizeHint().width()});
    for (QWidget* pane : boxes) {
        pane->setMinimumWidth(shared_floor);
        layout->addWidget(pane, 1);
    }
    // Undo the tab widget's work, if we are coming back from it. A
    // `QTabWidget` hides its background page with `hide()`, which marks
    // it *explicitly* hidden -- and unlike the implicit hide a reparent
    // leaves, showing an ancestor does not clear that. Without these
    // two lines the pane that was in the background tab never comes
    // back, and half the window is blank with nothing having failed.
    // (Harmless on the first build, where nothing is hidden yet.)
    first_->show();
    second_->show();
    return row;
}

QWidget* PaneContainer::build_tabs() {
    auto* tabs = new QTabWidget;
    tabs->addTab(first_, first_title_);
    tabs->addTab(second_, second_title_);
    tabs_ = tabs;
    // Re-apply whatever note was current, so switching to tabs mid
    // reception does not lose the one cue this mode has.
    set_first_note(first_note_);
    return tabs;
}

void PaneContainer::set_mode(PaneLayout mode) {
    if (mode == mode_) return;

    // Build first, delete second. The new container's addWidget/addTab
    // reparents `first_` and `second_` out of the old one, so by the
    // time it is deleted it owns neither -- and neither is ever left
    // parentless, which is what would mark them explicitly hidden. See
    // the header.
    QWidget* previous = container_;
    install(mode == PaneLayout::Tabs ? build_tabs() : build_split());
    delete previous;

    mode_ = mode;
    emit modeChanged(mode_);
}

void PaneContainer::set_picture_areas(QWidget* first_picture, QWidget* second_picture) {
    first_picture_ = first_picture;
    second_picture_ = second_picture;
    equalise();
}

void PaneContainer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    equalise();
}

void PaneContainer::equalise() {
    QWidget* a = first_picture_.data();
    QWidget* b = second_picture_.data();
    if (a == nullptr || b == nullptr) return;
    // Only meaningful when both are on screen; in tabs one page is
    // hidden and has no useful geometry, and there is nothing to match
    // it against anyway -- a tab gets the whole window either way.
    if (mode_ != PaneLayout::Split) {
        a->setMaximumHeight(QWIDGETSIZE_MAX);
        b->setMaximumHeight(QWIDGETSIZE_MAX);
        return;
    }
    // Recomputed from scratch every time, in three steps: release both
    // caps, let each pane lay out to the height it would naturally
    // take, then cap both to the smaller. Releasing first is what stops
    // this drifting -- carrying last pass's cap into this one would
    // ratchet the pictures smaller on every resize and never let them
    // grow back, which is the same failure as a fixed height wearing a
    // different hat.
    auto* la = dynamic_cast<HeightLimited*>(a);
    auto* lb = dynamic_cast<HeightLimited*>(b);
    if (la == nullptr || lb == nullptr) return;
    la->set_height_limit(0);
    lb->set_height_limit(0);
    for (QWidget* pane : {first_, second_}) {
        if (pane->layout() != nullptr) pane->layout()->activate();
    }
    const int room = std::min(a->height(), b->height());
    if (room <= 0) return;
    la->set_height_limit(room);
    lb->set_height_limit(room);
}

void PaneContainer::set_first_note(const QString& note) {
    first_note_ = note;
    if (tabs_ == nullptr) return;
    tabs_->setTabText(0, note.isEmpty() ? first_title_
                                        : QStringLiteral("%1 (%2)")
                                              .arg(first_title_, note));
}

}  // namespace sstvae::gui
