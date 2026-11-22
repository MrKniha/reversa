#include "gui/main_window.hpp"

#include <algorithm>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <set>

#include "core/errors.hpp"
#include "core/text.hpp"
#include "gui/graph_view.hpp"
#include "gui/i18n.hpp"
#include "report/report.hpp"
#include "plugins/native.hpp"

namespace rv::gui {

namespace {

enum Tab { T_LISTING, T_PSEUDO, T_CFG, T_CALLS, T_STRINGS, T_IMPORTS, T_EXPORTS, T_SECTIONS, T_BOOKMARKS, T_SEARCH, T_VIEWS };

QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s8(const QString& s) { return s.toStdString(); }
QString hexq(Addr a) { return q(hex(a)); }

void apply_theme(bool dark) {
    qApp->setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    if (dark) {
        p.setColor(QPalette::Window, QColor(37, 37, 38));
        p.setColor(QPalette::WindowText, QColor(220, 220, 220));
        p.setColor(QPalette::Base, QColor(30, 30, 30));
        p.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
        p.setColor(QPalette::ToolTipBase, QColor(50, 50, 50));
        p.setColor(QPalette::ToolTipText, QColor(230, 230, 230));
        p.setColor(QPalette::Text, QColor(212, 212, 212));
        p.setColor(QPalette::Button, QColor(51, 51, 55));
        p.setColor(QPalette::ButtonText, QColor(220, 220, 220));
        p.setColor(QPalette::Highlight, QColor(38, 79, 120));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, QColor(79, 193, 255));
        p.setColor(QPalette::PlaceholderText, QColor(140, 140, 140));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
    } else {
        p = qApp->style()->standardPalette();
    }
    qApp->setPalette(p);
}

}  // namespace

MainWindow::MainWindow(cli::Args args, QWidget* parent) : QMainWindow(parent), args_(std::move(args)) {
    settings_ = UiSettings::load(qApp->palette().color(QPalette::Window).lightness() < 128);
    prompt = [this](const QString& title, const QString& label, const QString& value) -> std::optional<QString> {
        bool ok = false;
        QString r = QInputDialog::getText(this, title, label, QLineEdit::Normal, value, &ok);
        if (!ok) return std::nullopt;
        return r;
    };
    ask_unsaved = [this](const QString& text) {
        return QMessageBox::question(this, tr("Neuložené zmeny"), text,
                                     QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    };
    show_error = [this](const QString& title, const QString& text, const QString& detail) {
        QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, this);
        if (!detail.isEmpty()) {
            box.setDetailedText(detail);
            auto* copy = box.addButton(tr("Skopírovať detail pre správcu"), QMessageBox::ActionRole);
            connect(copy, &QPushButton::clicked, this, [detail] { QApplication::clipboard()->setText(detail); });
        }
        box.exec();
    };
    build_commands();
    build_ui();
    apply_settings();
    update_actions();
    update_title();
    resize(1400, 860);
}

MainWindow::~MainWindow() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    if (session_ && event_sub_) session_->events.unsubscribe(event_sub_);
}

// ------------------------------------------------------------ construction
void MainWindow::build_commands() {
    command_defs_ = {
        {"open", tr("Otvoriť súbor…"), QKeySequence::Open},
        {"demo", tr("Skúšobná binárka"), QKeySequence()},
        {"save", tr("Uložiť projekt"), QKeySequence::Save},
        {"save_as", tr("Uložiť projekt ako…"), QKeySequence("Ctrl+Shift+S")},
        {"undo", tr("Vrátiť späť"), QKeySequence::Undo},
        {"redo", tr("Znova"), QKeySequence("Ctrl+Y")},
        {"rename", tr("Premenovať"), QKeySequence("N")},
        {"comment", tr("Komentár"), QKeySequence(";")},
        {"bookmark", tr("Záložka"), QKeySequence("B")},
        {"goto", tr("Skočiť na adresu…"), QKeySequence("G")},
        {"back", tr("Späť v histórii"), QKeySequence("Alt+Left")},
        {"search", tr("Hľadať"), QKeySequence::Find},
        {"listing", tr("Listing"), QKeySequence("1")},
        {"pseudocode", tr("Pseudokód"), QKeySequence("2")},
        {"cfg", tr("Graf funkcie"), QKeySequence("3")},
        {"callgraph", tr("Graf volaní"), QKeySequence("4")},
        {"report_md", tr("Exportovať report (Markdown)…"), QKeySequence()},
        {"report_html", tr("Exportovať report (HTML)…"), QKeySequence()},
        {"search_hex", tr("Hľadať bajty (hex vzor)…"), QKeySequence("Ctrl+Shift+H")},
        {"search_insn", tr("Hľadať inštrukcie (regex)…"), QKeySequence("Ctrl+Shift+I")},
        {"search_code", tr("Hľadať v pseudokóde (regex)…"), QKeySequence("Ctrl+Shift+P")},
        {"plugins", tr("Pluginy"), QKeySequence()},
        {"diagnostics", tr("Diagnostika načítania"), QKeySequence()},
        {"relocations", tr("Relokácie"), QKeySequence()},
        {"theme", tr("Prepnúť svetlý/tmavý motív"), QKeySequence("Ctrl+Shift+T")},
        {"settings", tr("Nastavenia…"), QKeySequence("Ctrl+,")},
        {"help", tr("Úvodný sprievodca"), QKeySequence::HelpContents},
        {"quit", tr("Koniec"), QKeySequence::Quit},
    };
    auto add = [this](const QString& id, std::function<void()> fn) {
        auto* a = new QAction(this);
        a->setObjectName("cmd_" + id);
        for (auto& c : command_defs_)
            if (c.id == id) a->setText(c.text);
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, std::move(fn));
        addAction(a);
        actions_[id] = a;
    };
    add("open", [this] { cmd_open(); });
    add("demo", [this] { open_demo(); });
    add("save", [this] { save_project(); });
    add("save_as", [this] { cmd_save_as(); });
    add("undo", [this] { cmd_undo(); });
    add("redo", [this] { cmd_redo(); });
    add("rename", [this] { cmd_rename(); });
    add("comment", [this] { cmd_comment(); });
    add("bookmark", [this] { cmd_bookmark(); });
    add("goto", [this] { cmd_goto(); });
    add("back", [this] { cmd_back(); });
    add("search", [this] { search_box_->setFocus(); search_box_->selectAll(); });
    add("listing", [this] { tabs_->setCurrentIndex(T_LISTING); });
    add("pseudocode", [this] { tabs_->setCurrentIndex(T_PSEUDO); });
    add("cfg", [this] { tabs_->setCurrentIndex(T_CFG); });
    add("callgraph", [this] { tabs_->setCurrentIndex(T_CALLS); });
    add("report_md", [this] { cmd_report("md"); });
    add("report_html", [this] { cmd_report("html"); });
    add("search_hex", [this] { cmd_advanced_search("hex"); });
    add("search_insn", [this] { cmd_advanced_search("insn"); });
    add("search_code", [this] { cmd_advanced_search("code"); });
    add("plugins", [this] { cmd_plugins(); });
    add("diagnostics", [this] { cmd_diagnostics(); });
    add("relocations", [this] { cmd_relocations(); });
    add("theme", [this] {
        settings_.theme = settings_.theme == "dark" ? "light" : "dark";
        settings_.save();
        apply_settings();
    });
    add("settings", [this] { cmd_settings(); });
    add("help", [this] { cmd_onboarding(); });
    add("quit", [this] { close(); });
}

