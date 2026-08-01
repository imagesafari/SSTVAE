// The application window.
//
// **The receive and transmit panels sit side by side in a splitter,
// not in tabs.** The reference (and this port until now) put them in a
// `QTabWidget`, which meant composing a picture was done blind: no
// waterfall, no decode progress, no incoming preview. On the band that
// is the wrong trade -- you prepare the next transmission *while*
// listening, and the thing you most want to see while composing is
// whether someone else is sending. The splitter lets either side
// dominate, and either collapse, so a narrow screen is still workable.
//
// The wiring between the panels lives here rather than in either of
// them, because all of it is about the pair: half duplex (our own
// transmission must not be decoded back into a received picture),
// pausing frequency polling while keyed, and handing the most recent
// received picture to the transmitter as an overlay inset.

#ifndef SSTVAE_GUI_MAIN_WINDOW_HPP
#define SSTVAE_GUI_MAIN_WINDOW_HPP

#include <QMainWindow>

class QDockWidget;
class QLabel;
class QMenu;
class QSplitter;
class QTimer;

namespace sstvae::gui {

class AppState;
class LogPane;
class ReceivePanel;
class TransmitPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

public slots:
    void open_settings();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void build_menu();
    void build_status_bar();
    void build_log_dock();
    void update_station_label();
    void on_model_loaded();
    void on_rig_status(const QString& text, bool error);
    void on_model_progress(qlonglong received, qlonglong total);
    void on_tx_state(int phase);
    void refresh_rig_error_age();

    AppState* state_ = nullptr;
    QSplitter* panes_ = nullptr;
    ReceivePanel* rx_panel_ = nullptr;
    TransmitPanel* tx_panel_ = nullptr;
    QLabel* ptt_label_ = nullptr;
    QLabel* station_label_ = nullptr;
    QLabel* rig_label_ = nullptr;
    QLabel* model_label_ = nullptr;
    QDockWidget* log_dock_ = nullptr;
    LogPane* log_pane_ = nullptr;
    QMenu* view_menu_ = nullptr;

    // The rig label's error-age display: a CAT failure keeps its text,
    // and this pair appends "(N s ago)" so a stale error cannot pass
    // for a fresh one (the controller deduplicates identical repeats,
    // so the text alone never says how old it is).
    QString rig_error_text_;
    qint64 rig_error_since_ms_ = 0;
    QTimer* rig_error_timer_ = nullptr;
};

}  // namespace sstvae::gui

#endif
