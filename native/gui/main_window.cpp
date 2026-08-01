#include "main_window.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QDockWidget>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>

#include "app_state.hpp"
#include "log_pane.hpp"
#include "rx_panel.hpp"
#include "tx_panel.hpp"
#include "settings_dialog.hpp"
#include "tx/engine.hpp"

namespace sstvae::gui {

namespace {

constexpr auto APP_NAME = "SSTVAE";

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    state_ = new AppState(this);
    setWindowTitle(QString::fromLatin1(APP_NAME));
    resize(1100, 800);

    tabs_ = new QTabWidget(this);
    rx_panel_ = new ReceivePanel(state_, tabs_);
    tabs_->addTab(rx_panel_, tr("Receive"));
    tx_panel_ = new TransmitPanel(state_, tabs_);
    tabs_->addTab(tx_panel_, tr("Transmit"));

    // Half duplex: our own transmission must not be decoded back into a
    // received picture. Frequency polling pauses too -- the answer is
    // not interesting mid-over, and it keeps CAT chatter off the wire
    // while keyed.
    connect(tx_panel_, &TransmitPanel::transmitStarted, rx_panel_,
            &ReceivePanel::suspend_for_transmit);
    connect(tx_panel_, &TransmitPanel::transmitFinished, rx_panel_,
            &ReceivePanel::resume_after_transmit);
    connect(tx_panel_, &TransmitPanel::transmitStarted, state_,
            &AppState::pause_rig_polling);
    connect(tx_panel_, &TransmitPanel::transmitFinished, state_,
            &AppState::resume_rig_polling);
    // The most recent picture becomes available as a transmit inset.
    connect(rx_panel_, &ReceivePanel::imageReceived, tx_panel_,
            &TransmitPanel::set_last_rx_image);
    setCentralWidget(tabs_);

    build_menu();
    build_status_bar();
    build_log_dock();

    connect(state_, &AppState::modelLoaded, this, &MainWindow::on_model_loaded);
    connect(state_, &AppState::rigStatus, this, &MainWindow::on_rig_status);
    connect(state_, &AppState::modelProgress, this,
            &MainWindow::on_model_progress);
    // The PTT lamp follows the transmit phase; the rig chip greys while
    // polling is paused so a frozen frequency does not read as current.
    connect(tx_panel_, &TransmitPanel::stateChanged, this,
            [this](int phase, double, const QString&) { on_tx_state(phase); });
    connect(tx_panel_, &TransmitPanel::sendFinished, this,
            [this](bool) { on_tx_state(static_cast<int>(tx::TxPhase::Idle)); });
    connect(tx_panel_, &TransmitPanel::transmitStarted, this,
            [this] { rig_label_->setEnabled(false); });
    connect(tx_panel_, &TransmitPanel::transmitFinished, this,
            [this] { rig_label_->setEnabled(true); });
    connect(rx_panel_, &ReceivePanel::receptionSaved, this,
            [this](const QString& path) {
                statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
            });

