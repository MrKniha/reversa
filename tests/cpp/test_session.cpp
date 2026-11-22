// Session: names, resolve, undoable edits, events, project save/load.
#include <filesystem>
#include <fstream>

#include "core/errors.hpp"
#include "core/json.hpp"
#include "core/sha256.hpp"
#include "core/text.hpp"
#include "project/store.hpp"
#include "session/session.hpp"
#include "test.hpp"

#include <sqlite3.h>

using namespace rv;
namespace fs = std::filesystem;

static fs::path temp_dir() {
    auto d = fs::temp_directory_path() / ("rvtest-" + std::to_string(std::rand()));
    fs::create_directories(d);
    return d;
}

TEST("resolve names, imports and default names") {
    auto s = Session::open(rvtest::sample("login_x64.elf"));
    s->require_analysis();
    auto main = s->resolve("main");
    CHECK(main.has_value());
    CHECK(s->resolve("strcmp").has_value());
    CHECK(s->resolve("strcmp@plt") == s->resolve("strcmp"));
    CHECK(s->resolve("0x" + hex_digits(*main)) == main);
    CHECK(s->resolve("sub_" + hex_digits(*main)) == main);
    CHECK(!s->resolve("does_not_exist").has_value());
    auto stripped = Session::open(rvtest::sample("login_x64_stripped.elf"));
    auto& a = stripped->require_analysis();
    for (auto& [ad, f] : a.functions) {
        std::string n = stripped->name_of(ad);
        CHECK(stripped->resolve(n) == ad);  // every displayed name can be searched
    }
}

TEST("rename propagates everywhere, undo and redo") {
    auto s = Session::open(rvtest::sample("hello_x64.elf"));
    s->require_analysis();
    int events = 0;
    s->events.subscribe("renamed", [&](const Event&) { events++; });
    auto sum = *s->resolve("sum_to");
    auto main = *s->resolve("main");
    s->rename("sum_to", "add_up");
    CHECK(s->name_of(sum) == "add_up");
    CHECK(s->pseudocode(main).find("add_up(") != std::string::npos);
    CHECK(s->resolve("add_up") == sum);
    CHECK(s->undo().has_value());
    CHECK(s->name_of(sum) == "sum_to");
    CHECK(s->redo().has_value());
    CHECK(s->name_of(sum) == "add_up");
    CHECK(events == 3);
    bool threw = false;
    try { s->rename("main", "add_up"); } catch (const AnnotationError&) { threw = true; }
    CHECK(threw);  // name already used
    threw = false;
    try { s->rename("main", "1bad name"); } catch (const AnnotationError&) { threw = true; }
    CHECK(threw);
}

TEST("variable rename propagates and undoes") {
    auto s = Session::open(rvtest::sample("hello_x64.elf"));
    s->require_analysis();
    auto f = *s->resolve("sum_to");
    CHECK(s->pseudocode(f).find("local_4") != std::string::npos);
    s->rename_var("sum_to", "s:-4", "i");
    auto c = s->pseudocode(f);
    CHECK(c.find("i++;") != std::string::npos && c.find("local_4") == std::string::npos);
    s->undo();
    CHECK(s->pseudocode(f).find("local_4") != std::string::npos);
}

TEST("project roundtrip keeps annotations, tampering is detected") {
    auto dir = temp_dir();
    std::string proj = path_to_utf8(dir / "p.rvproj");
    {
        auto s = Session::open(rvtest::sample("login_x64.elf"));
        s->require_analysis();
        s->rename("check_user", "verify");
        s->set_comment("main", "vstup");
        s->set_bookmark("main", "začiatok");
        s->rename_var("main", "s:-4", "count");
        s->settings.set("theme", "dark");
        s->save(proj);
        CHECK(!s->dirty);
    }
    CHECK(is_project(proj));
    CHECK(!is_project(rvtest::sample("login_x64.elf")));
    auto s = Session::open(proj);
    s->require_analysis();
    auto main = *s->resolve("main");
    CHECK(s->resolve("verify").has_value());
    CHECK(s->annotations.comments[main] == "vstup");
    CHECK(s->annotations.bookmarks[main] == "začiatok");
    CHECK((s->annotations.var_names[{main, "s:-4"}] == "count"));
    CHECK(s->settings.get("theme").as_string() == "dark");
    CHECK(s->project_path.has_value());
    // flip one byte of the stored binary blob -> SHA-256 mismatch
    {
        std::fstream f(path_from_utf8(proj), std::ios::in | std::ios::out | std::ios::binary);
        std::string all((std::istreambuf_iterator<char>(f)), {});
        auto pos = all.find("\x7f" "ELF");
        CHECK(pos != std::string::npos);
        f.seekp(std::streamoff(pos + 100));
        f.put(char(all[pos + 100] ^ 0x55));
    }
    bool threw = false;
    try { Session::open_project(proj); } catch (const ProjectError& e) {
        threw = std::string(e.what()).find("SHA-256") != std::string::npos;
    }
    CHECK(threw);
    // garbage file
    std::string junk = path_to_utf8(dir / "junk.rvproj");
    { std::ofstream(path_from_utf8(junk)) << "not a project"; }
    threw = false;
    try { load_project(junk); } catch (const ProjectError&) { threw = true; }
    CHECK(threw);
    fs::remove_all(dir);
}

TEST("a failing event listener is isolated") {
    auto s = Session::open(rvtest::sample("hello_x64.elf"));
    s->require_analysis();
    bool second = false;
    s->events.subscribe("comment", [](const Event&) { throw std::runtime_error("boom"); });
    s->events.subscribe("comment", [&](const Event&) { second = true; });
    s->set_comment("main", "x");
    CHECK(second);
    CHECK(!s->runtime_diagnostics.empty());
}

