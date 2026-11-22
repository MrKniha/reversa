import json
import os
import shutil

from conftest import EXAMPLES, sample

import reversa
from reversa.plugins import API_VERSION


def make_plugin(root, name, code, api=API_VERSION, manifest=None):
    d = root / name
    d.mkdir(parents=True)
    (d / "plugin.json").write_text(manifest if manifest is not None else json.dumps(
        {"name": name, "version": "1.0", "api_version": api, "entry": "plugin.py"}))
    (d / "plugin.py").write_text(code)
    return d


def test_tutorial_plugin_hooks(tmp_path):
    plugdir = tmp_path / "plugins"
    shutil.copytree(os.path.join(EXAMPLES, "plugins", "hello_plugin"), plugdir / "hello_plugin")
    p = reversa.open(sample("login_x64.elf"), plugin_dirs=[str(plugdir)])
    info = p.session.plugins.plugins[0]
    assert info.status == "active" and set(info.hooks) == {"on_load", "on_function_analyzed", "on_rename", "on_unload"}
    assert "hello-plugin" in (p.get_comment(p.main.addr) or "")
    p.rename("check_user", "verify")
    assert "Premenovaní: 1" in p.session.views["Hello plugin"](p)
    p.session.plugins.unload_all()
    assert info.calls > 5


def test_crashing_plugin_is_isolated(tmp_path):
    plugdir = tmp_path / "plugins"
    shutil.copytree(os.path.join(EXAMPLES, "plugins", "crashing_plugin"), plugdir / "crashing_plugin")
    make_plugin(plugdir, "zz_ok", "seen = []\ndef on_function_analyzed(program, func):\n    seen.append(func.addr)\n")
    p = reversa.open(sample("login_x64.elf"), plugin_dirs=[str(plugdir)])
    crash, ok = p.session.plugins.plugins
    assert crash.status == "failed" and "zámerný pád" in crash.error
    assert ok.status == "active" and ok.calls == len(p.functions())
    assert any("spadol" in d.message for d in p.session.diagnostics)
    assert p.main.pseudocode()  # the application keeps working


def test_bad_plugins_never_stop_loading(tmp_path):
    plugdir = tmp_path / "plugins"
    make_plugin(plugdir, "a_syntax", "def broken(:\n")
    make_plugin(plugdir, "b_old_api", "", api="0.1")
    make_plugin(plugdir, "c_bad_json", "", manifest="{not json")
    make_plugin(plugdir, "d_escape", "", manifest=json.dumps({"name": "x", "api_version": "1.0", "entry": "../../evil.py"}))
    make_plugin(plugdir, "e_register_raises", "def register(api):\n    raise ValueError('nope')\n")
    p = reversa.open(sample("hello_x64.elf"), plugin_dirs=[str(plugdir)])
    states = {pl.path.split(os.sep)[-1]: pl.status for pl in p.session.plugins.plugins}
    assert states == {"a_syntax": "failed", "b_old_api": "incompatible", "c_bad_json": "failed",
                      "d_escape": "failed", "e_register_raises": "failed"}
    assert len(p.functions()) > 5
    assert all(pl.as_dict()["status"] for pl in p.session.plugins.plugins)


def test_crypto_plugin_and_env_path(tmp_path, monkeypatch):
    blob = bytes.fromhex("637c777bf26b6fc53001672bfed7ab76")
    target = tmp_path / "aes.bin"
    target.write_bytes(b"\x90" * 64 + blob + b"\xc3" * 16)
    monkeypatch.setenv("REVERSA_PLUGIN_PATH", os.path.join(EXAMPLES, "plugins"))
    p = reversa.open(str(target), arch="x86-64")
    assert "AES S-box" in p.session.views["Krypto konštanty"](p)
    assert any("krypto" in v for v in p.session.annotations.bookmarks.values())


def test_no_plugins_flag():
    p = reversa.open(sample("hello_x64.elf"), load_plugins=False, plugin_dirs=[os.path.join(EXAMPLES, "plugins")])
    assert p.session.plugins.plugins == []