QAction* MainWindow::command(const QString& id) const {
    auto it = actions_.find(id);
    return it == actions_.end() ? nullptr : it->second;
}

void MainWindow::build_ui() {
    setWindowIcon(app_icon());
    // menus
    auto* file = menuBar()->addMenu(tr("&Súbor"));
    for (auto id : {"open", "demo", "save", "save_as"}) file->addAction(command(id));
    file->addSeparator();
    file->addAction(command("report_md"));
    file->addAction(command("report_html"));
    file->addSeparator();
    file->addAction(command("quit"));
    auto* edit = menuBar()->addMenu(tr("&Upraviť"));
    for (auto id : {"undo", "redo"}) edit->addAction(command(id));
    edit->addSeparator();
    for (auto id : {"rename", "comment", "bookmark"}) edit->addAction(command(id));
    auto* go = menuBar()->addMenu(tr("&Prejsť"));
    for (auto id : {"goto", "back", "search"}) go->addAction(command(id));
    go->addSeparator();
    for (auto id : {"listing", "pseudocode", "cfg", "callgraph"}) go->addAction(command(id));
    auto* adv = menuBar()->addMenu(tr("P&okročilé"));
    for (auto id : {"search_hex", "search_insn", "search_code"}) adv->addAction(command(id));
    adv->addSeparator();
    for (auto id : {"plugins", "diagnostics", "relocations"}) adv->addAction(command(id));
    adv->addSeparator();
    auto* future = adv->addAction(tr("Patchovanie, diffing, emulácia a debugger – v ďalších verziách (pozri docs/architecture.md)"));
    future->setEnabled(false);
    auto* view = menuBar()->addMenu(tr("&Zobraziť"));
    view->addAction(command("theme"));
    view->addAction(command("settings"));
    auto* help = menuBar()->addMenu(tr("Pomo&c"));
    help->addAction(command("help"));
    help->addAction(tr("O programe REVERSA"), this, [this] {
        QMessageBox::about(this, tr("O programe REVERSA"),
                           tr("<b>REVERSA %1</b><br>Open-source nástroj na reverzné inžinierstvo (MIT).<br>"
                              "Statická analýza – analyzovaný program sa nikdy nespúšťa.<br>"
                              "Knižnice: Qt (LGPL-3.0), Capstone (BSD-3-Clause), SQLite (public domain).")
                               .arg(REVERSA_VERSION));
    });

    // toolbar
    auto* tb = addToolBar(tr("Hlavný panel"));
    tb->setObjectName("main_toolbar");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    command("open")->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    command("save")->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    command("undo")->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    command("redo")->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    command("demo")->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    for (auto id : {"open", "demo", "save", "undo", "redo"}) tb->addAction(command(id));
    tb->addSeparator();
    search_box_ = new QLineEdit;
    search_box_->setObjectName("search_box");
    search_box_->setPlaceholderText(tr("Hľadať funkciu, reťazec alebo adresu (Ctrl+F)…"));
    search_box_->setClearButtonEnabled(true);
    search_box_->setMinimumWidth(360);
    connect(search_box_, &QLineEdit::returnPressed, this, [this] { run_search(search_box_->text()); });
    tb->addWidget(search_box_);
    auto* adv_btn = new QToolButton;
    adv_btn->setText(tr("Pokročilé"));
    adv_btn->setMenu(adv);
    adv_btn->setPopupMode(QToolButton::InstantPopup);
    tb->addWidget(adv_btn);
    tb->addSeparator();
    tb->addAction(command("theme"));
    command("theme")->setIconText(tr("Motív"));
    tb->addAction(command("settings"));
    command("settings")->setIconText(tr("Nastavenia"));
    tb->addAction(command("help"));
    command("help")->setIconText(tr("Pomoc"));

    // left: functions
    auto* left = new QWidget;
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(4, 4, 4, 4);
    func_filter_ = new QLineEdit;
    func_filter_->setPlaceholderText(tr("Filtrovať…"));
    func_filter_->setClearButtonEnabled(true);
    functions_ = new QTreeWidget;
    functions_->setObjectName("functions");
    functions_->setHeaderLabels({tr("Meno"), tr("Adresa"), tr("Volaní"), tr("Veľkosť")});
    functions_->setRootIsDecorated(false);
    functions_->setSortingEnabled(true);
    functions_->setUniformRowHeights(true);
    functions_->header()->setStretchLastSection(false);
    functions_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; c++) functions_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    functions_->setToolTip(tr("Zoznam všetkých funkcií programu. Stĺpec Volaní = koľko iných funkcií ju volá. "
                              "Importy (šedé) sú funkcie z knižníc."));
    ll->addWidget(func_filter_);
    ll->addWidget(functions_);
    connect(func_filter_, &QLineEdit::textChanged, this, [this](const QString& f) {
        for (int i = 0; i < functions_->topLevelItemCount(); i++) {
            auto* it = functions_->topLevelItem(i);
            it->setHidden(!f.isEmpty() && !it->text(0).contains(f, Qt::CaseInsensitive) && !it->text(1).contains(f, Qt::CaseInsensitive));
        }
    });
    connect(functions_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        go_to(Addr(it->data(0, Qt::UserRole).toULongLong()));
        if (tabs_->currentIndex() > T_CALLS) tabs_->setCurrentIndex(T_LISTING);
    });
    auto* ldock = new QDockWidget(tr("Funkcie"), this);
    ldock->setObjectName("dock_functions");
    ldock->setWidget(left);
    ldock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, ldock);

    // right: details
    auto* right = new QWidget;
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(4, 4, 4, 4);
    details_ = new QTextBrowser;
    details_->setObjectName("details");
    details_->setOpenLinks(false);
    details_->setHtml(tr("<p>Kliknite na funkciu, adresu alebo reťazec – tu uvidíte, kto ich používa (cross-referencie).</p>"));
    connect(details_, &QTextBrowser::anchorClicked, this, &MainWindow::on_link);
    auto* btns = new QHBoxLayout;
    for (auto id : {"rename", "comment", "bookmark"}) {
        auto* b = new QPushButton(command(id)->text());
        connect(b, &QPushButton::clicked, command(id), &QAction::trigger);
        btns->addWidget(b);
    }
    rl->addWidget(details_);
    rl->addLayout(btns);
    auto* rdock = new QDockWidget(tr("Detaily"), this);
    rdock->setObjectName("dock_details");
    rdock->setWidget(right);
    rdock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, rdock);
    resizeDocks({ldock, rdock}, {380, 340}, Qt::Horizontal);

    // center
    auto* central = new QStackedWidget;
    welcome_ = new QTextBrowser;
    welcome_->setOpenLinks(false);
    welcome_->setHtml(tr("<div style=\"margin:40px\"><h1>REVERSA</h1>"
                         "<p style=\"font-size:large\">Otvorte binárny súbor (ELF, PE alebo surové dáta) alebo vyskúšajte "
                         "skúšobnú binárku. Všetko dôležité sa vyanalyzuje automaticky.</p>"
                         "<p><a href=\"open\">Otvoriť súbor…</a> &nbsp;·&nbsp; <a href=\"demo\">Skúšobná binárka</a> "
                         "&nbsp;·&nbsp; <a href=\"help\">Úvodný sprievodca</a></p>"
                         "<p style=\"color:gray\">REVERSA analyzuje staticky: skúmaný program sa nikdy nespúšťa.</p></div>"));
    connect(welcome_, &QTextBrowser::anchorClicked, this, [this](const QUrl& u) {
        if (auto* a = command(u.toString())) a->trigger();
    });
    tabs_ = new QTabWidget;
    tabs_->setObjectName("tabs");
    central->addWidget(welcome_);
    central->addWidget(tabs_);
    setCentralWidget(central);

    auto page = [this](QWidget* content, const QString& hint, QWidget* top = nullptr) {
        auto* w = new QWidget;
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(2, 2, 2, 2);
        auto* h = new QLabel(hint);
        h->setWordWrap(true);
        h->setMargin(5);
        h->setAutoFillBackground(true);
        h->setBackgroundRole(QPalette::AlternateBase);
        hints_.push_back(h);
        l->addWidget(h);
        if (top) l->addWidget(top);
        l->addWidget(content, 1);
        return w;
    };
    auto browser = [this](const char* name) {
        auto* b = new QTextBrowser;
        b->setObjectName(name);
        b->setOpenLinks(false);
        b->setLineWrapMode(QTextEdit::NoWrap);
        connect(b, &QTextBrowser::anchorClicked, this, &MainWindow::on_link);
        return b;
    };
    listing_ = browser("listing");
    pseudo_ = browser("pseudocode");
    tabs_->addTab(page(listing_, tr("Listing ukazuje inštrukcie procesora za sebou. Modré mená sú klikateľné – kliknutím "
                                   "skočíte na cieľ skoku alebo volania.")), tr("Listing"));
    tabs_->addTab(page(pseudo_, tr("Pseudokód je čitateľný prepis funkcie do jazyka podobného C. Je dočasný – pri "
                                  "pochybnostiach overte v listingu. Kliknutím na premennú ju vyberiete, klávesom N "
                                  "premenujete.")), tr("Pseudokód"));
    cfg_ = new GraphView;
    cfg_->setObjectName("cfg");
    tabs_->addTab(page(cfg_, tr("Graf toku riadenia (CFG) ukazuje bloky funkcie a skoky medzi nimi. Zelená = podmienka "
                               "splnená, červená = nesplnená, oranžový rámik = slučka. Kolieskom približujete.")),
                  tr("Graf funkcie"));
    callgraph_ = new GraphView;
    callgraph_->setObjectName("callgraph");
    auto* cg_bar = new QWidget;
    auto* cgl = new QHBoxLayout(cg_bar);
    cgl->setContentsMargins(0, 0, 0, 0);
    cg_depth_ = new QSpinBox;
    cg_depth_->setRange(1, 6);
    cg_depth_->setValue(2);
    cg_imports_ = new QCheckBox(tr("Zobraziť importy"));
    cg_imports_->setChecked(true);
    auto* fit = new QPushButton(tr("Prispôsobiť"));
    cgl->addWidget(new QLabel(tr("Hĺbka:")));
    cgl->addWidget(cg_depth_);
    cgl->addWidget(cg_imports_);
    cgl->addStretch();
    cgl->addWidget(fit);
    connect(cg_depth_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { refresh_graphs(); });
    connect(cg_imports_, &QCheckBox::toggled, this, [this] { refresh_graphs(); });
    connect(fit, &QPushButton::clicked, callgraph_, &GraphView::fit);
    tabs_->addTab(page(callgraph_, tr("Tento graf ukazuje, ktoré funkcie volajú ktoré – dvojklik otvorí funkciu. "
                                     "Kolieskom myši približujete, ťahaním posúvate."), cg_bar),
                  tr("Graf volaní"));
    for (auto* g : {cfg_, callgraph_}) {
        connect(g, &GraphView::nodeActivated, this, [this](Addr a) {
            go_to(a);
            tabs_->setCurrentIndex(T_LISTING);
        });
        connect(g, &GraphView::nodeSelected, this, [this](Addr a) {
            current_addr_ = a;
            refresh_details();
        });
    }

    auto table = [this](const char* name) {
        auto* t = new QTableWidget;
        t->setObjectName(name);
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::SingleSelection);
        t->verticalHeader()->hide();
        t->horizontalHeader()->setStretchLastSection(true);
        t->setSortingEnabled(true);
        t->setWordWrap(false);
        connect(t, &QTableWidget::cellClicked, this, [this, t](int r, int) {
            auto* it = t->item(r, 0);
            if (!it || !it->data(Qt::UserRole).isValid()) return;
            current_addr_ = Addr(it->data(Qt::UserRole).toULongLong());
            refresh_details();
        });
        connect(t, &QTableWidget::cellDoubleClicked, this, [this, t](int r, int) {
            auto* it = t->item(r, 0);
            if (!it || !it->data(Qt::UserRole).isValid() || !session_ || !session_->analysis) return;
            Addr a = Addr(it->data(Qt::UserRole).toULongLong());
            go_to(a);
            if (session_->analysis->function_containing(a) || session_->analysis->function(a)) tabs_->setCurrentIndex(T_LISTING);
        });
        return t;
    };
    auto filtered = [this](QTableWidget* t, const QString& hint) {
        auto* f = new QLineEdit;
        f->setPlaceholderText(tr("Filtrovať…"));
        f->setClearButtonEnabled(true);
        connect(f, &QLineEdit::textChanged, t, [t](const QString& text) {
            for (int r = 0; r < t->rowCount(); r++) {
                bool hit = text.isEmpty();
                for (int c = 0; c < t->columnCount() && !hit; c++)
                    hit = t->item(r, c) && t->item(r, c)->text().contains(text, Qt::CaseInsensitive);
                t->setRowHidden(r, !hit);
            }
        });
        return std::make_pair(f, hint);
    };
    strings_ = table("strings");
    imports_ = table("imports");
    exports_ = table("exports");
    sections_ = table("sections");
    bookmarks_ = table("bookmarks");
    search_ = table("search_results");
    {
        auto [f, h] = filtered(strings_, tr("Reťazce sú texty nájdené v súbore. Stĺpec Odkazy ukazuje, koľko miest v kóde "
                                            "ich používa – kliknutím ich nájdete."));
        tabs_->addTab(page(strings_, h, f), tr("Reťazce"));
    }
    {
        auto [f, h] = filtered(imports_, tr("Importy sú funkcie, ktoré program používa z iných knižníc (napr. printf, "
                                            "strcmp). Často prezradia, čo program robí."));
        tabs_->addTab(page(imports_, h, f), tr("Importy"));
    }
    tabs_->addTab(page(exports_, tr("Exporty sú funkcie a dáta, ktoré súbor ponúka iným programom (typické pre knižnice).")),
                  tr("Exporty"));
    hex_ = new QPlainTextEdit;
    hex_->setObjectName("hex");
    hex_->setReadOnly(true);
    hex_->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(sections_);
    split->addWidget(hex_);
    split->setSizes({200, 400});
    connect(sections_, &QTableWidget::cellClicked, this, [this](int r, int) {
        auto* it = sections_->item(r, 0);
        if (!it || !session_) return;
        Addr a = Addr(it->data(Qt::UserRole).toULongLong());
        hex_->setPlainText(hexdump_text(session_->binary, a, 64 * 1024));
    });
    tabs_->addTab(page(split, tr("Sekcie sú časti súboru: kód (.text), dáta (.data, .rodata) a ďalšie. Kliknutím zobrazíte "
                                "ich bajty v hexadecimálnom výpise (prvých 64 KiB). Práva: r = čítanie, w = zápis, "
                                "x = spúšťanie.")),
                  tr("Sekcie a hex"));
    tabs_->addTab(page(bookmarks_, tr("Záložky a komentáre, ktoré ste pridali. Pridáte ich v paneli Detaily alebo klávesmi "
                                     "B a ;.")),
                  tr("Záložky"));
    tabs_->addTab(page(search_, tr("Výsledky posledného hľadania. Dvojklikom skočíte na miesto v kóde.")),
                  tr("Výsledky hľadania"));
    views_combo_ = new QComboBox;
    views_combo_->setObjectName("views_combo");
    views_text_ = new QTextBrowser;
    views_text_->setObjectName("views");
    connect(views_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refresh_views_tab(); });
    tabs_->addTab(page(views_text_, tr("Vlastné pohľady pridané pluginmi alebo skriptami: program.add_view(\"Názov\", funkcia)."),
                       views_combo_),
                  tr("Pohľady"));
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int i) {
        if (i == T_CFG || i == T_CALLS) refresh_graphs();
        if (i == T_VIEWS) refresh_views_tab();
    });

    // status bar
    status_ = new QLabel(tr("Pripravené – otvorte súbor"));
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setMaximumWidth(260);
    progress_->hide();
    cancel_btn_ = new QPushButton(tr("Zrušiť"));
    cancel_btn_->hide();
    connect(cancel_btn_, &QPushButton::clicked, this, &MainWindow::cancel_analysis);
    statusBar()->addWidget(status_, 1);
    statusBar()->addPermanentWidget(progress_);
    statusBar()->addPermanentWidget(cancel_btn_);
}

