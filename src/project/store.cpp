#include "project/store.hpp"

#include <sqlite3.h>

#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>

#include "core/errors.hpp"
#include "core/sha256.hpp"
#include "core/text.hpp"

namespace fs = std::filesystem;

namespace rv {

namespace {

const char* MAGIC = "SQLite format 3";  // followed by a NUL byte

const char* SCHEMA_V1 = R"(
CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE binary (id INTEGER PRIMARY KEY CHECK (id = 1), name TEXT, path TEXT, sha256 TEXT, data BLOB, load_options TEXT);
CREATE TABLE names (addr INTEGER PRIMARY KEY, name TEXT NOT NULL);
CREATE TABLE comments (addr INTEGER PRIMARY KEY, text TEXT NOT NULL);
CREATE TABLE bookmarks (addr INTEGER PRIMARY KEY, label TEXT NOT NULL);
CREATE TABLE var_names (func INTEGER, key TEXT, name TEXT NOT NULL, PRIMARY KEY (func, key));
)";

// version -> SQL that upgrades from version-1 to version. Empty for now; add entries here (never
// edit old ones) when the schema changes.
const std::map<int, std::string> MIGRATIONS = {};

struct Db {
    sqlite3* h = nullptr;
    ~Db() {
        if (h) sqlite3_close(h);
    }
    void exec(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(h, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "sqlite error";
            sqlite3_free(err);
            throw std::runtime_error(msg);
        }
    }
};

struct Stmt {
    sqlite3_stmt* s = nullptr;
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Stmt() { sqlite3_finalize(s); }
    Stmt& text(int i, const std::string& v) {
        sqlite3_bind_text(s, i, v.data(), int(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& integer(int i, int64_t v) {
        sqlite3_bind_int64(s, i, v);
        return *this;
    }
    Stmt& blob(int i, const Bytes& v) {
        sqlite3_bind_blob64(s, i, v.data(), v.size(), SQLITE_TRANSIENT);
        return *this;
    }
    bool step() {
        int rc = sqlite3_step(s);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(s)));
    }
    void run() {
        step();
        sqlite3_reset(s);
        sqlite3_clear_bindings(s);
    }
    std::string col_text(int i) {
        auto* p = reinterpret_cast<const char*>(sqlite3_column_text(s, i));
        return p ? std::string(p, size_t(sqlite3_column_bytes(s, i))) : "";
    }
    int64_t col_int(int i) { return sqlite3_column_int64(s, i); }
    bool col_null(int i) { return sqlite3_column_type(s, i) == SQLITE_NULL; }
};

bool has_magic(const std::string& path) {
    std::ifstream f(path_from_utf8(path), std::ios::binary);
    char buf[16] = {};
    if (!f.read(buf, 16)) return false;
    return std::memcmp(buf, MAGIC, 15) == 0 && buf[15] == '\0';
}

std::string now_iso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

}  // namespace

Json load_options_json(const LoadOptions& o) {
    Json j = Json::object();
    j.set("arch", o.arch ? Json(*o.arch) : Json());
    j.set("base", o.base ? Json(uint64_t(*o.base)) : Json());
    j.set("raw", o.force_raw);
    return j;
}

LoadOptions load_options_from_json(const Json& j) {
    LoadOptions o;
    if (j.get("arch").is_string()) o.arch = j.get("arch").as_string();
    if (j.get("base").is_number()) o.base = Addr(j.get("base").as_int());
    o.force_raw = j.get("raw").as_bool();
    return o;
}

bool is_project(const std::string& path) {
    try {
        if (!has_magic(path)) return false;
        Db db;
        if (sqlite3_open_v2(path.c_str(), &db.h, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) return false;
        Stmt q(db.h, "SELECT value FROM meta WHERE key='schema_version'");
        q.step();
        return true;
    } catch (...) {
        return false;
    }
}

void save_project(const std::string& path, const Bytes& data, const std::string& binary_path, const LoadOptions& options,
                  const Annotations& ann, const Json& settings) {
    std::string tmp = path + ".tmp";
    try {
        std::error_code ec;
        fs::remove(path_from_utf8(tmp), ec);
        {
            Db db;
            if (sqlite3_open_v2(tmp.c_str(), &db.h, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
                throw std::runtime_error(db.h ? sqlite3_errmsg(db.h) : "sqlite3_open failed");
            db.exec("BEGIN");
            db.exec(SCHEMA_V1);
            {
                Stmt m(db.h, "INSERT INTO meta VALUES (?, ?)");
                m.text(1, "schema_version").text(2, std::to_string(SCHEMA_VERSION)).run();
                m.text(1, "app").text(2, "REVERSA").run();
                m.text(1, "saved_at").text(2, now_iso()).run();
                m.text(1, "settings").text(2, settings.dump()).run();
            }
            {
                Stmt b(db.h, "INSERT INTO binary VALUES (1, ?, ?, ?, ?, ?)");
                std::string name = path_to_utf8(path_from_utf8(binary_path).filename());
                b.text(1, name).text(2, binary_path).text(3, sha256_hex(data)).blob(4, data);
                b.text(5, load_options_json(options).dump()).run();
            }
            auto put_map = [&](const char* sql, const std::map<Addr, std::string>& m) {
                Stmt s(db.h, sql);
                for (auto& [a, v] : m) s.integer(1, int64_t(a)).text(2, v).run();
            };
            put_map("INSERT INTO names VALUES (?, ?)", ann.names);
            put_map("INSERT INTO comments VALUES (?, ?)", ann.comments);
            put_map("INSERT INTO bookmarks VALUES (?, ?)", ann.bookmarks);
            {
                Stmt s(db.h, "INSERT INTO var_names VALUES (?, ?, ?)");
                for (auto& [k, v] : ann.var_names) s.integer(1, int64_t(k.first)).text(2, k.second).text(3, v).run();
            }
            db.exec("COMMIT");
        }
        fs::rename(path_from_utf8(tmp), path_from_utf8(path));  // atomic: an interrupted save never corrupts the old project
    } catch (const std::exception& e) {
        std::error_code ec;
        fs::remove(path_from_utf8(tmp), ec);
        throw ProjectError("Projekt sa nepodarilo uložiť do '" + path + "'.", e.what());
    }
}

ProjectData load_project(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(path_from_utf8(path), ec)) throw ProjectError("Projekt '" + path + "' neexistuje.");
    Db db;
    if (sqlite3_open_v2(path.c_str(), &db.h, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK)
        throw ProjectError("'" + path + "' nie je platný projekt REVERSA.", db.h ? sqlite3_errmsg(db.h) : "");
    try {
        int version = 0;
        {
            Stmt q(db.h, "SELECT value FROM meta WHERE key='schema_version'");
            if (!q.step()) throw std::runtime_error("chýba schema_version");
            version = std::stoi(q.col_text(0));
        }
        if (version > SCHEMA_VERSION)
            throw ProjectError("Projekt bol uložený novšou verziou REVERSA (schéma " + std::to_string(version) +
                               "). Aktualizujte REVERSA.");
        for (int v = version + 1; v <= SCHEMA_VERSION; v++) {
            db.exec("BEGIN");
            db.exec(MIGRATIONS.at(v));
            Stmt u(db.h, "UPDATE meta SET value=? WHERE key='schema_version'");
            u.text(1, std::to_string(v)).run();
            db.exec("COMMIT");
        }
        ProjectData p;
        {
            Stmt q(db.h, "SELECT name, path, sha256, data, load_options FROM binary WHERE id=1");
            if (!q.step()) throw std::runtime_error("chýba binárka");
            p.name = q.col_text(0);
            p.path = q.col_text(1);
            std::string sha = q.col_text(2);
            auto* blob = static_cast<const uint8_t*>(sqlite3_column_blob(q.s, 3));
            auto n = size_t(sqlite3_column_bytes(q.s, 3));
            auto data = std::make_shared<Bytes>(blob, blob + n);
            if (sha256_hex(*data) != sha)
                throw ProjectError("Binárka uložená v projekte je poškodená (nesedí kontrolný súčet SHA-256).");
            p.data = data;
            std::string opts = q.col_null(4) ? "{}" : q.col_text(4);
            p.load_options = load_options_from_json(Json::parse(opts.empty() ? "{}" : opts));
        }
        auto get_map = [&](const char* sql, std::map<Addr, std::string>& m) {
            Stmt s(db.h, sql);
            while (s.step()) m[Addr(s.col_int(0))] = s.col_text(1);
        };
        get_map("SELECT addr, name FROM names", p.annotations.names);
        get_map("SELECT addr, text FROM comments", p.annotations.comments);
        get_map("SELECT addr, label FROM bookmarks", p.annotations.bookmarks);
        {
            Stmt s(db.h, "SELECT func, key, name FROM var_names");
            while (s.step()) p.annotations.var_names[{Addr(s.col_int(0)), s.col_text(1)}] = s.col_text(2);
        }
        {
            Stmt s(db.h, "SELECT value FROM meta WHERE key='settings'");
            if (s.step()) p.settings = Json::parse(s.col_text(0));
            if (!p.settings.is_object()) p.settings = Json::object();
        }
        return p;
    } catch (const ProjectError&) {
        throw;
    } catch (const std::exception& e) {
        throw ProjectError("Projekt '" + path + "' je poškodený alebo nie je to projekt REVERSA.", e.what());
    }
}

}  // namespace rv
