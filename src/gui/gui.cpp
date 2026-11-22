#include "gui/gui.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QTimer>

#include "cli/cli.hpp"
#include "gui/dialogs.hpp"
#include "gui/i18n.hpp"
#include "gui/main_window.hpp"

namespace rv::gui {

bool display_available() {
#if defined(__linux__) || defined(__FreeBSD__)
    auto set = [](const char* v) { return !qEnvironmentVariableIsEmpty(v); };
    return set("QT_QPA_PLATFORM") || set("DISPLAY") || set("WAYLAND_DISPLAY");
#else
    return true;
#endif
}

int run(const cli::Args& args) {
    static int argc = 1;
    static char name[] = "reversa";
    static char* argv[] = {name, nullptr};
    QApplication app(argc, argv);
    QApplication::setApplicationName("REVERSA");
    QApplication::setOrganizationName("REVERSA");
    QApplication::setApplicationVersion(REVERSA_VERSION);
    QApplication::setWindowIcon(app_icon());
    set_language(pick_language(UiSettings::load(false).language));
    MainWindow w(args);
    w.show();
    if (args.demo) QTimer::singleShot(0, &w, [&w] { w.open_demo(); });
    else if (!args.file.empty()) {
        QString path = QString::fromStdString(args.file);
        QTimer::singleShot(0, &w, [&w, path] { w.open_path(path); });
    }
    return app.exec();
}

}  // namespace rv::gui