void MainWindow::apply_settings() {
    apply_theme(settings_.theme == "dark");
    pal_ = Palette::make(settings_.theme == "dark");
    for (auto& c : command_defs_)
        if (auto* a = command(c.id)) a->setShortcut(settings_.key(c));
    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(settings_.font_size);
    for (QWidget* w : std::initializer_list<QWidget*>{listing_, pseudo_, hex_}) w->setFont(mono);
    for (auto* h : hints_) h->setVisible(settings_.hints);
    if (session_ && session_->analysis && !busy()) refresh_all();
}

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    if (!first_show_) return;
    first_show_ = false;
    if (!settings_.onboarding_done) QTimer::singleShot(0, this, [this] { cmd_onboarding(); });
}

// ------------------------------------------------------------ opening and analysis
void MainWindow::open_path(const QString& path) {
    if (busy() || !confirm_discard()) return;
    try {
        status_->setText(tr("Načítavam…"));
        auto s = Session::open(s8(path), args_.options);
        start_session(std::move(s), QFileInfo(path).fileName());
    } catch (const std::exception& e) {
        status_->setText(tr("Chyba"));
        report_error(e);
    }
}

void MainWindow::open_demo() {
    if (busy() || !confirm_discard()) return;
    start_session(cli::open_demo(), "login_x64.elf (demo)");
}

