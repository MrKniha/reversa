import json
import threading
import time
import urllib.error
import urllib.request

import pytest
from conftest import sample

from reversa.ui.server import Api, GuiState, create_server, parse_addr
from reversa.core.errors import ReversaError


def wait_ready(state, timeout=60):
    t0 = time.time()
    while state.status not in ("ready", "error") and time.time() - t0 < timeout:
        time.sleep(0.05)
    return state.status


@pytest.fixture(scope="module")
def ready_api():
    st = GuiState({"no_plugins": True})
    st.open(sample("login_x64.elf"))
    assert wait_ready(st) == "ready"
    return Api(st)


def test_state_and_tables(ready_api):
    api = ready_api
    s = api.state()
    assert s["status"] == "ready" and s["file"]["format"] == "ELF" and s["file"]["main"]
    fn = api.functions()
    assert any(f["name"] == "main" and f["main"] for f in fn)
    assert any(x["text"] == "admin" and x["xrefs"] == 1 for x in api.strings())
    assert any(i["name"] == "strcmp" and i["callers"] == 2 for i in api.imports())
    assert api.sections() and isinstance(api.exports(), list) and api.relocations()
    assert api.hexdump({"addr": s["file"]["entry"], "len": "64"})["rows"]


def test_listing_cfg_callgraph_pseudocode(ready_api):
    api = ready_api
    main = api.state()["file"]["main"]
    lst = api.listing({"addr": main, "count": "80"})
    assert lst["lines"][0]["type"] in ("function", "section") and any(l.get("mn") == "call" for l in lst["lines"])
    calls = [l for l in lst["lines"] if l.get("target")]
    assert any(c["target"]["name"] == "check_user" for c in calls)
    up = api.listing({"addr": main, "count": "20", "dir": "up"})
    assert up["lines"]
    data = api.listing({"addr": hex(api.a.strings[0].addr), "count": "10"})
    assert any(l.get("kind") == "string" for l in data["lines"])
    cfg = api.cfg({"addr": main})
    assert len(cfg["nodes"]) >= 6 and cfg["edges"]
    cg = api.callgraph({"root": main, "depth": "1"})
    assert {n["name"] for n in cg["nodes"]} >= {"main", "check_user", "check_password", "puts"}
    full = api.callgraph({"depth": "0", "imports": "0"})
    assert all(n["kind"] == "normal" for n in full["nodes"])
    pc = api.pseudocode({"addr": main})
    text = "".join(s[0] for ln in pc["lines"] for s in ln["segs"])
    assert "check_user(" in text
    info = api.addrinfo({"addr": main})
    assert info["kind"] == "function" and info["callees"]
    with pytest.raises(ReversaError):
        api.cfg({"addr": hex(api.a.import_addr["puts"])})


def test_search_and_report(ready_api):
    api = ready_api
    assert api.search({"kind": "string", "q": "admin"})
    assert api.search({"kind": "hex", "q": "f3 0f 1e fa"})
    assert api.search({"kind": "insn", "q": "call", "regex": "1"})
    assert api.search({"kind": "code", "q": "strcmp", "regex": "1"})
    assert api.search({"kind": "name", "q": "check"})
    assert api.search({"kind": "string", "q": ""}) == []
    assert "# Analýza" in api.report({"fmt": "md"})["text"]
    with pytest.raises(ReversaError):
        api.view({"name": "nope"})


def test_mutations(ready_api, tmp_path):
    api = ready_api
    main = api.state()["file"]["main"]
    assert api.post("rename", {"addr": main, "name": "entry_logic"})["name"] == "entry_logic"
    assert any(f["name"] == "entry_logic" and f["user"] for f in api.functions())
    api.post("comment", {"addr": main, "text": "ahoj"})
    api.post("bookmark", {"addr": main, "label": "tu"})
    assert len(api.bookmarks()) == 2
    assert api.post("undo", {})["what"]
    assert api.post("redo", {})["what"]
    api.post("rename_var", {"func": main, "key": "s:-4", "name": "argc"})
    api.post("settings", {"settings": {"layout": "x"}})
    path = api.post("save", {"path": str(tmp_path / "s.rvproj")})["path"]
    assert path.endswith("s.rvproj")
    with pytest.raises(ReversaError):
        api.post("rename", {"addr": main, "name": "bad name"})
    with pytest.raises(ReversaError):
        api.post("unknown", {})
    with pytest.raises(ReversaError):
        api.post("open", {"path": str(tmp_path / "missing")})
    for _ in range(4):
        api.post("undo", {})


