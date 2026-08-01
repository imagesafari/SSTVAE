// Render the application's windows to PNG files, headless.
//
// A GUI's layout bugs are not the kind a test catches. This does not
// try to: it just makes *looking* cheap, so a layout can be checked at
// several sizes without a display, a compositor, or a human. It found
// two things the day it was written -- help text clipped mid-sentence
// by a form layout with too little height, and every combo and spin box
// in the dialog rendering with zero left padding because a stylesheet
// somewhere else in the app had swapped the platform style out.
//
// A tool rather than a ctest, for the same reason `sstvae-audio-check`
// is: there is no assertion to make. "Is this laid out well" has no
// oracle, and a screenshot test that compares against a stored PNG
// fails on every font and theme it was not recorded with.
//
// Only widgets that can be built without touching the network or a
// radio are offered. `MainWindow` is deliberately absent: it starts a
// model load and opens the rig, neither of which belongs in a
// screenshot tool.

#include <QApplication>
#include <QPixmap>
#include <QStringList>
#include <QTabWidget>

#include <cstdio>
#include <string>

#include "app_state.hpp"
#include "banner.hpp"
#include "log/log.hpp"
#include "log_pane.hpp"
#include "settings/settings.hpp"
#include "settings_dialog.hpp"
#include "tx_panel.hpp"

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: sstvae-gui-shot [--out DIR] [--size WxH] [--tab N]\n"
                 "\n"
                 "  --out DIR    where to write the PNGs (default: .)\n"
                 "  --size WxH   window size (default: the window's own)\n"
                 "  --tab N      only this settings tab; default is all\n"
                 "  --transmit   also shoot the transmit panel\n"
                 "  --log        also shoot the log pane and error banner\n"
                 "\n"
                 "Writes settings-<n>-<name>.png, one per tab.\n");
}

// A configuration with the optional controls switched on, so the shots
// show the dialog at its fullest rather than at its emptiest.
sstvae::settings::Config demo_config() {
    sstvae::settings::Config config;
    config.callsign = "N0CALL";
    config.rig.enabled = true;
    config.rig.device = "/dev/ttyUSB0";
    config.rig.baud = 38400;
    // On, so the transmit shot exercises the refinement wiring as well
    // as showing the control in its non-default state.
    config.transmit.optimize = true;
    return config;
}

}  // namespace

int main(int argc, char** argv) {
    // Before QApplication: the platform plugin is chosen at construction.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QString out = QStringLiteral(".");
    int width = 0;
    int height = 0;
    int only_tab = -1;
    bool transmit = false;
    bool log_widgets = false;

    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args[i];
        if (arg == QLatin1String("--out") && i + 1 < args.size()) {
            out = args[++i];
        } else if (arg == QLatin1String("--size") && i + 1 < args.size()) {
            const QStringList parts = args[++i].split(QLatin1Char('x'));
            if (parts.size() != 2) {
                usage();
                return 2;
            }
            width = parts[0].toInt();
            height = parts[1].toInt();
        } else if (arg == QLatin1String("--tab") && i + 1 < args.size()) {
            only_tab = args[++i].toInt();
        } else if (arg == QLatin1String("--transmit")) {
            transmit = true;
        } else if (arg == QLatin1String("--log")) {
            log_widgets = true;
        } else {
            usage();
            return 2;
        }
    }

    const sstvae::settings::Config config = demo_config();
    sstvae::gui::SettingsDialog dialog(config);
    if (width > 0 && height > 0) dialog.resize(width, height);
    dialog.show();

    auto* tabs = dialog.findChild<QTabWidget*>();
    if (tabs == nullptr) {
        std::fprintf(stderr, "sstvae-gui-shot: no tab widget found\n");
        return 1;
    }

    // Opt-in, because unlike the settings dialog this one needs an
    // AppState -- which begins resolving the checkpoint as soon as it is
    // constructed, and on a cold cache that means an HTTP download. A
    // smoke test that proves Qt links should not also depend on the
    // network being up.
    if (transmit) {
        sstvae::gui::AppState state;
        sstvae::gui::TransmitPanel panel(&state);
        panel.resize(width > 0 ? width : 1000, height > 0 ? height : 700);
        panel.show();
        app.processEvents();
        const QString path = QStringLiteral("%1/transmit.png").arg(out);
        panel.grab().save(path);
        std::printf("%s\n", path.toLocal8Bit().constData());
    }

    // The observability widgets, with representative content: the pane
    // filled from a preloaded log, and the banner showing the message
    // whose survivability is the whole reason it exists.
    if (log_widgets) {
        sstvae::log::StatusLog log;
        log.append("app", sstvae::log::Severity::Warning,
                   "config: receive.decode_every: not a number; using default");
        log.append("rig", sstvae::log::Severity::Info,
                   "Rig: 14.2300 MHz (Elecraft K4)");
        log.append("rx", sstvae::log::Severity::Info,
                   "sync acquired: mode B de KD8XYZ");
        log.append("rx", sstvae::log::Severity::Info,
                   "reception complete: mode B de KD8XYZ, 32/32 frames,  SNR "
                   "8.3dB -- saved /home/op/Pictures/rx/KD8XYZ-B-14230.png");
        log.append("tx", sstvae::log::Severity::Info, "keying rig");
        log.append("tx", sstvae::log::Severity::Error,
                   "PTT OFF FAILED: read timeout -- the rig may still be "
                   "transmitting. Unkey it manually.");
        sstvae::gui::LogPane pane(&log);
        pane.resize(width > 0 ? width : 1000, 180);
        pane.show();
        app.processEvents();
        const QString pane_path = QStringLiteral("%1/log-pane.png").arg(out);
        pane.grab().save(pane_path);
        std::printf("%s\n", pane_path.toLocal8Bit().constData());

        sstvae::gui::ErrorBanner banner;
        banner.show_error(QStringLiteral(
            "PTT OFF FAILED: read timeout -- the rig may still be "
            "transmitting. Unkey it manually."));
        banner.resize(width > 0 ? width : 1000, banner.sizeHint().height());
        banner.show();
        app.processEvents();
        const QString banner_path = QStringLiteral("%1/banner.png").arg(out);
        banner.grab().save(banner_path);
        std::printf("%s\n", banner_path.toLocal8Bit().constData());
    }

    for (int i = 0; i < tabs->count(); ++i) {
        if (only_tab >= 0 && i != only_tab) continue;
        tabs->setCurrentIndex(i);
        // Let the layout settle before grabbing: a tab that has just
        // been shown has not been laid out yet, and the shot would be of
        // the previous one's geometry.
        app.processEvents();
        const QString name = tabs->tabText(i).toLower().replace(QLatin1Char(' '),
                                                               QLatin1Char('-'));
        const QString path =
            QStringLiteral("%1/settings-%2-%3.png").arg(out).arg(i).arg(name);
        if (!dialog.grab().save(path)) {
            std::fprintf(stderr, "sstvae-gui-shot: could not write %s\n",
                         path.toLocal8Bit().constData());
            return 1;
        }
        std::printf("%s\n", path.toLocal8Bit().constData());
    }
    return 0;
}