void MainWindow::start_session(std::unique_ptr<Session> s, const QString& label) {
    if (session_ && event_sub_) session_->events.unsubscribe(event_sub_);
    event_sub_ = 0;
    session_ = std::move(s);
    current_func_.reset();
    current_addr_.reset();
    selected_var_.clear();
    history_.clear();
    seen_revision_ = 0;
    if (!args_.no_plugins) session_->plugins = plugins::load(*session_, args_.plugin_dirs);
    event_sub_ = session_->events.subscribe("*", [this](const Event&) {
        QMetaObject::invokeMethod(this, [this] { schedule_refresh(); }, Qt::QueuedConnection);
    });
    cancel_ = false;
    set_busy(true);
    status_->setText(tr("Analyzujem %1 na pozadí…").arg(label));
    Session* sess = session_.get();
    worker_ = std::thread([this, sess] {
        auto done = [this](bool ok, QString err, QString detail) {
            QMetaObject::invokeMethod(this, [this, ok, err, detail] { on_analysis_done(ok, err, detail); }, Qt::QueuedConnection);
        };
        try {
            int last = -1;
            sess->analyze(
                [this, &last](double frac, const std::string& msg) {
                    int pct = int(frac * 100);
                    if (pct == last) return;
                    last = pct;
                    QString m = q(msg);
                    QMetaObject::invokeMethod(this, [this, pct, m] {
                        progress_->setValue(pct);
                        status_->setText(m);
                    }, Qt::QueuedConnection);
                },
                &cancel_);
            done(true, {}, {});
        } catch (const Cancelled&) {
            done(false, tr("Analýza bola zrušená."), {});
        } catch (const ReversaError& e) {
            done(false, q(e.what()), q(e.detail));
        } catch (const std::exception& e) {
            done(false, tr("Neočakávaná chyba pri analýze: %1").arg(q(e.what())), q(e.what()));
        }
    });
}

void MainWindow::cancel_analysis() {
    if (busy()) {
        cancel_ = true;
        status_->setText(tr("Ruším analýzu…"));
    }
}

void MainWindow::on_analysis_done(bool ok, const QString& error, const QString& detail) {
    if (worker_.joinable()) worker_.join();
    set_busy(false);
    if (!ok) {
        if (session_ && event_sub_) session_->events.unsubscribe(event_sub_);
        event_sub_ = 0;
        session_.reset();
        static_cast<QStackedWidget*>(centralWidget())->setCurrentIndex(0);
        status_->setText(error);
        if (!cancel_) show_error(tr("Analýza zlyhala"), error, detail);
        update_actions();
        update_title();
        Q_EMIT analysisFinished(false);
        return;
    }
    static_cast<QStackedWidget*>(centralWidget())->setCurrentIndex(1);
    auto& a = *session_->analysis;
    status_->setText(tr("Hotovo: %1 funkcií, %2 reťazcov, %3 x-refs za %4 s")
                         .arg(a.functions.size())
                         .arg(a.strings.size())
                         .arg(a.xrefs.size())
                         .arg(a.duration, 0, 'f', 2));
    status_->setToolTip(tr("%1\nSHA-256: %2").arg(q(session_->binary.path), q(session_->binary.sha256())));
    seen_revision_ = session_->events.revision;
    std::optional<Addr> start = a.main ? a.main : session_->binary.entry;
    refresh_all();
    if (start) go_to(*start, false);
    tabs_->setCurrentIndex(T_LISTING);
    update_actions();
    update_title();
    Q_EMIT analysisFinished(true);
}

void MainWindow::set_busy(bool b) {
    progress_->setVisible(b);
    cancel_btn_->setVisible(b);
    progress_->setValue(0);
    update_actions();
}

bool MainWindow::confirm_discard() {
    if (!session_ || !session_->dirty) return true;
    int r = ask_unsaved(tr("Máte neuložené zmeny. Chcete ich uložiť do projektu?"));
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return save_project();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirm_discard()) {
        e->ignore();
        return;
    }
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    e->accept();
}

void MainWindow::update_title() {
    QString name = session_ ? q(session_->binary.path) : QString();
    if (session_ && session_->project_path) name = q(*session_->project_path);
    setWindowTitle(name.isEmpty() ? QStringLiteral("REVERSA") : QStringLiteral("REVERSA – %1[*]").arg(QFileInfo(name).fileName()));
    setWindowModified(session_ && session_->dirty);
}

