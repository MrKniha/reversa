// Text report of an analysis (Markdown or self-contained HTML) and the JSON summary used by the CLI.
#pragma once

#include <string>

#include "core/json.hpp"
#include "session/session.hpp"

namespace rv {

std::string build_markdown(Session& s);
std::string build_html(Session& s);
std::string build_report(Session& s, const std::string& fmt = "md");  // md | html
Json summary(Session& s);

}  // namespace rv
