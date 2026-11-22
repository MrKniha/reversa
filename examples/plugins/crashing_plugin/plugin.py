"""Intentionally broken plugin used by tests and docs/plugin-tutorial.md."""


def on_function_analyzed(program, func):
    raise RuntimeError(f"zámerný pád pri funkcii {func.name}")
