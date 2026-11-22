// GUI smoke test (QTest, offscreen): demo -> analysis -> navigation -> rename -> search -> notes
// -> undo -> save -> settings. Run: QT_QPA_PLATFORM=offscreen reversa_gui_tests
#include <QComboBox>
#include <QMenuBar>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTreeWidget>

#include "cli/cli.hpp"
#include "gui/dialogs.hpp"
#include "gui/graph_view.hpp"
#include "gui/i18n.hpp"
#include "gui/main_window.hpp"
#ifdef REVERSA_WITH_PYTHON
#include "python/embed.hpp"
#endif

using namespace rv;
using namespace rv::gui;

class GuiTest : public QObject {
    Q_OBJECT
    QTemporaryDir home_;

    static void wait_ready(MainWindow& w) {
        QTRY_VERIFY_WITH_TIMEOUT(!w.busy(), 60000);
        QVERIFY(w.session() && w.session()->analysis);
    }
    // Screenshots for the docs and for reviewing the layout: REVERSA_SHOTS=<dir>
    static void shot(QWidget& w, const char* name) {
        QByteArray dir = qgetenv("REVERSA_SHOTS");
        QCoreApplication::processEvents();
        if (!dir.isEmpty()) w.grab().save(QString::fromUtf8(dir) + "/" + name + ".png");
    }
    static int find_function(QTreeWidget* t, const QString& name) {
        for (int i = 0; i < t->topLevelItemCount(); i++)
            if (t->topLevelItem(i)->text(0).endsWith(name)) return i;
        return -1;
    }

private Q_SLOTS:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("HOME", home_.path().toUtf8());
        qputenv("USERPROFILE", home_.path().toUtf8());
        QCoreApplication::setOrganizationName("REVERSA-test");
        QCoreApplication::setApplicationName("REVERSA-test");
        QSettings st;
        st.clear();
        st.setValue("ui/onboarding_done", true);
    }

    void demo_workflow() {
        cli::Args args;
        args.no_plugins = true;
        MainWindow w(args);
        QStringList errors;
        w.show_error = [&](const QString&, const QString& text, const QString&) { errors << text; };
        w.show();
        QSignalSpy finished(&w, &MainWindow::analysisFinished);
        w.open_demo();
        wait_ready(w);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), true);

        // analysis landed on main: functions list, listing, pseudocode, details
        auto main = w.session()->resolve("main");
        QVERIFY(main);
        QCOMPARE(w.current_function(), main);
        QVERIFY(w.functions_view()->topLevelItemCount() > 5);
        QVERIFY(find_function(w.functions_view(), "main") >= 0);
        QVERIFY(w.listing_view()->toPlainText().contains("main:"));
        QVERIFY(w.pseudocode_view()->toPlainText().contains("check_password"));
        QVERIFY(w.details_view()->toPlainText().contains("Volá"));
        w.resize(1400, 860);
        shot(w, "listing");
        w.command("pseudocode")->trigger();
        shot(w, "pseudocode");
        w.command("listing")->trigger();

        // navigation: click a function in the list, then go back in history
        int row = find_function(w.functions_view(), "check_password");
        QVERIFY(row >= 0);
        auto* item = w.functions_view()->topLevelItem(row);
        QTest::mouseClick(w.functions_view()->viewport(), Qt::LeftButton, {},
                          w.functions_view()->visualItemRect(item).center());
        QCOMPARE(w.current_function(), w.session()->resolve("check_password"));
        QVERIFY(w.pseudocode_view()->toPlainText().contains("strcmp"));
        w.command("back")->trigger();
        QCOMPARE(w.current_function(), main);

        // graphs
        w.command("cfg")->trigger();
        QVERIFY(w.cfg_view()->node_count() >= 2);
        shot(w, "cfg");
        w.command("callgraph")->trigger();
        QVERIFY(w.callgraph_view()->node_count() >= 2);
        shot(w, "callgraph");
        w.command("listing")->trigger();

        // rename through the command (prompt replaced), propagates to the function list
        w.prompt = [](const QString&, const QString&, const QString&) { return std::optional<QString>("start_here"); };
        w.command("rename")->trigger();
        QCOMPARE(w.session()->name_of(*main), std::string("start_here"));
        QTRY_VERIFY(find_function(w.functions_view(), "start_here") >= 0);
        QTRY_VERIFY(w.listing_view()->toPlainText().contains("start_here:"));
        QVERIFY(w.isWindowModified());

        // variable rename in the pseudocode
        w.go_to(*w.session()->resolve("check_password"));
        w.command("pseudocode")->trigger();
        w.select_variable("a1");
        w.prompt = [](const QString&, const QString&, const QString&) { return std::optional<QString>("password"); };
        w.command("rename")->trigger();
        QTRY_VERIFY(w.pseudocode_view()->toPlainText().contains("password"));

        // comment and bookmark
        w.prompt = [](const QString&, const QString&, const QString&) { return std::optional<QString>("checks the password"); };
        w.command("comment")->trigger();
        w.command("bookmark")->trigger();
        QCOMPARE(w.session()->annotations.comments.size(), size_t(1));
        QCOMPARE(w.session()->annotations.bookmarks.size(), size_t(1));

        // search: by name and an advanced instruction search
        w.run_search("check");
        QCOMPARE(w.tabs()->currentIndex(), 9);
        QVERIFY(w.search_table()->rowCount() >= 2);
        shot(w, "search");
        w.prompt = [](const QString&, const QString&, const QString&) { return std::optional<QString>("call"); };
        w.command("search_insn")->trigger();
        QVERIFY(w.search_table()->rowCount() > 0);
        w.prompt = [](const QString&, const QString&, const QString&) { return std::optional<QString>("zz"); };
        w.command("search_hex")->trigger();  // invalid pattern: friendly error, no crash
        QCOMPARE(errors.size(), 1);

        // undo everything back to the original name
        for (int i = 0; i < 4; i++) w.command("undo")->trigger();
        QCOMPARE(w.session()->name_of(*main), std::string("main"));
        QVERIFY(w.session()->annotations.comments.empty());
        w.command("redo")->trigger();
        QCOMPARE(w.session()->name_of(*main), std::string("start_here"));

        // save the project, reopen it
        QString proj = home_.filePath("demo.rvproj");
        QVERIFY(w.save_project(proj));
        QVERIFY(QFile::exists(proj));
        QVERIFY(!w.isWindowModified());
        w.open_path(proj);
        wait_ready(w);
        QCOMPARE(w.session()->name_of(*main), std::string("start_here"));

        // a file that cannot be loaded gives a friendly error
        QString empty = home_.filePath("empty.bin");
        QFile(empty).open(QIODevice::WriteOnly);
        w.open_path(empty);
        QCOMPARE(errors.size(), 2);
        QVERIFY(errors.last().contains("empty"));

        // theme switch and shortcuts
        QCOMPARE(w.command("rename")->shortcut(), QKeySequence("N"));
        w.command("theme")->trigger();
        w.command("listing")->trigger();
        shot(w, "theme2");
        w.command("theme")->trigger();
        QVERIFY(w.listing_view()->toPlainText().contains("start_here:"));
    }

    void cancel_analysis() {
        cli::Args args;
        args.no_plugins = true;
        MainWindow w(args);
        w.show_error = [](const QString&, const QString&, const QString&) { QFAIL("cancel must not show an error"); };
        QSignalSpy finished(&w, &MainWindow::analysisFinished);
        w.open_demo();
        w.cancel_analysis();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60000);
        QVERIFY(!w.busy());
        // either the analysis was faster than the cancel, or the window is back at the start page
        QCOMPARE(finished.at(0).at(0).toBool(), w.session() != nullptr);
    }

    // Python plugins run their hooks on the analysis worker thread (GIL handling) and add views.
    void plugins_on_worker_thread() {
#ifdef REVERSA_WITH_PYTHON
        if (!py::available()) QSKIP("Python is not available");
        py::release_gil_for_threads();
        cli::Args args;
        args.plugin_dirs.push_back(std::string(REVERSA_SOURCE_DIR) + "/examples/plugins");
        MainWindow w(args);
        w.show_error = [](const QString&, const QString& t, const QString&) { QFAIL(qPrintable(t)); };
        w.open_demo();
        wait_ready(w);
        QVERIFY(w.session()->plugins);
        bool hello = false;
        for (auto& [name, render] : w.session()->views) hello |= name == "Hello plugin";
        QVERIFY(hello);
        w.tabs()->setCurrentIndex(10);
        auto* combo = w.findChild<QComboBox*>("views_combo");
        combo->setCurrentIndex(combo->findText("Hello plugin"));
        QVERIFY(w.findChild<QTextBrowser*>("views")->toPlainText().contains("Analyzovaných funkcií"));
        bool crash_reported = false;  // the crashing example plugin is isolated and reported
        for (auto& d : w.session()->diagnostics()) crash_reported |= d.message.find("crashing") != std::string::npos;
        QVERIFY(crash_reported);
#else
        QSKIP("built without Python");
#endif
    }

    void onboarding_and_settings() {
        OnboardingDialog d;
        QCOMPARE(d.pages(), 5);
        for (int i = 0; i < 4; i++) d.next();
        QCOMPARE(d.page(), 4);
        d.next();
        QCOMPARE(d.result(), int(QDialog::Accepted));

        UiSettings s;
        s.shortcuts.push_back({"rename", QKeySequence("Ctrl+R")});
        s.theme = "dark";
        s.save();
        UiSettings back = UiSettings::load(false);
        QCOMPARE(back.theme, QString("dark"));
        QCOMPARE(back.key({"rename", "x", QKeySequence("N")}), QKeySequence("Ctrl+R"));
        QCOMPARE(back.key({"comment", "x", QKeySequence(";")}), QKeySequence(";"));
        cli::Args args;
        args.no_plugins = true;
        MainWindow w(args);
        QCOMPARE(w.command("rename")->shortcut(), QKeySequence("Ctrl+R"));
        QSettings().remove("shortcuts");
    }

    void languages() {
        QCOMPARE(int(gui::languages().size()), 5);
        QCOMPARE(pick_language("de"), QString("de"));
        QCOMPARE(pick_language("xx"), QString("en"));
        QCOMPARE(pick_language(""), QString("en"));
        UiSettings s = UiSettings::load(false);
        s.language = "fr";
        s.save();
        QCOMPARE(UiSettings::load(false).language, QString("fr"));
        QSettings().remove("ui/language");
        QSettings().setValue("ui/onboarding_done", true);  // no modal guide when the window is shown

        const std::pair<const char*, const char*> menus[] = {
            {"en", "&File"}, {"de", "&Datei"}, {"fr", "&Fichier"}, {"zh", "文件(&F)"}};
        for (auto& [code, file_menu] : menus) {
            set_language(code);
            QCOMPARE(current_language(), QString(code));
            QVERIFY(T("Volá") != QString::fromUtf8("Volá"));  // free-function strings (views.cpp)
            cli::Args args;
            args.no_plugins = true;
            MainWindow w(args);
            QCOMPARE(w.menuBar()->actions().at(0)->text(), QString::fromUtf8(file_menu));
            if (QByteArray(code) == "zh" || QByteArray(code) == "en") {
                w.resize(1280, 800);
                w.show();
                w.open_demo();
                wait_ready(w);
                shot(w, code == QByteArray("zh") ? "lang_zh" : "lang_en");
            }
        }
        set_language("sk");
        QCOMPARE(T("Volá"), QString::fromUtf8("Volá"));
    }
};

QTEST_MAIN(GuiTest)
#include "test_gui.moc"