void MainWindow::update_actions() {
    bool ready = session_ && session_->analysis && !busy();
    for (auto id : {"save", "save_as", "rename", "comment", "bookmark", "goto", "back", "report_md", "report_html", "search_hex",
                    "search_insn", "search_code", "diagnostics", "relocations"})
        command(id)->setEnabled(ready);
    command("undo")->setEnabled(ready && session_->undo_stack.can_undo());
    command("redo")->setEnabled(ready && session_->undo_stack.can_redo());
    command("open")->setEnabled(!busy());
    command("demo")->setEnabled(!busy());
    search_box_->setEnabled(ready);
}

// ------------------------------------------------------------ navigation
void MainWindow::go_to(Addr addr, bool record) {
    if (!session_ || !session_->analysis) return;
    auto& a = *session_->analysis;
    if (record && current_addr_ && *current_addr_ != addr) history_.push_back(*current_addr_);
    current_addr_ = addr;
    const Function* fn = a.function(addr);
    if (!fn) fn = a.function_containing(addr);
    if (fn && (!current_func_ || *current_func_ != fn->addr)) {
        current_func_ = fn->addr;
        selected_var_.clear();
    }
    refresh_code();
    refresh_details();
    if (tabs_->currentIndex() == T_CFG || tabs_->currentIndex() == T_CALLS) refresh_graphs();
    if (!fn && session_->binary.is_mapped(addr)) {
        Addr start = addr & ~Addr(15);
        hex_->setPlainText(hexdump_text(session_->binary, start, 4096));
    }
    if (current_func_) {
        for (int i = 0; i < functions_->topLevelItemCount(); i++) {
            auto* it = functions_->topLevelItem(i);
            if (Addr(it->data(0, Qt::UserRole).toULongLong()) == *current_func_) {
                functions_->setCurrentItem(it);
                functions_->scrollToItem(it);
                break;
            }
        }
    }
}

void MainWindow::on_link(const QUrl& url) {
    QString href = url.toString();
    if (href.startsWith("v:")) {
        selected_var_ = s8(href.mid(2));
        refresh_code();
        message(tr("Vybraná premenná – klávesom N ju premenujete."));
        return;
    }
    if (auto a = addr_from_link(href)) {
        bool from_details = sender() == details_;
        go_to(*a);
        if (from_details && tabs_->currentIndex() > T_CALLS && session_->analysis->function_containing(*a))
            tabs_->setCurrentIndex(T_LISTING);
    }
}

void MainWindow::cmd_back() {
    if (history_.empty()) {
        message(tr("História je prázdna."));
        return;
    }
    Addr a = history_.back();
    history_.pop_back();
    go_to(a, false);
}

void MainWindow::cmd_goto() {
    auto r = ask(tr("Skočiť na adresu"), tr("Adresa alebo meno (napr. 0x401000 alebo main):"));
    if (!r || r->trimmed().isEmpty()) return;
    auto a = session_->resolve(s8(r->trimmed()));
    if (!a) a = parse_address(*r);
    if (!a || !session_->binary.is_mapped(*a)) {
        show_error(tr("Nenašiel som"), tr("'%1' nie je meno ani adresa v tomto súbore.").arg(*r), {});
        return;
    }
    go_to(*a);
    if (session_->analysis->function_containing(*a) && tabs_->currentIndex() > T_CALLS) tabs_->setCurrentIndex(T_LISTING);
}

// ------------------------------------------------------------ refreshing
void MainWindow::schedule_refresh() {
    if (refresh_pending_ || busy()) return;
    refresh_pending_ = true;
    QTimer::singleShot(0, this, [this] {
        refresh_pending_ = false;
        if (!session_ || !session_->analysis || busy()) return;
        if (session_->events.revision == seen_revision_) return;
        seen_revision_ = session_->events.revision;
        refresh_all();
    });
}

void MainWindow::refresh_all() {
    refresh_functions();
    refresh_code();
    refresh_details();
    refresh_tables();
    if (tabs_->currentIndex() == T_CFG || tabs_->currentIndex() == T_CALLS) refresh_graphs();
    refresh_views_tab();
    update_actions();
    update_title();
}

void MainWindow::refresh_functions() {
    auto& s = *session_;
    auto& a = *s.analysis;
    functions_->setSortingEnabled(false);
    functions_->clear();
    QColor dim = palette().color(QPalette::Disabled, QPalette::Text);
    QList<QTreeWidgetItem*> items;
    for (auto& [addr, fn] : a.functions) {
        auto* it = new QTreeWidgetItem;
        QString name = q(s.name_of(addr));
        if (a.main && *a.main == addr) name = QStringLiteral("★ ") + name;
        it->setText(0, name);
        it->setText(1, hexq(addr));
        it->setData(2, Qt::DisplayRole, int(a.callers(addr).size()));
        it->setData(3, Qt::DisplayRole, qulonglong(fn.size));
        it->setData(0, Qt::UserRole, qulonglong(addr));
        if (fn.kind != "normal") {
            for (int c = 0; c < 4; c++) it->setForeground(c, dim);
            it->setToolTip(0, tr("Importovaná funkcia%1").arg(fn.library.empty() ? QString() : " z " + q(fn.library)));
        }
        items << it;
    }
    functions_->addTopLevelItems(items);
    functions_->setSortingEnabled(true);
    functions_->sortByColumn(1, Qt::AscendingOrder);
    if (!func_filter_->text().isEmpty()) Q_EMIT func_filter_->textChanged(func_filter_->text());
    if (current_func_) go_to(*current_addr_, false);
}

void MainWindow::refresh_code() {
    if (!session_ || !session_->analysis) return;
    if (!current_func_) {
        listing_->setHtml(tr("<p>Vyberte funkciu vľavo.</p>"));
        pseudo_->setHtml(tr("<p>Vyberte funkciu vľavo.</p>"));
        return;
    }
    int lv = listing_->verticalScrollBar()->value(), pv = pseudo_->verticalScrollBar()->value();
    static std::optional<Addr> last_func;  // keep the scroll position while re-rendering the same function
    bool same = last_func == current_func_;
    last_func = current_func_;
    try {
        listing_->setHtml(listing_html(*session_, *current_func_, pal_, current_addr_));
        auto* fn = session_->analysis->function(*current_func_);
        if (fn && fn->kind == "normal") pseudo_->setHtml(pseudocode_html(*session_, *current_func_, pal_, selected_var_));
        else pseudo_->setHtml(tr("<p>%1 je importovaná funkcia – jej kód je v inej knižnici, pseudokód sa preto nedá zobraziť.</p>")
                                  .arg(q(session_->name_of(*current_func_)).toHtmlEscaped()));
    } catch (const std::exception& e) {
        pseudo_->setHtml(tr("<p>Pseudokód sa nepodarilo vytvoriť: %1</p>").arg(q(e.what()).toHtmlEscaped()));
    }
    if (same) {
        listing_->verticalScrollBar()->setValue(lv);
        pseudo_->verticalScrollBar()->setValue(pv);
    }
    if (current_addr_ && *current_addr_ != *current_func_) listing_->scrollToAnchor(hexq(*current_addr_));
    else if (!same) listing_->verticalScrollBar()->setValue(0);
}

void MainWindow::refresh_details() {
    if (!session_ || !session_->analysis || !current_addr_) return;
    details_->setHtml(details_html(*session_, *current_addr_, pal_));
}