TEST("json parse and dump") {
    auto j = Json::parse(R"({"a": [1, 2.5, "xé", true, null], "b": {"c": -3}})");
    CHECK(j.get("a").size() == 5);
    CHECK(j.get("a").as_array()[2].as_string() == "x\xc3\xa9");
    CHECK(j.get("b").get("c").as_int() == -3);
    CHECK(Json::parse(j.dump()).dump() == j.dump());
    bool threw = false;
    try { Json::parse("{bad"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

TEST("undo stack limit, redo cleared by a new command") {
    UndoStack st(2);
    std::vector<int> log;
    for (int i = 0; i < 3; i++) st.push({std::to_string(i), [&log, i] { log.push_back(i); }, [&log, i] { log.push_back(-i); }});
    CHECK(st.undo() == std::optional<std::string>("2"));
    CHECK(st.undo() == std::optional<std::string>("1"));
    CHECK(!st.undo().has_value());
    CHECK(st.redo() == std::optional<std::string>("1"));
    st.push({"x", [] {}, [] {}});
    CHECK(!st.can_redo());
    st.clear();
    CHECK(!st.can_undo());
    CHECK(log == std::vector<int>({0, 1, 2, -2, -1, 1}));
}

TEST("project migrations upgrade the schema step by step") {
    auto dir = temp_dir();
    auto s = Session::open(rvtest::sample("hello_x64.elf"));
    std::string path = s->save((dir / "h.rvproj").string());
    auto data = load_project(path, {{SCHEMA_VERSION + 1, "CREATE TABLE IF NOT EXISTS extra (x INTEGER);"}},
                             SCHEMA_VERSION + 1);
    CHECK_EQ(data.name, std::string("hello_x64.elf"));
    bool newer = false;  // the file now carries the newer schema, so the current version refuses it
    try {
        load_project(path);
    } catch (const ProjectError& e) {
        newer = std::string(e.what()).find("newer REVERSA") != std::string::npos;
    }
    CHECK(newer);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------- project files are plain SQLite
static void sql(const std::string& path, const std::string& script) {
    sqlite3* db = nullptr;
    CHECK(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, script.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}

static std::string sql_value(const std::string& path, const std::string& query) {
    sqlite3* db = nullptr;
    sqlite3_stmt* st = nullptr;
    std::string out;
    sqlite3_open(path.c_str(), &db);
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &st, nullptr) == SQLITE_OK)
        while (sqlite3_step(st) == SQLITE_ROW) out += std::string(reinterpret_cast<const char*>(sqlite3_column_text(st, 0))) + ",";
    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}

TEST("project is plain SQLite with documented tables") {
    auto dir = temp_dir();
    std::string path = Session::open(rvtest::sample("hello_x64.elf"))->save(path_to_utf8(dir / "h.rvproj"));
    std::string tables = sql_value(path, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name");
    for (auto t : {"meta,", "binary,", "names,", "comments,", "bookmarks,", "var_names,"}) CHECK(contains(tables, t));
    CHECK_EQ(sql_value(path, "SELECT value FROM meta WHERE key='schema_version'"), std::to_string(SCHEMA_VERSION) + ",");
    fs::remove_all(dir);
}

TEST("project save errors and default project paths") {
    auto dir = temp_dir();
    bool threw = false;
    try { Session::open(rvtest::sample("hello_x64.elf"))->save(path_to_utf8(dir / "no" / "such" / "x.rvproj")); }
    catch (const ProjectError&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { load_project(path_to_utf8(dir / "nope.rvproj")); } catch (const ProjectError&) { threw = true; }
    CHECK(threw);
    CHECK(!is_project(path_to_utf8(dir / "missing.rvproj")));
    CHECK_EQ(default_project_path(path_to_utf8(dir / "a.out")), path_to_utf8(dir / "a.out.rvproj"));
    CHECK(contains(default_project_path("<demo>/login_x64.elf"), "reversa-projects"));
    fs::remove_all(dir);
}

TEST("projects written by REVERSA 0.1 (Python) still open") {
    auto dir = temp_dir();
    std::string path = path_to_utf8(dir / "old.rvproj");
    std::ifstream f(path_from_utf8(rvtest::sample("hello_x64.elf")), std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    sql(path, R"(
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE binary (id INTEGER PRIMARY KEY CHECK (id = 1), name TEXT, path TEXT, sha256 TEXT, data BLOB, load_options TEXT);
CREATE TABLE names (addr INTEGER PRIMARY KEY, name TEXT NOT NULL);
CREATE TABLE comments (addr INTEGER PRIMARY KEY, text TEXT NOT NULL);
CREATE TABLE bookmarks (addr INTEGER PRIMARY KEY, label TEXT NOT NULL);
CREATE TABLE var_names (func INTEGER, key TEXT, name TEXT NOT NULL, PRIMARY KEY (func, key));
INSERT INTO meta VALUES ('schema_version', '1'), ('app', 'REVERSA'), ('settings', '{}');
INSERT INTO names VALUES (4198806, 'my_func');)");
    sqlite3* db = nullptr;
    sqlite3_open(path.c_str(), &db);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO binary VALUES (1, 'hello_x64.elf', '/old/hello_x64.elf', ?, ?, ?)", -1, &st, nullptr);
    std::string sha = sha256_hex(data), opts = R"({"arch": null, "base": null, "raw": false})";
    sqlite3_bind_text(st, 1, sha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(st, 2, data.data(), int(data.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, opts.c_str(), -1, SQLITE_TRANSIENT);
    CHECK(sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    sqlite3_close(db);
    auto s = Session::open(path);
    CHECK_EQ(s->name_of(0x401196), std::string("my_func"));
    CHECK_EQ(s->binary.path, std::string("/old/hello_x64.elf"));
    fs::remove_all(dir);
}
