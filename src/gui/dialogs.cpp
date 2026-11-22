#include "gui/dialogs.hpp"

#include <algorithm>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace rv::gui {

// ------------------------------------------------------------ settings storage
UiSettings UiSettings::load(bool system_dark) {
    QSettings st;
    UiSettings s;
    s.theme = st.value("ui/theme", system_dark ? "dark" : "light").toString();
    s.font_size = st.value("ui/font_size", 10).toInt();
    s.hints = st.value("ui/hints", true).toBool();
    s.onboarding_done = st.value("ui/onboarding_done", false).toBool();
    st.beginGroup("shortcuts");
    for (auto& k : st.childKeys()) s.shortcuts.push_back({k, QKeySequence(st.value(k).toString(), QKeySequence::PortableText)});
    st.endGroup();
    return s;
}

void UiSettings::save() const {
    QSettings st;
    st.setValue("ui/theme", theme);
    st.setValue("ui/font_size", font_size);
    st.setValue("ui/hints", hints);
    st.setValue("ui/onboarding_done", onboarding_done);
    st.remove("shortcuts");
    st.beginGroup("shortcuts");
    for (auto& [id, key] : shortcuts) st.setValue(id, key.toString(QKeySequence::PortableText));
    st.endGroup();
}

QKeySequence UiSettings::key(const CommandDef& c) const {
    for (auto& [id, k] : shortcuts)
        if (id == c.id) return k;
    return c.default_key;
}

// ------------------------------------------------------------ onboarding
namespace {
struct Step { const char* title; const char* text; };
const Step STEPS[] = {
    {"Vitajte v REVERSA",
     "REVERSA vám pomôže pochopiť, čo robí program, aj keď nemáte jeho zdrojový kód. Ukážeme vám to v 5 krokoch."},
    {"1. Otvorte súbor",
     "Tlačidlom „Otvoriť súbor“ vyberte program (ELF z Linuxu, PE/.exe z Windows alebo surové dáta). Ak nemáte čo "
     "otvoriť, použite skúšobnú binárku – je to malá CTF úloha s heslom. Analýza beží na pozadí a dá sa zrušiť."},
    {"2. Ľavý panel: funkcie",
     "Program sa skladá z funkcií. Hlavná funkcia (main) je označená hviezdičkou. Kliknutím funkciu otvoríte, "
     "filtrom nad zoznamom ju rýchlo nájdete."},
    {"3. Stred: listing, pseudokód a grafy",
     "Listing ukazuje inštrukcie procesora, Pseudokód ich prepis do čitateľného kódu podobného C a grafy ukazujú "
     "štruktúru funkcie a volania. Prepínate ich kartami alebo klávesmi 1 až 4. Modré mená sú klikateľné."},
    {"4. Pravý panel: detaily a x-refs",
     "Tu vidíte, kto funkciu alebo reťazec používa (cross-referencie). Funkciu môžete premenovať (N), pridať "
     "komentár (;) alebo záložku (B). Všetko sa dá vrátiť (Ctrl+Z) a uložiť do projektu (Ctrl+S)."},
};
}  // namespace

OnboardingDialog::OnboardingDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Úvodný sprievodca"));
    setMinimumWidth(480);
    auto* lay = new QVBoxLayout(this);
    stack_ = new QStackedWidget(this);
    for (auto& s : STEPS) {
        auto* w = new QWidget;
        auto* l = new QVBoxLayout(w);
        auto* t = new QLabel(QStringLiteral("<h2>%1</h2>").arg(QString::fromUtf8(s.title)));
        auto* x = new QLabel(QString::fromUtf8(s.text));
        x->setWordWrap(true);
        l->addWidget(t);
        l->addWidget(x);
        l->addStretch();
        stack_->addWidget(w);
    }
    lay->addWidget(stack_);
    auto* row = new QHBoxLayout;
    counter_ = new QLabel;
    auto* skip = new QPushButton(tr("Preskočiť"));
    back_ = new QPushButton(tr("Späť"));
    next_ = new QPushButton(tr("Ďalej"));
    next_->setDefault(true);
    row->addWidget(counter_);
    row->addStretch();
    row->addWidget(skip);
    row->addWidget(back_);
    row->addWidget(next_);
    lay->addLayout(row);
    connect(skip, &QPushButton::clicked, this, &QDialog::accept);
    connect(back_, &QPushButton::clicked, this, [this] { show_page(page() - 1); });
    connect(next_, &QPushButton::clicked, this, &OnboardingDialog::next);
    show_page(0);
}

int OnboardingDialog::page() const { return stack_->currentIndex(); }
int OnboardingDialog::pages() const { return stack_->count(); }

void OnboardingDialog::next() {
    if (page() + 1 >= pages()) accept();
    else show_page(page() + 1);
}