void MainWindow::fill_table(QTableWidget* t, const QStringList& headers, const std::vector<QStringList>& rows,
                            const std::vector<Addr>& addrs) {
    t->setSortingEnabled(false);
    t->clear();
    t->setColumnCount(int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->setRowCount(int(rows.size()));
    for (int r = 0; r < int(rows.size()); r++) {
        for (int c = 0; c < int(headers.size()) && c < rows[size_t(r)].size(); c++) {
            auto* it = new QTableWidgetItem;
            bool ok = false;
            int n = rows[size_t(r)][c].toInt(&ok);
            if (ok && c > 0) it->setData(Qt::DisplayRole, n);  // numeric columns sort as numbers
            else it->setText(rows[size_t(r)][c]);
            if (c == 0 && size_t(r) < addrs.size()) it->setData(Qt::UserRole, qulonglong(addrs[size_t(r)]));
            t->setItem(r, c, it);
        }
    }
    t->setSortingEnabled(true);
    t->resizeColumnsToContents();
    t->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::refresh_tables() {
    auto& s = *session_;
    auto& a = *s.analysis;
    auto& b = s.binary;
    std::vector<QStringList> rows;
    std::vector<Addr> addrs;
    for (auto& st : a.strings) {
        QString text = q(utf8_prefix(st.text, 300));
        rows.push_back({hexq(st.addr), text, q(st.encoding), QString::number(a.xrefs.count_to(st.addr))});
        addrs.push_back(st.addr);
    }
    fill_table(strings_, {tr("Adresa"), tr("Text"), tr("Kódovanie"), tr("Odkazy")}, rows, addrs);
    // put the text column last so it stretches
    strings_->horizontalHeader()->moveSection(1, 3);

    rows.clear();
    addrs.clear();
    for (auto& im : b.imports) {
        auto it = a.import_addr.find(im.name);
        Addr at = it != a.import_addr.end() ? it->second : (im.thunk ? *im.thunk : im.slot);
        rows.push_back({q(im.name), q(im.library), hexq(im.thunk ? *im.thunk : im.slot), QString::number(a.callers(at).size())});
        addrs.push_back(at);
    }
    fill_table(imports_, {tr("Meno"), tr("Knižnica"), tr("PLT/IAT"), tr("Volaní")}, rows, addrs);

    rows.clear();
    addrs.clear();
    for (auto& ex : b.exports) {
        rows.push_back({q(ex.name), hexq(ex.addr), ex.ordinal ? QString::number(*ex.ordinal) : QString(), q(ex.forwarder)});
        addrs.push_back(ex.addr);
    }
    fill_table(exports_, {tr("Meno"), tr("Adresa"), tr("Ordinál"), tr("Presmerovanie")}, rows, addrs);
    if (b.exports.empty()) {
        exports_->setRowCount(1);
        exports_->setItem(0, 0, new QTableWidgetItem(tr("Tento súbor nič neexportuje (bežné pri spustiteľných programoch).")));
    }

    rows.clear();
    addrs.clear();
    for (auto& sec : b.sections) {
        rows.push_back({q(sec.name), hexq(sec.addr), hexq(sec.size), hexq(sec.offset), q(sec.perms()), q(sec.kind())});
        addrs.push_back(sec.addr);
    }
    fill_table(sections_, {tr("Meno"), tr("Adresa"), tr("Veľkosť"), tr("Offset"), tr("Práva"), tr("Druh")}, rows, addrs);

    rows.clear();
    addrs.clear();
    auto func_name = [&](Addr ad) {
        auto* f = a.function_containing(ad);
        return f ? q(s.name_of(f->addr)) : QString();
    };
    for (auto& [ad, label] : s.annotations.bookmarks) {
        rows.push_back({hexq(ad), tr("záložka"), q(label), func_name(ad)});
        addrs.push_back(ad);
    }
    for (auto& [ad, text] : s.annotations.comments) {
        rows.push_back({hexq(ad), tr("komentár"), q(text), func_name(ad)});
        addrs.push_back(ad);
    }
    fill_table(bookmarks_, {tr("Adresa"), tr("Typ"), tr("Text"), tr("Funkcia")}, rows, addrs);
    if (rows.empty()) {
        bookmarks_->setRowCount(1);
        bookmarks_->setItem(0, 0, new QTableWidgetItem(tr("Zatiaľ žiadne záložky ani komentáre.")));
    }
}

void MainWindow::refresh_graphs() {
    if (!session_ || !session_->analysis) return;
    auto& s = *session_;
    auto& a = *s.analysis;
    int tab = tabs_->currentIndex();
    if (tab == T_CFG) {
        const Function* fn = current_func_ ? a.function(*current_func_) : nullptr;
        if (!fn || fn->kind != "normal") {
            cfg_->set_message(tr("Graf toku riadenia (CFG) sa dá zobraziť len pre funkciu s kódom v tomto súbore. Vyberte funkciu vľavo."),
                              pal_.dim);
            return;
        }
        Cfg g = build_cfg(*fn);
        std::set<Addr> in_loop;
        for (auto& [h, body] : g.loops) in_loop.insert(body.begin(), body.end());
        std::vector<GraphNode> nodes;
        for (size_t i = 0; i < g.nodes.size() && i < 3000; i++) {
            Addr n = g.nodes[i];
            auto& blk = fn->blocks.at(n);
            QString text = n == fn->addr ? q(s.name_of(n)) + ":" : QStringLiteral("loc_%1:").arg(q(hex_digits(n)));
            size_t shown = 0;
            for (Addr ad : blk.insns) {
                if (++shown > 40) {
                    text += tr("\n… ďalších %1 inštrukcií").arg(blk.insns.size() - 40);
                    break;
                }
                const Insn* ins = a.insn(ad);
                if (!ins) continue;
                QString line = q(ins->text());
                if (ins->flow == Flow::Call || ins->flow == Flow::ICall || ins->flow == Flow::IJmp) {
                    for (auto& x : a.xrefs.frm(ad))
                        if (x.kind == "call" || x.kind == "jump") line = q(ins->mnemonic) + " " + q(s.name_of(x.to));
                }
                for (auto& r : ins->refs)
                    if (auto t = s.string_text(r.addr)) {
                        line += QStringLiteral("   ; \"%1\"").arg(q(utf8_prefix(*t, 30)));
                        break;
                    }
                text += "\n" + line;
            }
            GraphNode gn{n, text, pal_.dim, pal_.dark ? "#252526" : "#ffffff", n == fn->addr};
            if (g.loops.count(n)) gn.border = "#e8912d";
            else if (n == g.entry) gn.border = pal_.link;
            else if (blk.succs.empty()) gn.border = "#c94f4f";
            if (in_loop.count(n)) gn.fill = pal_.dark ? "#33291a" : "#fff6e8";
            nodes.push_back(gn);
        }
        std::vector<GraphEdge> edges;
        for (auto& e : g.edges)
            edges.push_back({e.from, e.to, e.kind == "true" ? "#3fa34d" : e.kind == "false" ? "#d9534f" : QString(), e.back});
        Addr focus = fn->addr;
        if (current_addr_)
            for (auto& [st, blk] : fn->blocks)
                if (*current_addr_ >= blk.start && *current_addr_ < blk.end) focus = st;
        cfg_->set_graph(nodes, edges, focus, pal_.dim, pal_.text);
    } else if (tab == T_CALLS) {
        std::map<Addr, Function> funcs;
        for (auto& [ad, fn] : a.functions)
            if (cg_imports_->isChecked() || fn.kind == "normal") funcs.emplace(ad, fn);
        std::optional<Addr> root = current_func_;
        if (root && !funcs.count(*root)) root.reset();
        CallGraph g = call_graph(funcs, root, root ? cg_depth_->value() : 0);
        constexpr size_t LIMIT = 600;
        std::set<Addr> keep(g.nodes.begin(), g.nodes.begin() + long(std::min(g.nodes.size(), LIMIT)));
        std::vector<GraphNode> nodes;
        for (Addr n : g.nodes) {
            if (!keep.count(n)) continue;
            bool imp = a.functions.at(n).kind != "normal";
            GraphNode gn{n, q(s.name_of(n)), imp ? pal_.dim : pal_.link, pal_.dark ? (imp ? "#2a2a2a" : "#1e2a36") : (imp ? "#f2f2f2" : "#eef5ff"),
                         (a.main && *a.main == n) || (root && *root == n)};
            nodes.push_back(gn);
        }
        std::vector<GraphEdge> edges;
        for (auto& [x, y] : g.edges)
            if (keep.count(x) && keep.count(y)) edges.push_back({x, y, {}, false});
        callgraph_->set_graph(nodes, edges, root.value_or(0), pal_.dim, pal_.text);
        if (g.nodes.size() > LIMIT) message(tr("Graf volaní je veľký – zobrazených prvých %1 z %2 funkcií. Vyberte funkciu a zmenšite hĺbku.")
                                                .arg(LIMIT).arg(g.nodes.size()), 8000);
    }
}

void MainWindow::refresh_views_tab() {
    if (!session_ || !session_->analysis) return;
    QStringList names;
    for (auto& [n, f] : session_->views) names << q(n);
    QStringList current;
    for (int i = 0; i < views_combo_->count(); i++) current << views_combo_->itemText(i);
    if (names != current) {
        QSignalBlocker block(views_combo_);
        views_combo_->clear();
        views_combo_->addItems(names);
    }
    if (tabs_->currentIndex() != T_VIEWS) return;
    if (names.isEmpty()) {
        views_text_->setMarkdown(tr("Žiadne vlastné pohľady. Pridáte ich skriptom alebo pluginom: `program.add_view(\"Názov\", funkcia)`."));
        return;
    }
    int i = std::max(0, views_combo_->currentIndex());
    try {
        views_text_->setMarkdown(q(session_->views[size_t(i)].second()));
    } catch (const std::exception& e) {
        views_text_->setPlainText(tr("Pohľad zlyhal: %1").arg(q(e.what())));
    }
}

// ------------------------------------------------------------ edits
std::optional<QString> MainWindow::ask(const QString& title, const QString& label, const QString& value) {
    return prompt(title, label, value);
}

void MainWindow::message(const QString& text, int ms) { statusBar()->showMessage(text, ms); }

void MainWindow::report_error(const std::exception& e) {
    QString detail;
    if (auto* re = dynamic_cast<const ReversaError*>(&e)) detail = q(re->detail);
    show_error(tr("REVERSA"), q(e.what()), detail);
}

void MainWindow::cmd_rename() {
    if (!session_ || !session_->analysis) return;
    try {
        if (tabs_->currentIndex() == T_PSEUDO && !selected_var_.empty() && current_func_) {
            QString old;
            for (auto& ln : session_->decompile(*current_func_))
                for (auto& seg : ln.segs)
                    if (seg.kind == "var" && seg.key == selected_var_) old = q(seg.text);
            auto r = ask(tr("Premenovať premennú"), tr("Nové meno premennej %1 (prázdne = pôvodné meno):").arg(old), old);
            if (!r) return;
            session_->rename_var(*current_func_, selected_var_, s8(r->trimmed()));
            message(tr("Premenná premenovaná."));
            return;
        }
        auto target = current_addr_ ? current_addr_ : current_func_;
        if (!target) {
            message(tr("Najprv vyberte funkciu alebo adresu."));
            return;
        }
        // In the listing, the selected instruction belongs to a function: rename the function itself
        // unless the address carries its own label (block start, string, data).
        auto& a = *session_->analysis;
        if (!a.function(*target) && !a.string(*target) && current_func_ && a.function_containing(*target)) {
            auto* fn = a.function_containing(*target);
            if (!fn->blocks.count(*target)) target = fn->addr;
        }
        QString old = q(session_->name_of(*target));
        auto r = ask(tr("Premenovať"), tr("Nové meno pre %1 (prázdne = pôvodné meno):").arg(old), old);
        if (!r) return;
        session_->rename(*target, s8(r->trimmed()));
        message(tr("Premenované."));
    } catch (const std::exception& e) {
        report_error(e);
    }
}

void MainWindow::cmd_comment() {
    if (!session_ || !session_->analysis) return;
    auto target = current_addr_ ? current_addr_ : current_func_;
    if (!target) return message(tr("Najprv vyberte funkciu alebo adresu."));
    auto it = session_->annotations.comments.find(*target);
    auto r = ask(tr("Komentár"), tr("Komentár k adrese %1 (prázdny = zmazať):").arg(hexq(*target)),
                 it != session_->annotations.comments.end() ? q(it->second) : QString());
    if (!r) return;
    try {
        session_->set_comment(*target, s8(*r));
    } catch (const std::exception& e) {
        report_error(e);
    }
}

void MainWindow::cmd_bookmark() {
    if (!session_ || !session_->analysis) return;
    auto target = current_addr_ ? current_addr_ : current_func_;
    if (!target) return message(tr("Najprv vyberte funkciu alebo adresu."));
    auto it = session_->annotations.bookmarks.find(*target);
    auto r = ask(tr("Záložka"), tr("Názov záložky pre %1 (prázdny = zmazať):").arg(hexq(*target)),
                 it != session_->annotations.bookmarks.end() ? q(it->second) : q(session_->name_of(*target)));
    if (!r) return;
    try {
        session_->set_bookmark(*target, s8(*r));
    } catch (const std::exception& e) {
        report_error(e);
    }
}

void MainWindow::cmd_undo() {
    if (!session_) return;
    auto d = session_->undo();
    message(d ? tr("Vrátené: %1").arg(q(*d)) : tr("Nie je čo vrátiť."));
    update_actions();
}

void MainWindow::cmd_redo() {
    if (!session_) return;
    auto d = session_->redo();
    message(d ? tr("Zopakované: %1").arg(q(*d)) : tr("Nie je čo zopakovať."));
    update_actions();
}

// ------------------------------------------------------------ files
void MainWindow::cmd_open() {
    QString path = QFileDialog::getOpenFileName(this, tr("Otvoriť binárny súbor alebo projekt"), {},
                                                tr("Všetky súbory (*);;Projekty REVERSA (*.rvproj)"));
    if (!path.isEmpty()) open_path(path);
}

bool MainWindow::save_project(const QString& path) {
    if (!session_ || !session_->analysis) return false;
    if (path.isEmpty() && !session_->project_path) {
        cmd_save_as();
        return !session_->dirty;
    }
    try {
        std::string out = session_->save(s8(path));
        message(tr("Projekt uložený: %1").arg(q(out)), 8000);
        update_title();
        return true;
    } catch (const std::exception& e) {
        report_error(e);
        return false;
    }
}

void MainWindow::cmd_save_as() {
    if (!session_) return;
    QString def = q(session_->project_path ? *session_->project_path : default_project_path(session_->binary.path));
    QString path = QFileDialog::getSaveFileName(this, tr("Uložiť projekt"), def, tr("Projekty REVERSA (*.rvproj)"));
    if (!path.isEmpty()) save_project(path);
}

void MainWindow::cmd_report(const QString& fmt) {
    if (!session_ || !session_->analysis) return;
    QString base = QFileInfo(q(session_->binary.path)).completeBaseName();
    QString path = QFileDialog::getSaveFileName(this, tr("Exportovať report"), base + "-report." + fmt,
                                                fmt == "html" ? tr("HTML (*.html)") : tr("Markdown (*.md)"));
    if (path.isEmpty()) return;
    try {
        std::string text = build_report(*session_, s8(fmt));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(text.data(), qint64(text.size())) != qint64(text.size()))
            throw ReversaError(s8(tr("Súbor '%1' sa nepodarilo zapísať.").arg(path)), s8(f.errorString()));
        message(tr("Report uložený: %1").arg(path), 8000);
    } catch (const std::exception& e) {
        report_error(e);
    }
}

// ------------------------------------------------------------ search
void MainWindow::run_search(const QString& text) {
    if (!session_ || !session_->analysis || text.trimmed().isEmpty()) return;
    auto& s = *session_;
    auto& a = *s.analysis;
    std::string query = s8(text.trimmed());
    std::vector<QStringList> rows;
    std::vector<Addr> addrs;
    auto resolved = s.resolve(query);
    if (!resolved) resolved = parse_address(text);
    if (resolved && s.binary.is_mapped(*resolved)) {
        rows.push_back({hexq(*resolved), tr("adresa / meno"), q(s.name_of(*resolved))});
        addrs.push_back(*resolved);
    }
    QString needle = text.trimmed();
    for (auto& [ad, fn] : a.functions) {
        QString n = q(s.name_of(ad));
        if (n.contains(needle, Qt::CaseInsensitive) && !(resolved && *resolved == ad)) {
            rows.push_back({hexq(ad), fn.kind == "normal" ? tr("funkcia") : tr("import"), n});
            addrs.push_back(ad);
        }
    }
    try {
        for (auto& [ad, t] : s.search_strings(query)) {
            rows.push_back({hexq(ad), tr("reťazec"), q(utf8_prefix(t, 200))});
            addrs.push_back(ad);
        }
    } catch (const std::exception&) {
    }
    fill_table(search_, {tr("Adresa"), tr("Typ"), tr("Text")}, rows, addrs);
    tabs_->setCurrentIndex(T_SEARCH);
    message(tr("Nájdených %1 výsledkov pre „%2“.").arg(rows.size()).arg(needle));
    if (resolved && s.binary.is_mapped(*resolved)) go_to(*resolved);
}

void MainWindow::cmd_advanced_search(const QString& kind) {
    if (!session_ || !session_->analysis) return;
    QString title = kind == "hex" ? tr("Hľadať bajty") : kind == "insn" ? tr("Hľadať inštrukcie") : tr("Hľadať v pseudokóde");
    QString label = kind == "hex" ? tr("Hex vzor, ?? = ľubovoľný bajt (napr. 48 8b ?? 05):")
                                  : tr("Regulárny výraz (napr. call\\s+0x[0-9a-f]+):");
    auto r = ask(title, label);
    if (!r || r->trimmed().isEmpty()) return;
    std::vector<QStringList> rows;
    std::vector<Addr> addrs;
    try {
        auto& a = *session_->analysis;
        auto fname = [&](Addr ad) {
            auto* f = a.function_containing(ad);
            return f ? q(session_->name_of(f->addr)) : QString();
        };
        if (kind == "hex") {
            for (Addr ad : session_->search_bytes(s8(*r))) {
                rows.push_back({hexq(ad), tr("bajty"), fname(ad)});
                addrs.push_back(ad);
            }
        } else {
            auto hits = kind == "insn" ? session_->search_instructions(s8(*r)) : session_->search_pseudocode(s8(*r));
            for (auto& [ad, t] : hits) {
                rows.push_back({hexq(ad), fname(ad), q(t)});
                addrs.push_back(ad);
            }
        }
    } catch (const std::exception& e) {
        report_error(e);
        return;
    }
    fill_table(search_, {tr("Adresa"), kind == "hex" ? tr("Typ") : tr("Funkcia"), kind == "hex" ? tr("Funkcia") : tr("Text")}, rows, addrs);
    tabs_->setCurrentIndex(T_SEARCH);
    message(tr("Nájdených %1 výsledkov.").arg(rows.size()));
}

// ------------------------------------------------------------ dialogs
void MainWindow::cmd_settings() {
    SettingsDialog d(settings_, command_defs_, this);
    if (d.exec() != QDialog::Accepted) return;
    QString old_language = current_language();
    settings_ = d.result();
    settings_.save();
    apply_settings();
    if (settings_.language != old_language) {
        set_language(settings_.language);  // new dialogs use it at once; menus and panels after a restart
        QMessageBox::information(this, tr("Jazyk"), tr("Jazyk sa úplne zmení po reštarte REVERSA."));
    }
    if (d.reshow_onboarding()) cmd_onboarding();
}

void MainWindow::cmd_onboarding() {
    OnboardingDialog d(this);
    d.exec();
    settings_.onboarding_done = true;
    settings_.save();
}

void MainWindow::cmd_diagnostics() {
    if (!session_) return;
    std::vector<QStringList> rows;
    for (auto& d : session_->diagnostics())
        rows.push_back({q(d.severity), q(d.message), d.location ? hexq(*d.location) + " (" + q(d.where) + ")" : QString(), q(d.detail)});
    show_table(this, tr("Diagnostika načítania"),
               rows.empty() ? tr("Súbor sa načítal bez problémov.")
                            : tr("Problémy, ktoré sa našli pri načítaní a analýze. Žiadny z nich nie je fatálny – REVERSA "
                                 "zobrazí všetko, čo sa dalo prečítať."),
               {tr("Závažnosť"), tr("Správa"), tr("Miesto"), tr("Detail pre správcu")}, rows);
}

void MainWindow::cmd_relocations() {
    if (!session_) return;
    std::vector<QStringList> rows;
    for (auto& r : session_->binary.relocations)
        rows.push_back({hexq(r.addr), q(r.type), q(r.symbol), QString::number(r.addend)});
    show_table(this, tr("Relokácie"), tr("Miesta, ktoré zavádzač operačného systému upraví pri spustení programu."),
               {tr("Adresa"), tr("Typ"), tr("Symbol"), tr("Addend")}, rows);
}

void MainWindow::cmd_plugins() {
    std::vector<QStringList> rows;
    QString intro;
    if (args_.no_plugins) intro = tr("Pluginy sú vypnuté (--no-plugins).");
    else {
        intro = tr("Pluginy sú knižnice .so/.dll v C alebo C++. Načítavajú sa z priečinka %1, z premennej "
                   "REVERSA_PLUGIN_PATH a z volieb --plugins. Bežia priamo v REVERSA – inštalujte len pluginy, "
                   "ktorým dôverujete.").arg(q(plugins::default_dirs().front()));
        if (session_ && session_->plugins) {
            Json info = plugins::info(session_->plugins);
            for (auto& p : info.as_array()) {
                QStringList hooks;
                for (auto& h : p.get("hooks").as_array()) hooks << q(h.as_string());
                rows.push_back({q(p.get("name").as_string()), q(p.get("version").as_string()), q(p.get("status").as_string()),
                                hooks.join(", "), q(p.get("error").as_string()), q(p.get("description").as_string())});
            }
        }
    }
    show_table(this, tr("Pluginy"), intro, {tr("Meno"), tr("Verzia"), tr("Stav"), tr("Háčiky"), tr("Chyba"), tr("Popis")}, rows);
}

}  // namespace rv::gui
