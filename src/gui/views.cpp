#include "gui/views.hpp"

#include <QStringList>

#include "core/text.hpp"
#include "gui/i18n.hpp"

namespace rv::gui {

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }
QString esc(const std::string& s) { return q(s).toHtmlEscaped(); }
QString hexq(Addr a) { return q(hex(a)); }

QString span(const QString& color, const QString& text) {
    return QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color, text);
}
QString anchor(Addr a, const QString& color, const QString& text) {
    return QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none\">%3</a>").arg(link_for(a), color, text);
}
QString pad(const QString& s, int width) { return s.size() >= width ? s + ' ' : s + QString(width - s.size(), ' '); }

QString shorten(const std::string& s, size_t n) {
    std::string t = utf8_prefix(s, n);
    return esc(t) + (t.size() < s.size() ? QStringLiteral("…") : QString());
}

// Branch/call target of an instruction as resolved by the analysis (thunks included).
std::optional<Addr> flow_target(const Analysis& a, const Insn& ins) {
    if (ins.flow == Flow::Seq || ins.flow == Flow::Ret || ins.flow == Flow::Trap) return std::nullopt;
    std::optional<Addr> t;
    for (auto& x : a.xrefs.frm(ins.addr))
        if (x.kind == "call" || x.kind == "jump") t = x.to;
    return t ? t : ins.target;
}

QString page(const Palette& p, const QString& body) {
    return QStringLiteral("<html><body style=\"color:%1\"><pre style=\"font-family:monospace\">%2</pre></body></html>")
        .arg(p.text, body);
}

// Fixed comments the decompiler (core, English) emits, mapped to the GUI's translatable source strings.
QString decompiler_comment(const std::string& text) {
    if (text == PROVISIONAL_NOTE)
        return T("// Provisional pseudocode (REVERSA MVP): types are guessed from access sizes, check the listing.");
    if (text == "  // indirect jump (e.g. switch): not expanded yet")
        return T("  // indirect jump (e.g. switch): not expanded yet");
    return q(text);
}

QString xref_label(const std::string& kind) {
    if (kind == "call") return T("called from");
    if (kind == "jump") return T("jump from");
    if (kind == "read") return T("read from");
    if (kind == "write") return T("written from");
    if (kind == "addr") return T("address used in");
    if (kind == "data") return T("pointer in data");
    return q(kind);
}

}  // namespace

Palette Palette::make(bool dark) {
    Palette p;
    p.dark = dark;
    if (dark) {
        p.text = "#d4d4d4"; p.dim = "#808080"; p.addr = "#6a9955"; p.link = "#4fc1ff"; p.keyword = "#c586c0";
        p.number = "#b5cea8"; p.string = "#ce9178"; p.comment = "#6a9955"; p.type = "#4ec9b0"; p.label = "#dcdcaa";
        p.highlight = "#264f78"; p.warn = "#d7ba7d";
    } else {
        p.text = "#1f1f1f"; p.dim = "#6e6e6e"; p.addr = "#2e7d32"; p.link = "#0b57d0"; p.keyword = "#8e24aa";
        p.number = "#098658"; p.string = "#a31515"; p.comment = "#5f7f5f"; p.type = "#267f99"; p.label = "#795e26";
        p.highlight = "#fff2a8"; p.warn = "#9a6700";
    }
    return p;
}

QString link_for(Addr a) { return QStringLiteral("a:") + hexq(a); }

std::optional<Addr> parse_address(const QString& text) {
    QString t = text.trimmed().toLower();
    bool ok = false;
    qulonglong v = 0;
    if (t.startsWith("0x")) v = t.mid(2).toULongLong(&ok, 16);
    else if (t.endsWith('h')) v = t.chopped(1).toULongLong(&ok, 16);
    else v = t.toULongLong(&ok, 10);
    if (!ok) return std::nullopt;
    return Addr(v);
}

std::optional<Addr> addr_from_link(const QString& href) {
    if (!href.startsWith("a:")) return std::nullopt;
    return parse_address(href.mid(2));
}