def test_parse_addr():
    assert parse_addr("0x10") == 16 and parse_addr(5) == 5 and parse_addr("20") == 20
    with pytest.raises(ReversaError):
        parse_addr("hello")


def test_http_server_security_and_flow(tmp_path):
    httpd, state, url, token = create_server(sample("hello_x64.elf"), 0, {"no_plugins": True})
    th = threading.Thread(target=httpd.serve_forever, daemon=True)
    th.start()
    base = url.split("/?")[0]
    try:
        def get(path, tok=token, host=None):
            req = urllib.request.Request(base + path, headers={"X-Reversa-Token": tok} if tok else {})
            if host:
                req.add_header("Host", host)
            with urllib.request.urlopen(req, timeout=10) as r:
                return r.status, r.read()
        assert get("/")[0] == 200 and b"REVERSA" in get("/")[1]
        assert get("/app.js")[0] == 200 and get("/i18n/sk.json")[0] == 200
        with pytest.raises(urllib.error.HTTPError) as e:
            get("/api/state", tok="wrong")
        assert e.value.code == 403
        with pytest.raises(urllib.error.HTTPError) as e:
            get("/api/state", host="evil.example")
        assert e.value.code == 403
        with pytest.raises(urllib.error.HTTPError) as e:
            get("/../../etc/passwd")
        assert e.value.code == 404
        assert wait_ready(state) == "ready"
        status, body = get("/api/functions")
        assert status == 200 and any(f["name"] == "sum_to" for f in json.loads(body))
        with pytest.raises(urllib.error.HTTPError) as e:
            get("/api/listing?addr=0x1")
        assert e.value.code == 400 and "sekcii" in json.loads(e.value.read())["error"]["message"]
        assert get("/docs/glossary.md")[0] in (200,)
        # upload a new file through the API
        data = open(sample("login_arm64.elf"), "rb").read()
        req = urllib.request.Request(base + "/api/upload", data=data, method="POST",
                                     headers={"X-Reversa-Token": token, "X-Filename": "login_arm64.elf"})
        assert json.loads(urllib.request.urlopen(req, timeout=10).read())["ok"]
        time.sleep(0.1)
        assert wait_ready(state) == "ready"
        assert json.loads(get("/api/state")[1])["file"]["arch"] == "arm64"
        req = urllib.request.Request(base + "/api/rename", data=json.dumps({"addr": "0xdeadbeef00", "name": "x"}).encode(),
                                     method="POST", headers={"X-Reversa-Token": token})
        with pytest.raises(urllib.error.HTTPError) as e:
            urllib.request.urlopen(req, timeout=10)
        assert e.value.code == 400
        req = urllib.request.Request(base + "/api/demo", data=b"{}", method="POST", headers={"X-Reversa-Token": token})
        assert json.loads(urllib.request.urlopen(req, timeout=10).read())["ok"]
        assert wait_ready(state) == "ready"
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_cancel_and_broken_file(tmp_path):
    st = GuiState({"no_plugins": True})
    bad = tmp_path / "broken.exe"
    bad.write_bytes(b"MZ" + b"\x00" * 58 + b"\x40\x00\x00\x00" + b"PE\x00\x00" + b"\xff" * 30)
    st.open(str(bad))
    assert wait_ready(st) == "ready"
    assert any(d["severity"] == "error" for d in Api(st).state()["diagnostics"])
    st.open(sample("hello_x64.exe"))
    st.cancel.set()
    wait_ready(st)
    empty = GuiState()
    with pytest.raises(ReversaError):
        Api(empty).functions()
    assert Api(empty).state()["status"] == "empty"
