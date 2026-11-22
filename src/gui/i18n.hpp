// GUI languages. Source strings are Slovak; translations are JSON tables in src/gui/i18n/<code>.json,
// compiled into the binary as Qt resources and checked by tools/i18n_check.cpp.
#pragma once

#include <QCoreApplication>
#include <QString>
#include <vector>

namespace rv::gui {

struct Language {
    QString code;    // sk en de fr zh
    QString native;  // name shown in the language picker, in that language
};

const std::vector<Language>& languages();

// Saved choice if supported, else English (the default language).
QString pick_language(const QString& saved);

// Install the translation for `code` (Slovak needs none) plus Qt's own dialog translations if present.
// Replaces whatever an earlier call installed.
void set_language(const QString& code);
QString current_language();

// Translate a string outside a QObject (same table as QObject::tr).
inline QString T(const char* source) { return QCoreApplication::translate("rv", source); }

}  // namespace rv::gui
