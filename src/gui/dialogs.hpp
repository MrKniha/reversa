// Dialogs: first-run guide, settings (theme, font size, custom shortcuts), table viewer.
#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <vector>

class QKeySequenceEdit;
class QLabel;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QStackedWidget;
class QPushButton;

namespace rv::gui {

// One user-visible command whose shortcut can be changed in the settings.
struct CommandDef {
    QString id, text;
    QKeySequence default_key;
};

struct UiSettings {
    QString theme = "light";  // light | dark
    int font_size = 10;
    bool hints = true;        // beginner explanations above the panels
    bool onboarding_done = false;
    std::vector<std::pair<QString, QKeySequence>> shortcuts;  // id -> key (only changed ones)

    static UiSettings load(bool system_dark);
    void save() const;
    QKeySequence key(const CommandDef& c) const;
};

class OnboardingDialog : public QDialog {
    Q_OBJECT
public:
    explicit OnboardingDialog(QWidget* parent = nullptr);
    int page() const;
    int pages() const;
    void next();

private:
    void show_page(int i);
    QStackedWidget* stack_;
    QLabel* counter_;
    QPushButton *back_, *next_;
};

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const UiSettings& current, const std::vector<CommandDef>& commands, QWidget* parent = nullptr);
    UiSettings result() const;
    bool reshow_onboarding() const;

private:
    const std::vector<CommandDef>& commands_;
    UiSettings base_;
    QComboBox* theme_;
    QSpinBox* font_;
    QCheckBox* hints_;
    QCheckBox* onboarding_;
    std::vector<QKeySequenceEdit*> edits_;
};

// Read-only table with a filter box and a "copy" button (diagnostics, plugins, relocations).
void show_table(QWidget* parent, const QString& title, const QString& intro, const QStringList& headers,
                const std::vector<QStringList>& rows);

}  // namespace rv::gui
