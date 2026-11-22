#include "gui/i18n.hpp"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <memory>

namespace rv::gui {

namespace {

// Looks strings up by source text only (context-free), so tr() in any class and T() share one table.
class JsonTranslator : public QTranslator {
public:
    bool load_json(const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        auto obj = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = obj.begin(); it != obj.end(); ++it) table_.insert(it.key(), it.value().toString());
        return !table_.isEmpty();
    }
    QString translate(const char*, const char* source, const char*, int) const override {
        return table_.value(QString::fromUtf8(source));  // empty = not found, Qt falls back to the source
    }
    bool isEmpty() const override { return table_.isEmpty(); }

private:
    QHash<QString, QString> table_;
};

std::unique_ptr<JsonTranslator> g_app;
std::unique_ptr<QTranslator> g_qt;
QString g_current = "sk";

bool supported(const QString& code) {
    for (auto& l : languages())
        if (l.code == code) return true;
    return false;
}

}  // namespace

const std::vector<Language>& languages() {
    static const std::vector<Language> L = {
        {"sk", QString::fromUtf8("Slovenčina")}, {"en", "English"},
        {"de", "Deutsch"}, {"fr", QString::fromUtf8("Français")}, {"zh", QString::fromUtf8("中文（简体）")},
    };
    return L;
}

QString pick_language(const QString& saved) {
    if (supported(saved)) return saved;
    QString sys = QLocale::system().name().left(2);
    return supported(sys) ? sys : QStringLiteral("en");
}

void set_language(const QString& code) {
    if (g_app) QCoreApplication::removeTranslator(g_app.get());
    if (g_qt) QCoreApplication::removeTranslator(g_qt.get());
    g_app.reset();
    g_qt.reset();
    g_current = supported(code) ? code : QStringLiteral("sk");
    if (g_current != "sk") {
        auto t = std::make_unique<JsonTranslator>();
        if (t->load_json(":/i18n/" + g_current + ".json")) {
            QCoreApplication::installTranslator(t.get());
            g_app = std::move(t);
        }
    }
    // Standard buttons (OK, Cancel, Yes, ...) come from Qt's own catalogue, when it is installed.
    QString qt_name = g_current == "zh" ? QStringLiteral("zh_CN") : g_current;
    auto qt = std::make_unique<QTranslator>();
    if (qt->load("qtbase_" + qt_name, QLibraryInfo::path(QLibraryInfo::TranslationsPath)) ||
        qt->load("qtbase_" + qt_name, QCoreApplication::applicationDirPath() + "/translations")) {
        QCoreApplication::installTranslator(qt.get());
        g_qt = std::move(qt);
    }
}

QString current_language() { return g_current; }

}  // namespace rv::gui
