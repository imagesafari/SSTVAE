// The received picture, in a 4:3 box that fits *inside* the space it is
// given rather than demanding space of its own.
//
// **The label is positioned by hand and is deliberately not in a
// layout.** That is the whole point: a child in a layout pushes its
// minimum up into the window, and three earlier attempts at a 4:3
// preview each did that in a different way.
//
//  * `heightForWidth` alone is only a *request*. A section shorter than
//    the sum of what its children want shrinks the preview toward its
//    minimum, and a 4:3 box came out 2.5:1.
//  * Pinning from the *panel's* resizeEvent reads `preview_->width()`
//    before the layout has re-run, so it pins to the previous width --
//    a 460x90 strip.
//  * `setFixedHeight(width * 3/4)` from the label's own resizeEvent
//    looked right and is a **ratchet**: a fixed height is a hard
//    minimum, so a wide pane makes a tall window that can never be
//    shortened again. That was harmless while the splitter kept each
//    pane narrow, and not harmless at all once a tab gives one pane the
//    whole width -- measured at a **1405 px minimum window height on a
//    1400 px window**, taller than the laptop panels the tabbed layout
//    exists to fit.
//
// So the aspect is enforced by *geometry*, never by a minimum: 4:3 is
// what the box asks for (`sizeHint`) and caps itself at
// (`setMaximumHeight`), while `minimumSizeHint` stays small. Given the
// height, the picture is exactly 4:3 and full width, which is what it
// has always looked like; denied it, the picture stays 4:3 and narrows,
// which is the part that used to be impossible.
//
// No `Q_OBJECT`: these are plain virtual overrides with nothing for moc
// to do.

#ifndef SSTVAE_GUI_PICTURE_BOX_HPP
#define SSTVAE_GUI_PICTURE_BOX_HPP

#include <QPixmap>
#include <QString>
#include <QWidget>

class QLabel;

namespace sstvae::gui {

class PictureBox : public QWidget {
public:
    // Never smaller than this, whatever the picture's shape -- and
    // crucially, never *larger* a minimum than this either, which is
    // the property the ratchet broke.
    static constexpr int MIN_W = 160;
    static constexpr int MIN_H = 120;

    explicit PictureBox(const QString& text, QWidget* parent = nullptr);

    // What a 4:3 picture would like, given the width we have.
    // Preferred, not required.
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(MIN_W, MIN_H); }

    // The picture at its own size. Kept unscaled, so a resize rescales
    // from the original rather than compounding losses.
    void set_picture(const QPixmap& picture);

    // The label's rectangle within this widget. For tests: an inverted
    // axis or a dropped centring offset still looks like a working
    // preview until a picture is in it.
    QRect picture_rect() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void rescale();

    QLabel* label_ = nullptr;
    QPixmap source_;
};

}  // namespace sstvae::gui

#endif
