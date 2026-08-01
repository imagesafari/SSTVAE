#include "banner.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>

namespace sstvae::gui {

ErrorBanner::ErrorBanner(QWidget* parent) : QFrame(parent) {
    setFrameStyle(QFrame::StyledPanel | QFrame::Plain);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);

    icon_ = new QLabel(this);
    const int size = style()->pixelMetric(QStyle::PM_SmallIconSize);
    icon_->setPixmap(style()
                         ->standardIcon(QStyle::SP_MessageBoxCritical)
                         .pixmap(size, size));
    layout->addWidget(icon_);

    text_ = new QLabel(this);
    text_->setWordWrap(true);
    // Bold rather than coloured: readable in every theme, and the icon
    // already says "error".
    QFont font = text_->font();
    font.setBold(true);
    text_->setFont(font);
    layout->addWidget(text_, 1);

    auto* dismiss = new QPushButton(tr("Dismiss"), this);
    connect(dismiss, &QPushButton::clicked, this, &ErrorBanner::clear);
    layout->addWidget(dismiss);

    hide();
}

void ErrorBanner::show_error(const QString& message) {
    text_->setText(message);
    show();
}

void ErrorBanner::clear() {
    text_->clear();
    hide();
}

QString ErrorBanner::message() const { return text_->text(); }

}  // namespace sstvae::gui