void OnboardingDialog::show_page(int i) {
    i = std::clamp(i, 0, pages() - 1);
    stack_->setCurrentIndex(i);
    counter_->setText(QStringLiteral("%1 / %2").arg(i + 1).arg(pages()));
    back_->setEnabled(i > 0);
    next_->setText(i + 1 == pages() ? tr("Začať") : tr("Ďalej"));
}

// ------------------------------------------------------------ settings dialog
SettingsDialog::SettingsDialog(const UiSettings& current, const std::vector<CommandDef>& commands, QWidget* parent)
    : QDialog(parent), commands_(commands), base_(current) {
    setWindowTitle(tr("Nastavenia"));
    resize(560, 620);
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    theme_ = new QComboBox;
    theme_->addItem(tr("Svetlý"), "light");
    theme_->addItem(tr("Tmavý"), "dark");
    theme_->setCurrentIndex(current.theme == "dark" ? 1 : 0);
    font_ = new QSpinBox;
    font_->setRange(7, 24);
    font_->setValue(current.font_size);
    hints_ = new QCheckBox(tr("Zobrazovať vysvetlivky nad panelmi (pre začiatočníkov)"));
    hints_->setChecked(current.hints);
    onboarding_ = new QCheckBox(tr("Znova ukázať úvodného sprievodcu"));
    form->addRow(tr("Motív:"), theme_);
    form->addRow(tr("Veľkosť písma kódu:"), font_);
    form->addRow(hints_);
    form->addRow(onboarding_);
    lay->addLayout(form);

    lay->addWidget(new QLabel(tr("Klávesové skratky (kliknite do poľa a stlačte novú kombináciu):")));
    auto* table = new QTableWidget(int(commands.size()), 2);
    table->setHorizontalHeaderLabels({tr("Príkaz"), tr("Skratka")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    for (size_t i = 0; i < commands.size(); i++) {
        auto* name = new QTableWidgetItem(commands[i].text);
        name->setFlags(Qt::ItemIsEnabled);
        table->setItem(int(i), 0, name);
        auto* e = new QKeySequenceEdit(current.key(commands[i]));
        e->setObjectName("shortcut_" + commands[i].id);
        table->setCellWidget(int(i), 1, e);
        edits_.push_back(e);
    }
    lay->addWidget(table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText(tr("Obnoviť predvolené"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
        for (size_t i = 0; i < edits_.size(); i++) edits_[i]->setKeySequence(commands_[i].default_key);
        font_->setValue(10);
        hints_->setChecked(true);
    });
    lay->addWidget(buttons);
}

UiSettings SettingsDialog::result() const {
    UiSettings s = base_;
    s.theme = theme_->currentData().toString();
    s.font_size = font_->value();
    s.hints = hints_->isChecked();
    if (onboarding_->isChecked()) s.onboarding_done = false;
    s.shortcuts.clear();
    for (size_t i = 0; i < edits_.size(); i++)
        if (edits_[i]->keySequence() != commands_[i].default_key) s.shortcuts.push_back({commands_[i].id, edits_[i]->keySequence()});
    return s;
}

bool SettingsDialog::reshow_onboarding() const { return onboarding_->isChecked(); }

// ------------------------------------------------------------ table viewer
void show_table(QWidget* parent, const QString& title, const QString& intro, const QStringList& headers,
                const std::vector<QStringList>& rows) {
    QDialog d(parent);
    d.setWindowTitle(title);
    d.resize(820, 480);
    auto* lay = new QVBoxLayout(&d);
    if (!intro.isEmpty()) {
        auto* l = new QLabel(intro);
        l->setWordWrap(true);
        lay->addWidget(l);
    }
    auto* filter = new QLineEdit;
    filter->setPlaceholderText(QObject::tr("Filtrovať…"));
    lay->addWidget(filter);
    auto* t = new QTableWidget(int(rows.size()), int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setStretchLastSection(true);
    for (int r = 0; r < int(rows.size()); r++)
        for (int c = 0; c < int(headers.size()) && c < rows[size_t(r)].size(); c++)
            t->setItem(r, c, new QTableWidgetItem(rows[size_t(r)][c]));
    t->resizeColumnsToContents();
    lay->addWidget(t, 1);
    QObject::connect(filter, &QLineEdit::textChanged, t, [t](const QString& f) {
        for (int r = 0; r < t->rowCount(); r++) {
            bool hit = f.isEmpty();
            for (int c = 0; c < t->columnCount() && !hit; c++)
                hit = t->item(r, c) && t->item(r, c)->text().contains(f, Qt::CaseInsensitive);
            t->setRowHidden(r, !hit);
        }
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* copy = buttons->addButton(QObject::tr("Skopírovať všetko"), QDialogButtonBox::ActionRole);
    QObject::connect(copy, &QPushButton::clicked, &d, [rows, headers] {
        QStringList lines{headers.join('\t')};
        for (auto& r : rows) lines << r.join('\t');
        QApplication::clipboard()->setText(lines.join('\n'));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    lay->addWidget(buttons);
    d.exec();
}

}  // namespace rv::gui
