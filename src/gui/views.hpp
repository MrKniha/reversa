// Text renderings of the session for the GUI panels (listing, pseudocode, details, hex dump).
//
// They return rich text (the subset of HTML that QTextBrowser understands). Clickable parts are
// links: "a:0x401000" jumps to an address, "v:<key>" selects a pseudocode variable.
#pragma once

#include <QString>
#include <optional>
#include <string>

#include "session/session.hpp"

namespace rv::gui {

struct Palette {
    bool dark = false;
    QString text, dim, addr, link, keyword, number, string, comment, type, label, highlight, warn;
    static Palette make(bool dark);
};

QString link_for(Addr a);
std::optional<Addr> addr_from_link(const QString& href);
std::optional<Addr> parse_address(const QString& text);  // "0x401000", "401000h", "4198400"

// Listing (disassembly) of one function with names, string references, comments and bookmarks.
QString listing_html(Session& s, Addr func, const Palette& p, std::optional<Addr> focus = std::nullopt);
// Pseudocode with clickable functions, globals, strings and variables. `selected_var` is highlighted.
QString pseudocode_html(Session& s, Addr func, const Palette& p, const std::string& selected_var = "");
// Everything known about one address: kind, section, x-refs to and from, callers, callees, notes.
QString details_html(Session& s, Addr addr, const Palette& p);
// Classic 16-bytes-per-row hex dump with ASCII column.
QString hexdump_text(const Binary& b, Addr start, size_t length);

}  // namespace rv::gui