QString listing_html(Session& s, Addr func, const Palette& p, std::optional<Addr> focus) {
    std::lock_guard<std::recursive_mutex> g(s.lock);
    const Analysis& a = *s.analysis;
    auto* fn = a.function(func);
    if (!fn) fn = a.function_containing(func);
    if (!fn) return page(p, span(p.warn, T("There is no function at address %1.").arg(hexq(func))));
    QString out;
    auto comment_line = [&](const QString& text) { out += span(p.comment, "; " + text) + "\n"; };

    auto callers = a.callers(fn->addr, false);
    comment_line(QStringLiteral("════════ %1 %2 (%3)")
                     .arg(fn->kind == "normal" ? T("function") : T("import"), esc(s.name_of(fn->addr)),
                          hexq(fn->addr)));
    if (!callers.empty()) {
        QStringList names;
        for (size_t i = 0; i < callers.size() && i < 8; i++) names << anchor(callers[i], p.link, esc(s.name_of(callers[i])));
        QString more = callers.size() > 8 ? QStringLiteral(" … (+%1)").arg(callers.size() - 8) : QString();
        out += span(p.comment, "; " + T("called from:") + " ") + names.join(", ") + span(p.comment, more) + "\n";
    }
    if (fn->kind != "normal") {
        comment_line(T("function from an external library%1: its code is not in this file")
                         .arg(fn->library.empty() ? QString() : " " + esc(fn->library)));
        if (fn->kind == "import") return page(p, out);
    }
    if (auto it = s.annotations.comments.find(fn->addr); it != s.annotations.comments.end())
        comment_line(esc(it->second));
    out += "<a name=\"" + hexq(fn->addr) + "\"></a>" + span(p.label, "<b>" + esc(s.name_of(fn->addr)) + ":</b>") + "\n";

    for (auto& [start, blk] : fn->blocks) {
        if (start != fn->addr) {
            QString label = s.has_user_name(start) ? esc(s.name_of(start)) : QStringLiteral("loc_") + q(hex_digits(start));
            out += "\n<a name=\"" + hexq(start) + "\"></a>" + span(p.label, label + ":") + "\n";
        }
        for (Addr ad : blk.insns) {
            const Insn* ins = a.insn(ad);
            if (!ins) continue;
            bool focused = focus && *focus == ad;
            QString line = "<a name=\"" + hexq(ad) + "\"></a>";
            QString addr_text = pad(hexq(ad), 14);
            line += span(p.addr, addr_text);
            QStringList bytes;
            for (size_t i = 0; i < ins->raw.size() && i < 8; i++) bytes << QStringLiteral("%1").arg(ins->raw[i], 2, 16, QChar('0'));
            line += span(p.dim, pad(bytes.join(' ') + (ins->raw.size() > 8 ? QStringLiteral("…") : QString()), 22));
            line += span(p.keyword, pad(esc(ins->mnemonic), 8));
            auto tgt = flow_target(a, *ins);
            if (tgt && (ins->flow == Flow::Call || ins->flow == Flow::Jmp || ins->flow == Flow::CJmp ||
                        ins->flow == Flow::ICall || ins->flow == Flow::IJmp)) {
                QString name = fn->blocks.count(*tgt) && *tgt != fn->addr && !s.has_user_name(*tgt)
                                   ? QStringLiteral("loc_") + q(hex_digits(*tgt))
                                   : esc(s.name_of(*tgt));
                line += anchor(*tgt, p.link, name);
            } else {
                line += esc(ins->op_str);
            }
            QStringList notes;
            for (auto& r : ins->refs) {
                if (auto txt = s.string_text(r.addr)) {
                    notes << anchor(r.addr, p.string, "\"" + shorten(*txt, 60) + "\"");
                } else if (s.has_user_name(r.addr) || a.functions.count(r.addr) || a.slot_imports.count(r.addr)) {
                    notes << anchor(r.addr, p.link, esc(s.name_of(r.addr)));
                }
            }
            if (auto it = s.annotations.comments.find(ad); it != s.annotations.comments.end()) notes << span(p.comment, esc(it->second));
            if (auto it = s.annotations.bookmarks.find(ad); it != s.annotations.bookmarks.end())
                notes << span(p.warn, QStringLiteral("★ ") + esc(it->second));
            if (!notes.isEmpty()) line += span(p.comment, QStringLiteral("   ; ")) + notes.join(span(p.comment, ", "));
            if (focused) line = "<span style=\"background-color:" + p.highlight + "\">" + line + "</span>";
            out += line + "\n";
            if (ins->flow == Flow::Ret || ins->flow == Flow::Jmp || ins->flow == Flow::IJmp)
                out += span(p.dim, QString(40, QChar(0x2500))) + "\n";
        }
    }
    return page(p, out);
}

QString pseudocode_html(Session& s, Addr func, const Palette& p, const std::string& selected_var) {
    std::vector<Line> lines;
    {
        std::lock_guard<std::recursive_mutex> g(s.lock);
        lines = s.decompile(func);
    }
    QString out;
    for (auto& ln : lines) {
        QString text = QString(ln.indent * 4, ' ');
        for (auto& seg : ln.segs) {
            QString t = seg.kind == "comment" ? decompiler_comment(seg.text).toHtmlEscaped() : esc(seg.text);
            const std::string& k = seg.kind;
            if (k == "var") {
                QString style = "color:" + p.text + "; text-decoration:none";
                if (!selected_var.empty() && seg.key == selected_var) style += "; background-color:" + p.highlight;
                text += "<a href=\"v:" + q(seg.key).toHtmlEscaped() + "\" style=\"" + style + "\">" + t + "</a>";
            } else if (seg.addr && (k == "func" || k == "glob" || k == "str" || k == "label")) {
                QString color = k == "str" ? p.string : k == "label" ? p.label : p.link;
                text += anchor(*seg.addr, color, t);
            } else {
                QString color = k == "comment" ? p.comment
                              : k == "type"    ? p.type
                              : k == "kw"      ? p.keyword
                              : k == "num"     ? p.number
                              : k == "str"     ? p.string
                              : k == "label"   ? p.label
                              : k == "asm"     ? p.warn
                                               : QString();
                text += color.isEmpty() ? t : span(color, t);
            }
        }
        out += text + "\n";
    }
    return page(p, out);
}

