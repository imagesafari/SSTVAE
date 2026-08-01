#include "log_pane.hpp"

#include <QClipboard>
#include <QComboBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

#include <string>

namespace sstvae::gui {

namespace {

// "All" plus the sources AppState hands out. A source not in this list
// still logs and still shows under All; it just has no dedicated
// filter entry, which is the right default for a source added later.
const char* const FILTERS[] = {"All", "rig", "rx", "tx", "opt", "app"};

}  // namespace

LogPane::LogPane(const log::StatusLog* log, QWidget* parent)
    : QWidget(parent), log_(log) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto* header = new QHBoxLayout();
    header->setContentsMargins(4, 2, 4, 0);
    header->addWidget(new QLabel(tr("Filter:"), this));
    filter_ = new QComboBox(this);
    for (const char* name : FILTERS) {
        filter_->addItem(QString::fromLatin1(name));
    }
    connect(filter_, &QComboBox::currentIndexChanged, this, &LogPane::refill);
    header->addWidget(filter_);

    file_note_ = new QLabel(this);
    file_note_->setWordWrap(false);
    header->addWidget(file_note_, 1);

    auto* copy = new QPushButton(tr("Copy"), this);
    copy->setToolTip(tr("Copy the whole log (unfiltered) to the clipboard"));
    connect(copy, &QPushButton::clicked, this, &LogPane::copy_all);
    header->addWidget(copy);
    layout->addLayout(header);

    text_ = new QPlainTextEdit(this);
    text_->setReadOnly(true);
    text_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    // Matches StatusLog's own retention, so the view cannot outgrow
    // the model it claims to show.
    text_->setMaximumBlockCount(2000);
    text_->setMinimumHeight(text_->fontMetrics().lineSpacing() * 3);
    layout->addWidget(text_, 1);

    refill();
}

QSize LogPane::sizeHint() const {
    const int line = text_->fontMetrics().lineSpacing();
    return {QWidget::sizeHint().width(), line * 5 + filter_->sizeHint().height() + 10};
}

void LogPane::set_file_note(const QString& note) {
    file_note_->setText(note);
}

bool LogPane::passes_filter(const std::string& source) const {
    const QString wanted = filter_->currentText();
    return wanted == QLatin1String("All") ||
           wanted == QString::fromStdString(source);
}

void LogPane::append_line(const log::Entry& entry) {
    QTextCursor cursor(text_->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    // Bold rather than coloured (the traditional-look decision): an
    // error line stands out in every theme without inventing chrome.
    format.setFontWeight(entry.severity == log::Severity::Error ? QFont::Bold
                                                                : QFont::Normal);
    if (cursor.position() != 0) cursor.insertBlock();
    cursor.setCharFormat(format);
    cursor.insertText(QString::fromStdString(log::format_entry(entry)));
    text_->setTextCursor(cursor);
    text_->ensureCursorVisible();
    // ensureCursorVisible on a long line scrolls the view *right*,
    // taking the timestamp column off the left edge for every line --
    // seen in the first gui-shot render. Follow vertically, never
    // horizontally.
    text_->horizontalScrollBar()->setValue(0);
}

void LogPane::append(qlonglong ms, const QString& source, int severity,
                     const QString& text) {
    if (!passes_filter(source.toStdString())) return;
    log::Entry entry;
    entry.ms = ms;
    entry.source = source.toStdString();
    entry.severity = static_cast<log::Severity>(severity);
    entry.text = text.toStdString();
    append_line(entry);
}

void LogPane::refill() {
    text_->clear();
    if (log_ == nullptr) return;
    for (const log::Entry& entry : log_->snapshot()) {
        if (passes_filter(entry.source)) append_line(entry);
    }
}

void LogPane::copy_all() {
    if (log_ == nullptr) return;
    QString all;
    for (const log::Entry& entry : log_->snapshot()) {
        all += QString::fromStdString(log::format_entry(entry));
        all += QLatin1Char('\n');
    }
    QGuiApplication::clipboard()->setText(all);
}

}  // namespace sstvae::gui
