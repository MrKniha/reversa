// Main window: functions on the left, listing / pseudocode / graphs / tables in the middle,
// details and cross-references on the right. Analysis runs on a worker thread with progress and
// cancel; every edit goes through the Session (undoable, published on its event bus).
#pragma once

#include <QAction>
#include <QMainWindow>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "cli/cli.hpp"
#include "gui/dialogs.hpp"
#include "gui/views.hpp"
#include "session/session.hpp"

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QTabWidget;
class QTableWidget;
class QTextBrowser;
class QTreeWidget;
class QPlainTextEdit;

namespace rv::gui {

class GraphView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(cli::Args args, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Entry points (also used by the GUI tests).
    void open_path(const QString& path);
    void open_demo();
    bool busy() const { return worker_.joinable(); }
    Session* session() const { return session_.get(); }
    std::optional<Addr> current_function() const { return current_func_; }
    std::optional<Addr> current_address() const { return current_addr_; }
    void go_to(Addr addr, bool record = true);
    void select_variable(const std::string& key) { selected_var_ = key; refresh_code(); }
    void run_search(const QString& text);
    bool save_project(const QString& path = {});
    void cancel_analysis();
    QAction* command(const QString& id) const;
    QTabWidget* tabs() const { return tabs_; }

    // Replaceable for tests: text prompt (nullopt = cancelled) and "unsaved changes" question.
    std::function<std::optional<QString>(const QString& title, const QString& label, const QString& value)> prompt;
    std::function<int(const QString& text)> ask_unsaved;  // QMessageBox::Save / Discard / Cancel
    std::function<void(const QString& title, const QString& text, const QString& detail)> show_error;

    QTreeWidget* functions_view() const { return functions_; }
    QTextBrowser* listing_view() const { return listing_; }
    QTextBrowser* pseudocode_view() const { return pseudo_; }
    QTextBrowser* details_view() const { return details_; }
    QTableWidget* search_table() const { return search_; }
    GraphView* cfg_view() const { return cfg_; }
    GraphView* callgraph_view() const { return callgraph_; }

Q_SIGNALS:
    void analysisFinished(bool ok);

protected:
    void closeEvent(QCloseEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void build_ui();
    void build_commands();
    void apply_settings();
    void start_session(std::unique_ptr<Session> s, const QString& label);
    void on_analysis_done(bool ok, const QString& error, const QString& detail);
    bool confirm_discard();
    void set_busy(bool b);
    void update_title();
    void update_actions();

    // refreshing
    void schedule_refresh();
    void refresh_all();
    void refresh_functions();
    void refresh_code();
    void refresh_details();
    void refresh_tables();
    void refresh_graphs();
    void refresh_views_tab();
    void fill_table(QTableWidget* t, const QStringList& headers, const std::vector<QStringList>& rows,
                    const std::vector<Addr>& addrs);

    // commands
    void cmd_open();
    void cmd_save_as();
    void cmd_undo();
    void cmd_redo();
    void cmd_rename();
    void cmd_comment();
    void cmd_bookmark();
    void cmd_goto();
    void cmd_back();
    void cmd_report(const QString& fmt);
    void cmd_settings();
    void cmd_advanced_search(const QString& kind);
    void cmd_diagnostics();
    void cmd_plugins();
    void cmd_relocations();
    void cmd_onboarding();
    void on_link(const QUrl& url);
    void message(const QString& text, int ms = 5000);
    void report_error(const std::exception& e);
    std::optional<QString> ask(const QString& title, const QString& label, const QString& value = {});

    cli::Args args_;
    std::unique_ptr<Session> session_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    int event_sub_ = 0;
    bool refresh_pending_ = false;
    uint64_t seen_revision_ = 0;
    std::optional<Addr> current_func_, current_addr_;
    std::string selected_var_;
    std::vector<Addr> history_;
    UiSettings settings_;
    Palette pal_;
    std::vector<CommandDef> command_defs_;
    std::map<QString, QAction*> actions_;
    bool first_show_ = true;

    // widgets
    QLineEdit* search_box_ = nullptr;
    QLineEdit* func_filter_ = nullptr;
    QTreeWidget* functions_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTextBrowser* listing_ = nullptr;
    QTextBrowser* pseudo_ = nullptr;
    GraphView* cfg_ = nullptr;
    GraphView* callgraph_ = nullptr;
    QSpinBox* cg_depth_ = nullptr;
    QCheckBox* cg_imports_ = nullptr;
    QTableWidget *strings_ = nullptr, *imports_ = nullptr, *exports_ = nullptr, *sections_ = nullptr,
                 *bookmarks_ = nullptr, *search_ = nullptr;
    QPlainTextEdit* hex_ = nullptr;
    QComboBox* views_combo_ = nullptr;
    QTextBrowser* views_text_ = nullptr;
    QTextBrowser* details_ = nullptr;
    QTextBrowser* welcome_ = nullptr;
    std::vector<QLabel*> hints_;
    QProgressBar* progress_ = nullptr;
    QPushButton* cancel_btn_ = nullptr;
    QLabel* status_ = nullptr;
};

}  // namespace rv::gui
