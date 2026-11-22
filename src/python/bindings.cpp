// pybind11 bindings of the C++ core, shared by the `reversa._reversa` extension module and the
// interpreter embedded in the reversa application. The friendly Python API (reversa.Program)
// is written in Python on top of these objects (python/reversa/api.py).
#include "python/bindings.hpp"

#include <algorithm>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include "core/errors.hpp"
#include "core/text.hpp"
#include "report/report.hpp"

namespace py = pybind11;
using namespace pybind11::literals;

namespace rv::py_bind {

namespace {

// Lets Python subclasses (reversa.plugins.PluginManager) receive hooks from C++.
class PyPluginHost : public PluginHost {
public:
    using PluginHost::PluginHost;
    bool has_hook(const std::string& hook) override { PYBIND11_OVERRIDE_PURE(bool, PluginHost, has_hook, hook); }
    void on_load() override { PYBIND11_OVERRIDE_PURE(void, PluginHost, on_load, ); }
    void on_function_analyzed(Addr addr) override { PYBIND11_OVERRIDE_PURE(void, PluginHost, on_function_analyzed, addr); }
    void on_rename(Addr addr, const std::string& old_name, const std::string& new_name) override {
        PYBIND11_OVERRIDE_PURE(void, PluginHost, on_rename, addr, old_name, new_name);
    }
    void on_unload() override { PYBIND11_OVERRIDE_PURE(void, PluginHost, on_unload, ); }
};

py::object to_py(const Json& j) {
    if (j.is_null()) return py::none();
    if (j.is_bool()) return py::bool_(j.as_bool());
    if (j.is_int()) return py::int_(j.as_int());
    if (j.is_number()) return py::float_(j.as_double());
    if (j.is_string()) return py::str(j.as_string());
    if (j.is_array()) {
        py::list l;
        for (auto& x : j.as_array()) l.append(to_py(x));
        return l;
    }
    py::dict d;
    for (auto& [k, v] : j.as_object()) d[py::str(k)] = to_py(v);
    return d;
}

Json from_py(const py::handle& o) {
    if (o.is_none()) return Json();
    if (py::isinstance<py::bool_>(o)) return Json(o.cast<bool>());
    if (py::isinstance<py::int_>(o)) return Json(o.cast<int64_t>());
    if (py::isinstance<py::float_>(o)) return Json(o.cast<double>());
    if (py::isinstance<py::str>(o)) return Json(o.cast<std::string>());
    if (py::isinstance<py::dict>(o)) {
        Json j = Json::object();
        for (auto item : o.cast<py::dict>()) j.set(py::str(item.first).cast<std::string>(), from_py(item.second));
        return j;
    }
    if (py::isinstance<py::list>(o) || py::isinstance<py::tuple>(o)) {
        Json j = Json::array();
        for (auto item : o) j.push(from_py(item));
        return j;
    }
    return Json(py::str(o).cast<std::string>());
}

py::object event_value(const EventValue& v) {
    if (std::holds_alternative<uint64_t>(v)) return py::int_(std::get<uint64_t>(v));
    if (std::holds_alternative<std::string>(v)) return py::str(std::get<std::string>(v));
    return py::none();
}

py::list lines_to_py(const std::vector<Line>& lines) {
    py::list out;
    for (auto& ln : lines) {
        py::list segs;
        for (auto& s : ln.segs) {
            py::object ref = s.addr ? py::object(py::int_(*s.addr)) : (!s.key.empty() ? py::object(py::str(s.key)) : py::none());
            segs.append(py::make_tuple(s.text, s.kind, ref));
        }
        out.append(py::dict("indent"_a = ln.indent, "segs"_a = segs, "addr"_a = ln.addr ? py::object(py::int_(*ln.addr)) : py::none()));
    }
    return out;
}

py::list hits_to_py(const Session::Hits& hits) {
    py::list out;
    for (auto& [a, t] : hits) out.append(py::make_tuple(a, t));
    return out;
}

// Session methods accept an address (int) or a name/"0x..." string like the original Python API.
template <typename F>
auto with_ref(Session& s, const py::object& ref, F f) {
    if (py::isinstance<py::int_>(ref)) return f(ref.cast<Addr>());
    std::string text = py::str(ref).cast<std::string>();
    return f(text);
}

std::string opt_str(const py::object& o) { return o.is_none() ? "" : py::str(o).cast<std::string>(); }

}  // namespace

void bind_all(py::module_& m) {
    m.doc() = "REVERSA C++ core";
    m.attr("__version__") = REVERSA_VERSION;
    m.attr("API_VERSION") = "1.0";

    static py::exception<ReversaError> reversa_error(m, "ReversaError");
    static py::exception<LoadError> load_error(m, "LoadError", reversa_error.ptr());
    static py::exception<ProjectError> project_error(m, "ProjectError", reversa_error.ptr());
    static py::exception<AnnotationError> annotation_error(m, "AnnotationError", reversa_error.ptr());
    py::register_exception_translator([](std::exception_ptr p) {
        auto raise = [](py::object& type, const ReversaError& e) {
            py::object inst = type(py::str(e.what()));
            inst.attr("message") = py::str(e.what());
            inst.attr("detail") = py::str(e.detail);
            PyErr_SetObject(type.ptr(), inst.ptr());
        };
        try {
            if (p) std::rethrow_exception(p);
        } catch (const LoadError& e) {
            raise(load_error, e);
        } catch (const ProjectError& e) {
            raise(project_error, e);
        } catch (const AnnotationError& e) {
            raise(annotation_error, e);
        } catch (const ReversaError& e) {
            raise(reversa_error, e);
        }
    });

    m.def("is_project", &is_project, "path"_a);
    m.def("default_project_path", &default_project_path, "binary_path"_a);
    m.def("load_project", [](const std::string& path) {
        ProjectData p = load_project(path);
        return py::dict("name"_a = p.name, "path"_a = p.path, "size"_a = p.data->size(),
                        "load_options"_a = to_py(load_options_json(p.load_options)), "settings"_a = to_py(p.settings));
    }, "path"_a);

    py::class_<Diagnostic>(m, "Diagnostic")
        .def(py::init([](std::string sev, std::string msg, std::optional<uint64_t> loc, std::string where, std::string detail) {
                 return Diagnostic{std::move(sev), std::move(msg), loc, std::move(where), std::move(detail)};
             }),
             "severity"_a, "message"_a, "location"_a = py::none(), "where"_a = "offset", "detail"_a = "")
        .def_readonly("severity", &Diagnostic::severity)
        .def_readonly("message", &Diagnostic::message)
        .def_readonly("location", &Diagnostic::location)
        .def_readonly("where", &Diagnostic::where)
        .def_readonly("detail", &Diagnostic::detail)
        .def("friendly", &Diagnostic::friendly)
        .def("__repr__", [](const Diagnostic& d) { return "<Diagnostic " + d.severity + ": " + d.friendly() + ">"; });

    py::class_<Section>(m, "Section")
        .def_readonly("name", &Section::name)
        .def_readonly("addr", &Section::addr)
        .def_readonly("size", &Section::size)
        .def_readonly("offset", &Section::offset)
        .def_readonly("file_size", &Section::file_size)
        .def_readonly("readable", &Section::readable)
        .def_readonly("writable", &Section::writable)
        .def_readonly("executable", &Section::executable)
        .def_property_readonly("end", &Section::end)
        .def_property_readonly("kind", &Section::kind)
        .def_property_readonly("perms", &Section::perms)
        .def("__repr__", [](const Section& s) { return "<Section " + s.name + " " + format_hex(s.addr) + " " + s.perms() + ">"; });

    py::class_<Symbol>(m, "Symbol")
        .def_readonly("name", &Symbol::name)
        .def_readonly("addr", &Symbol::addr)
        .def_readonly("size", &Symbol::size)
        .def_readonly("kind", &Symbol::kind);

    py::class_<Import>(m, "Import")
        .def_readonly("name", &Import::name)
        .def_readonly("library", &Import::library)
        .def_readonly("slot", &Import::slot)
        .def_readonly("thunk", &Import::thunk)
        .def_readonly("ordinal", &Import::ordinal)
        .def("__repr__", [](const Import& i) { return "<Import " + i.name + " " + i.library + ">"; });

    py::class_<Export>(m, "Export")
        .def_readonly("name", &Export::name)
        .def_readonly("addr", &Export::addr)
        .def_readonly("ordinal", &Export::ordinal)
        .def_readonly("forwarder", &Export::forwarder);

    py::class_<Binary>(m, "Binary")
        .def_readonly("path", &Binary::path)
        .def_readonly("format", &Binary::format)
        .def_readonly("arch", &Binary::arch)
        .def_readonly("bits", &Binary::bits)
        .def_readonly("endian", &Binary::endian)
        .def_readonly("entry", &Binary::entry)
        .def_readonly("base", &Binary::base)
        .def_readonly("compiler", &Binary::compiler)
        .def_readonly("os", &Binary::os)
        .def_readonly("sections", &Binary::sections)
        .def_readonly("symbols", &Binary::symbols)
        .def_readonly("imports", &Binary::imports)
        .def_readonly("exports", &Binary::exports)
        .def_readonly("meta", &Binary::meta)
        .def_readonly("diagnostics", &Binary::diagnostics)
        .def_property_readonly("sha256", &Binary::sha256)
        .def_property_readonly("data", [](const Binary& b) {
            return py::bytes(reinterpret_cast<const char*>(b.data->data()), b.data->size());
        })
        .def("read", [](const Binary& b, Addr a, size_t n) {
            auto v = b.read(a, n);
            return py::bytes(reinterpret_cast<const char*>(v.data()), v.size());
        })
        .def("read_cstring", [](const Binary& b, Addr a) { return b.read_cstring(a); })
        .def("is_mapped", &Binary::is_mapped)
        .def("is_code", &Binary::is_code)
        .def("section_at", &Binary::section_at, py::return_value_policy::reference_internal);

    py::class_<Insn>(m, "Instruction")
        .def_readonly("addr", &Insn::addr)
        .def_readonly("size", &Insn::size)
        .def_readonly("mnemonic", &Insn::mnemonic)
        .def_readonly("op_str", &Insn::op_str)
        .def_readonly("target", &Insn::target)
        .def_property_readonly("address", [](const Insn& i) { return i.addr; })
        .def_property_readonly("text", &Insn::text)
        .def_property_readonly("flow", [](const Insn& i) { return std::string(flow_name(i.flow)); })
        .def_property_readonly("bytes", [](const Insn& i) {
            return py::bytes(reinterpret_cast<const char*>(i.raw.data()), i.raw.size());
        })
        .def_property_readonly("refs", [](const Insn& i) {
            py::list l;
            for (auto& r : i.refs) l.append(py::make_tuple(r.addr, r.kind));
            return l;
        })
        .def("__repr__", [](const Insn& i) { return "<Insn " + format_hex(i.addr) + " " + i.text() + ">"; });

    py::class_<Function>(m, "Function")
        .def_readonly("addr", &Function::addr)
        .def_readonly("kind", &Function::kind)
        .def_readonly("size", &Function::size)
        .def_readonly("insn_count", &Function::insn_count)
        .def_readonly("noreturn", &Function::noreturn)
        .def_readonly("library", &Function::library)
        .def_readonly("callees", &Function::callees)
        .def_readonly("tail_calls", &Function::tail_calls)
        .def_property_readonly("end", &Function::end)
        .def_property_readonly("insn_addrs", [](const Function& f) {
            std::vector<Addr> v;
            for (auto& [a, b] : f.blocks) v.insert(v.end(), b.insns.begin(), b.insns.end());
            return v;
        })
        .def_property_readonly("blocks", [](const Function& f) {
            py::list l;
            for (auto& [a, b] : f.blocks) l.append(py::make_tuple(b.start, b.end));
            return l;
        });

    py::class_<Xref>(m, "Xref")
        .def_readonly("frm", &Xref::frm)
        .def_readonly("to", &Xref::to)
        .def_readonly("kind", &Xref::kind)
        .def_property_readonly("label", &Xref::label);

    py::class_<String>(m, "String")
        .def_readonly("addr", &String::addr)
        .def_readonly("text", &String::text)
        .def_readonly("encoding", &String::encoding)
        .def_readonly("length", &String::length)
        .def("__repr__", [](const String& s) { return "<String " + format_hex(s.addr) + " " + json_quote(s.text) + ">"; });

    py::class_<Analysis>(m, "Analysis")
        .def_property_readonly("functions", [](py::object self) {
            auto& a = self.cast<Analysis&>();
            py::dict d;
            for (auto& [addr, f] : a.functions) d[py::int_(addr)] = py::cast(&f, py::return_value_policy::reference_internal, self);
            return d;
        })
        .def_readonly("main", &Analysis::main)
        .def_readonly("strings", &Analysis::strings)
        .def_readonly("duration", &Analysis::duration)
        .def_readonly("import_addr", &Analysis::import_addr)
        .def_readonly("auto_names", &Analysis::auto_names)
        .def("function", &Analysis::function, py::return_value_policy::reference_internal)
        .def("callers", &Analysis::callers, "addr"_a, "through_thunks"_a = true)
        .def("function_containing", &Analysis::function_containing, py::return_value_policy::reference_internal)
        .def("insn", &Analysis::insn, py::return_value_policy::reference_internal)
        .def("decode_at", &Analysis::decode_at, "addr"_a, "count"_a = 1)
        .def("xrefs_to", [](const Analysis& a, Addr addr) { return a.xrefs.to(addr); })
        .def("xrefs_from", [](const Analysis& a, Addr addr) { return a.xrefs.frm(addr); })
        .def_property_readonly("xref_count", [](const Analysis& a) { return a.xrefs.size(); })
        .def("instruction_addrs", [](const Analysis& a) {
            std::vector<Addr> v;
            for (auto& [ad, i] : a.insns) v.push_back(ad);
            std::sort(v.begin(), v.end());
            return v;
        })
        .def("string_at", [](const Analysis& a, Addr addr) { return a.string(addr); }, py::return_value_policy::reference_internal);

    py::class_<PluginHost, PyPluginHost, std::shared_ptr<PluginHost>>(m, "PluginHost")
        .def(py::init<>());

    py::class_<Session, std::unique_ptr<Session>>(m, "Session")
        .def_static("open", [](const std::string& path, std::optional<std::string> arch, std::optional<Addr> base, bool raw) {
            return Session::open(path, LoadOptions{std::move(arch), base, raw});
        }, "path"_a, "arch"_a = py::none(), "base"_a = py::none(), "raw"_a = false)
        .def_static("open_project", &Session::open_project)
        .def_property_readonly("binary", [](Session& s) -> Binary& { return s.binary; }, py::return_value_policy::reference_internal)
        .def_property_readonly("analysis", [](Session& s) -> Analysis* { return s.analysis.get(); },
                               py::return_value_policy::reference_internal)
        .def("analyze", [](Session& s, std::optional<std::function<void(double, std::string)>> progress) -> Analysis& {
            Progress p;
            if (progress) p = [f = *progress](double x, const std::string& msg) { f(x, msg); };
            py::gil_scoped_release nogil;  // hooks and callbacks re-acquire the GIL themselves
            return s.analyze(p);
        }, "progress"_a = py::none(), py::return_value_policy::reference_internal)
        .def("require_analysis", &Session::require_analysis, py::return_value_policy::reference_internal)
        .def("name_of", &Session::name_of)
        .def("default_name", &Session::default_name)
        .def("has_user_name", &Session::has_user_name)
        .def("resolve", [](Session& s, const py::object& ref) -> std::optional<Addr> {
            if (py::isinstance<py::int_>(ref)) return ref.cast<Addr>();
            return s.resolve(py::str(ref).cast<std::string>());
        })
        .def("rename", [](Session& s, const py::object& ref, const py::object& name) {
            return with_ref(s, ref, [&](auto r) { return s.rename(r, opt_str(name)); });
        })
        .def("rename_var", [](Session& s, const py::object& func, const std::string& key, const py::object& name) {
            with_ref(s, func, [&](auto r) { s.rename_var(r, key, opt_str(name)); return 0; });
        })
        .def("set_comment", [](Session& s, const py::object& ref, const py::object& text) {
            with_ref(s, ref, [&](auto r) { s.set_comment(r, opt_str(text)); return 0; });
        })
        .def("set_bookmark", [](Session& s, const py::object& ref, const py::object& label) {
            with_ref(s, ref, [&](auto r) { s.set_bookmark(r, opt_str(label)); return 0; });
        })
        .def("undo", &Session::undo)
        .def("redo", &Session::redo)
        .def_property_readonly("can_undo", [](Session& s) { return s.undo_stack.can_undo(); })
        .def_property_readonly("can_redo", [](Session& s) { return s.undo_stack.can_redo(); })
        .def("decompile", [](Session& s, const py::object& ref) {
            return lines_to_py(with_ref(s, ref, [&](auto r) { return s.decompile(r); }));
        })
        .def("pseudocode", [](Session& s, const py::object& ref) {
            return to_text(with_ref(s, ref, [&](auto r) { return s.decompile(r); }));
        })
        .def_property_readonly("names", [](Session& s) { return s.annotations.names; })
        .def_property_readonly("comments", [](Session& s) { return s.annotations.comments; })
        .def_property_readonly("bookmarks", [](Session& s) { return s.annotations.bookmarks; })
        .def_property_readonly("var_names", [](Session& s) {
            py::dict d;
            for (auto& [k, v] : s.annotations.var_names) d[py::make_tuple(k.first, k.second)] = v;
            return d;
        })
        .def("search_strings", [](Session& s, const std::string& q, bool regex, size_t min_len) {
            return hits_to_py(s.search_strings(q, regex, min_len));
        }, "query"_a, "regex"_a = false, "min_len"_a = 4)
        .def("search_instructions", [](Session& s, const std::string& q, bool regex) {
            return hits_to_py(s.search_instructions(q, regex));
        }, "query"_a, "regex"_a = true)
        .def("search_pseudocode", [](Session& s, const std::string& q, bool regex) {
            return hits_to_py(s.search_pseudocode(q, regex));
        }, "query"_a, "regex"_a = true)
        .def("search_bytes", &Session::search_bytes)
        .def("add_view", [](Session& s, const std::string& name, std::function<std::string()> render) {
            s.add_view(name, std::move(render));
        })
        .def_property_readonly("view_names", [](Session& s) {
            std::vector<std::string> v;
            for (auto& x : s.views) v.push_back(x.first);
            return v;
        })
        .def("render_view", [](Session& s, const std::string& name) -> std::string {
            for (auto& [n, f] : s.views)
                if (n == name) return f();
            throw AnnotationError("View '" + name + "' does not exist.");
        })
        .def("subscribe", [](Session& s, const std::string& topic, py::function handler) {
            return s.events.subscribe(topic, [handler](const Event& ev) {
                py::gil_scoped_acquire gil;
                py::dict kw;
                for (auto& [k, v] : ev.data) kw[py::str(k)] = event_value(v);
                kw["topic"] = ev.topic;
                handler(**kw);
            });
        })
        .def("unsubscribe", [](Session& s, int id) { s.events.unsubscribe(id); })
        .def_property_readonly("revision", [](Session& s) { return s.events.revision; })
        .def("save", [](Session& s, const py::object& path) { return s.save(opt_str(path)); }, "path"_a = py::none())
        .def_property_readonly("project_path", [](Session& s) { return s.project_path; })
        .def_readwrite("dirty", &Session::dirty)
        .def_property("settings", [](Session& s) { return to_py(s.settings); },
                      [](Session& s, const py::object& o) { s.settings = from_py(o); })
        .def_property_readonly("diagnostics", &Session::diagnostics)
        .def("add_diagnostic", [](Session& s, const std::string& severity, const std::string& message, const std::string& detail) {
            s.runtime_diagnostics.push_back({severity, message, std::nullopt, "address", detail});
        }, "severity"_a, "message"_a, "detail"_a = "")
        .def("set_plugin_host", [](Session& s, std::shared_ptr<PluginHost> host) { s.plugins = std::move(host); })
        .def("report", [](Session& s, const std::string& fmt) { return build_report(s, fmt); }, "fmt"_a = "md")
        .def("summary", [](Session& s) { return to_py(summary(s)); });
}

}  // namespace rv::py_bind
