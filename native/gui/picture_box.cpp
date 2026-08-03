#include "picture_box.hpp"

#include <QLabel>
#include <QPalette>
#include <QResizeEvent>
#include <QSizePolicy>

#include <algorithm>

#include "images/images.hpp"

namespace sstvae::gui {

PictureBox::PictureBox(const QString& text, QWidget* parent) : QWidget(parent) {
    label_ = new QLabel(text, this);
    label_->setAlignment(Qt::AlignCenter);
    // Palette, not a stylesheet: a stylesheet anywhere makes Qt wrap the
    // application style in QStyleSheetStyle, which resets padding to
    // zero across every combo and spin box in the app -- including the
    // settings dialog, which has nothing to do with this widget.
    label_->setAutoFillBackground(true);
    QPalette dark = label_->palette();
    dark.setColor(QPalette::Window, QColor(0x20, 0x20, 0x24));
    dark.setColor(QPalette::WindowText, QColor(0x88, 0x88, 0x88));
    label_->setPalette(dark);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

QSize PictureBox::sizeHint() const {
    return QSize(width(), std::max(MIN_H, width() * images::IMG_H / images::IMG_W));
}

void PictureBox::set_height_limit(int limit) {
    if (height_limit_ == limit) return;
    height_limit_ = limit;
    // **Recompute directly.** The first version called `resize(size())`
    // to provoke a `resizeEvent`, which does nothing at all: Qt returns
    // early when the size is unchanged. So releasing the bound never
    // restored the natural cap, `equalise` measured the *previous* cap
    // as the natural height, and the pictures ratcheted down to
    // whatever the first measurement happened to be -- 298 px, at every
    // window size. A no-op that looks like a refresh is the worst kind.
    apply_height_cap();
    updateGeometry();
}

void PictureBox::set_picture(const QPixmap& picture) {
    source_ = picture;
    rescale();
}

QRect PictureBox::picture_rect() const { return label_->geometry(); }

void PictureBox::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    apply_height_cap();

    // The largest 4:3 rectangle that fits, centred.
    int w = width();
    int h = height();
    if (w * images::IMG_H > h * images::IMG_W) {
        w = h * images::IMG_W / images::IMG_H;
    } else {
        h = w * images::IMG_H / images::IMG_W;
    }
    label_->setGeometry((width() - w) / 2, (height() - h) / 2, w, h);

    // Rescaled here rather than from the panel's resizeEvent, which is
    // the staleness trap in the header's second bullet: by this point
    // the label's geometry is the one it will keep.
    rescale();
}

// A *maximum*, never a minimum. Past 4:3 the extra height is grey
// margin above and below the picture, which is the shape that was
// objected to in the first place -- but capping is safe where
// `setFixedHeight` was not, because a maximum imposes nothing on the
// window and the surplus simply flows to whatever is below.
//
// The cap is derived from the width, so it is necessarily one pass
// behind it: Qt clamps the incoming geometry against the *previous*
// maximum before this runs. `updateGeometry` is what closes that.
void PictureBox::apply_height_cap() {
    int cap = std::max(MIN_H, width() * images::IMG_H / images::IMG_W);
    if (height_limit_ > 0) cap = std::min(cap, std::max(MIN_H, height_limit_));
    if (maximumHeight() != cap) {
        setMaximumHeight(cap);
        updateGeometry();
    }
}

void PictureBox::rescale() {
    if (source_.isNull()) return;
    label_->setPixmap(
        source_.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

}  // namespace sstvae::gui