    state_->load_model_async();
    state_->connect_rig();
    update_station_label();
}

MainWindow::~MainWindow() = default;

void MainWindow::build_menu() {
    // The explicit NoRole calls are load-bearing on macOS. Qt's Cocoa
    // plugin pattern-matches action text and moves anything looking
    // like Preferences or Quit into the application menu. Both of this
    // menu's actions match, so Qt emptied the File menu -- and macOS
    // hides an empty menu, leaving no way to reach Settings at all.
    //
    // The shortcuts are the belt to that braces: the platform-correct
    // sequences (Cmd+, and Cmd+Q on macOS, Ctrl+Q elsewhere), so
    // Settings stays reachable even if a platform menu bar misbehaves
    // again.
    QMenu* menu = menuBar()->addMenu(tr("&File"));

    QAction* settings_action = menu->addAction(tr("&Settings..."));
    settings_action->setMenuRole(QAction::NoRole);
    settings_action->setShortcut(QKeySequence::Preferences);
    connect(settings_action, &QAction::triggered, this, &MainWindow::open_settings);

    menu->addSeparator();

    QAction* quit_action = menu->addAction(tr("&Quit"));
    quit_action->setMenuRole(QAction::NoRole);
    quit_action->setShortcut(QKeySequence::Quit);
    connect(quit_action, &QAction::triggered, this, &MainWindow::close);

    // View > Status log: the dock's own toggle action, so the menu
    // entry and the dock's close button cannot disagree about state.
    // The action is added in build_log_dock(), which runs after this;
    // the menu pointer is kept on the window via findChild-free means.
    view_menu_ = menuBar()->addMenu(tr("&View"));
}

void MainWindow::build_status_bar() {
    auto* bar = new QStatusBar(this);
    setStatusBar(bar);
    // The PTT lamp: hidden except while the rig is keyed. Bold red
    // text on the standard background -- a state indicator, not chrome
    // -- because "the radio is transmitting" is the one state in the
    // app that must be visible at a glance (review F8).
    ptt_label_ = new QLabel(tr("TX"), this);
    QFont ptt_font = ptt_label_->font();
    ptt_font.setBold(true);
    ptt_label_->setFont(ptt_font);
    QPalette ptt_palette = ptt_label_->palette();
    ptt_palette.setColor(QPalette::WindowText, QColor(0xb3, 0x26, 0x1e));
    ptt_label_->setPalette(ptt_palette);
    ptt_label_->hide();

    station_label_ = new QLabel(QString(), this);
    rig_label_ = new QLabel(tr("Rig control off"), this);
    model_label_ = new QLabel(tr("Loading model..."), this);
    for (QLabel* label : {ptt_label_, station_label_, rig_label_, model_label_}) {
        bar->addPermanentWidget(label);
    }

    rig_error_timer_ = new QTimer(this);
    rig_error_timer_->setInterval(5000);
    connect(rig_error_timer_, &QTimer::timeout, this,
            &MainWindow::refresh_rig_error_age);
}

void MainWindow::build_log_dock() {
    log_pane_ = new LogPane(&state_->status_log(), this);
    log_pane_->set_file_note(state_->log_file_note());
    connect(state_, &AppState::logEntry, log_pane_, &LogPane::append);

    log_dock_ = new QDockWidget(tr("Status log"), this);
    // Closable so it can be put away; not floatable or movable -- it is
    // a log strip, not a tool window, and the bottom is its place.
    log_dock_->setFeatures(QDockWidget::DockWidgetClosable);
    log_dock_->setAllowedAreas(Qt::BottomDockWidgetArea);
    log_dock_->setWidget(log_pane_);
    addDockWidget(Qt::BottomDockWidgetArea, log_dock_);
    view_menu_->addAction(log_dock_->toggleViewAction());

    // An error re-opens a closed dock: the log is where the detail
    // lives, and an error with the log hidden would be exactly the
    // silent failure this pane exists to end.
    connect(state_, &AppState::logEntry, this,
            [this](qlonglong, const QString&, int severity, const QString&) {
                if (severity == static_cast<int>(log::Severity::Error)) {
                    log_dock_->show();
                }
            });
}

void MainWindow::update_station_label() {
    const std::string& callsign = state_->config().callsign;
    station_label_->setText(
        tr("Callsign: %1")
            .arg(callsign.empty() ? tr("(no callsign set)")
                                  : QString::fromStdString(callsign)));
}

void MainWindow::on_model_loaded() {
    if (state_->model() == nullptr) {
        model_label_->setText(tr("Model failed to load"));
        QMessageBox::critical(
            this, tr("Could not load the model"),
            tr("%1\n\nSet a checkpoint path in Settings, or check your network "
               "connection for the published checkpoint.")
                .arg(state_->model_error()));
        return;
    }
    model_label_->setText(tr("Model ready"));
    // Refinement needs a codec to start from, so a run that could not
    // be armed at startup (or after a checkpoint change) is armed here
    // rather than waiting for the operator to edit something.
    tx_panel_->sync_from_config();
}

void MainWindow::on_rig_status(const QString& text, bool error) {
    if (error) {
        rig_error_text_ = text;
        rig_error_since_ms_ = QDateTime::currentMSecsSinceEpoch();
        rig_label_->setText(text);
        rig_error_timer_->start();
    } else {
        rig_error_text_.clear();
        rig_error_timer_->stop();
        rig_label_->setText(text);
    }
}

void MainWindow::refresh_rig_error_age() {
    if (rig_error_text_.isEmpty()) return;
    const qint64 seconds =
        (QDateTime::currentMSecsSinceEpoch() - rig_error_since_ms_) / 1000;
    // The controller deduplicates identical failures and backs its poll
    // off to a minute, so without this a stale error is indistinguishable
    // from a fresh one.
    rig_label_->setText(tr("%1 (%2 s ago)").arg(rig_error_text_).arg(seconds));
}

void MainWindow::on_model_progress(qlonglong received, qlonglong total) {
    if (total > 0) {
        model_label_->setText(tr("Model: downloading %1 / %2 MB")
                                  .arg(received / 1e6, 0, 'f', 1)
                                  .arg(total / 1e6, 0, 'f', 1));
    } else {
        model_label_->setText(
            tr("Model: downloading %1 MB").arg(received / 1e6, 0, 'f', 1));
    }
}

void MainWindow::on_tx_state(int phase) {
    // Keyed is Keying through Unkeying inclusive: the lamp exists for
    // "the radio is (or should be) transmitting right now".
    const auto p = static_cast<tx::TxPhase>(phase);
    const bool keyed = p == tx::TxPhase::Keying || p == tx::TxPhase::Sending ||
                       p == tx::TxPhase::Unkeying;
    ptt_label_->setVisible(keyed);
}

void MainWindow::open_settings() {
    SettingsDialog dialog(state_->config(), this);
    // Test CAT / Test PTT results are shown in a message box the
    // operator dismisses; the log keeps what it said.
    connect(&dialog, &SettingsDialog::rigTestFinished, state_,
            [this](bool ok, const QString& message) {
                state_->log_event("rig",
                                  ok ? log::Severity::Info : log::Severity::Error,
                                  tr("rig test: %1").arg(message));
            });
    if (dialog.exec() != QDialog::Accepted) return;

    // The sequence is the window's, not the dialog's: apply, save,
    // relabel, reconnect the rig, and reload the model *only* if the
    // checkpoint actually changed -- reloading unconditionally would
    // re-download or re-open it every time the operator adjusted an
    // unrelated setting.
    const std::string previous_model = state_->config().model_path;
    const std::string previous_precision = state_->config().precision;
    dialog.apply_to(state_->config());
    state_->save_config();
    update_station_label();
    rx_panel_->sync_from_config();
    // Picture refinement can be switched on or off here, and either way
    // it has to take effect now: on, and a run starts for what is
    // already composed; off, and any refined latents are discarded.
    tx_panel_->sync_from_config();
    state_->connect_rig();

    if (state_->config().model_path != previous_model ||
        state_->config().precision != previous_precision) {
        model_label_->setText(tr("Loading model..."));
        state_->load_model_async();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (tx_panel_->transmitting()) {
        const auto answer = QMessageBox::question(
            this, tr("Transmitting"),
            tr("A transmission is in progress. Stop it and quit?"));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        tx_panel_->cancel();
    }
    rx_panel_->stop();
    state_->disconnect_rig();
    state_->save_config();
    QMainWindow::closeEvent(event);
}

}  // namespace sstvae::gui