QString details_html(Session& s, Addr addr, const Palette& p) {
    std::lock_guard<std::recursive_mutex> g(s.lock);
    const Analysis& a = *s.analysis;
    const Binary& b = s.binary;
    auto* fn = a.function(addr);
    auto* cont = a.function_containing(addr);
    auto* str = a.string(addr);
    QString kind = fn ? (fn->kind == "normal" ? T("function") : T("imported function"))
                 : str ? T("string")
                 : b.is_code(addr) ? T("code")
                 : b.is_mapped(addr) ? T("data")
                                     : T("outside the file");
    QString h = QStringLiteral("<html><body style=\"color:%1\">").arg(p.text);
    h += QStringLiteral("<h3 style=\"margin:0\">%1</h3>").arg(esc(s.name_of(addr)));
    h += QStringLiteral("<p style=\"color:%1; margin-top:2px\">%2 · %3").arg(p.dim, hexq(addr), kind);
    if (auto* sec = b.section_at(addr)) h += " · " + esc(sec->name);
    if (auto off = b.addr_to_offset(addr)) h += " · " + T("offset %1").arg(hexq(*off));
    h += "</p>";
    auto row = [&](const QString& label, const QString& value) {
        h += QStringLiteral("<b>%1:</b> %2<br>").arg(label, value);
    };
    if (cont && !fn) row(T("In function"), anchor(cont->addr, p.link, esc(s.name_of(cont->addr))));
    if (str) row(T("Text"), span(p.string, "\"" + shorten(str->text, 300) + "\""));
    if (fn) {
        row(T("Size"), T("%1 B, %2 instructions, %3 blocks").arg(fn->size).arg(fn->insn_count).arg(fn->blocks.size()));
        if (!fn->library.empty()) row(T("Library"), esc(fn->library));
    }
    if (auto it = s.annotations.comments.find(addr); it != s.annotations.comments.end()) row(T("Comment"), esc(it->second));
    if (auto it = s.annotations.bookmarks.find(addr); it != s.annotations.bookmarks.end()) row(T("Bookmark"), esc(it->second));

    auto list = [&](const QString& title, const std::vector<Addr>& items) {
        if (items.empty()) return;
        h += QStringLiteral("<p><b>%1</b> (%2)<br>").arg(title).arg(items.size());
        for (size_t i = 0; i < items.size() && i < 200; i++) h += anchor(items[i], p.link, esc(s.name_of(items[i]))) + "<br>";
        h += "</p>";
    };
    if (fn) {
        list(T("Called from"), a.callers(addr));
        std::vector<Addr> callees;
        for (Addr c : fn->callees)
            if (a.functions.count(c)) callees.push_back(c);
        for (Addr c : fn->tail_calls)
            if (a.functions.count(c) && !fn->callees.count(c)) callees.push_back(c);
        list(T("Calls"), callees);
    }
    auto xr = a.xrefs.to(addr);
    h += QStringLiteral("<p><b>%1</b> (%2)<br>").arg(T("Who uses this (x-refs)")).arg(xr.size());
    if (xr.empty()) h += span(p.dim, T("Nobody: nothing in the code or data points to this address.")) + "<br>";
    for (size_t i = 0; i < xr.size() && i < 500; i++) {
        auto* f = a.function_containing(xr[i].frm);
        QString where = f ? esc(s.name_of(f->addr)) + "+" + QString::number(xr[i].frm - f->addr, 16) : hexq(xr[i].frm);
        h += span(p.dim, xref_label(xr[i].kind) + " ") + anchor(xr[i].frm, p.link, where) + "<br>";
    }
    h += "</p>";
    auto from = a.xrefs.frm(addr);
    if (!from.empty()) {
        h += QStringLiteral("<p><b>%1</b> (%2)<br>").arg(T("What this uses")).arg(from.size());
        for (size_t i = 0; i < from.size() && i < 200; i++)
            h += span(p.dim, q(from[i].kind) + " ") + anchor(from[i].to, p.link, esc(s.name_of(from[i].to))) + "<br>";
        h += "</p>";
    }
    return h + "</body></html>";
}

QString hexdump_text(const Binary& b, Addr start, size_t length) {
    Bytes data = b.read(start, length);
    QString out;
    for (size_t i = 0; i < data.size(); i += 16) {
        QString hexpart, ascii;
        for (size_t k = 0; k < 16; k++) {
            if (i + k < data.size()) {
                uint8_t c = data[i + k];
                hexpart += QStringLiteral("%1 ").arg(c, 2, 16, QChar('0'));
                ascii += (c >= 32 && c < 127) ? QChar(c) : QChar('.');
            } else {
                hexpart += "   ";
            }
            if (k == 7) hexpart += ' ';
        }
        out += pad(hexq(start + i), 14) + hexpart + " " + ascii + "\n";
    }
    return out;
}

}  // namespace rv::gui
