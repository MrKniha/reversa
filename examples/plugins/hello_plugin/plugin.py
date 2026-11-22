"""Ukážkový plugin pre REVERSA (pozri docs/plugin-tutorial.md)."""


class HelloPlugin:
    def __init__(self):
        self.functions = 0
        self.renames = []

    def on_load(self, program):
        print(f"[hello-plugin] načítaný súbor: {program.path}")

    def on_function_analyzed(self, program, func):
        self.functions += 1
        if func.name == "main":
            program.comment(func.addr, "hello-plugin: tu začína program")

    def on_rename(self, program, addr, old, new):
        self.renames.append((addr, old, new))
        print(f"[hello-plugin] {old} -> {new}")

    def on_unload(self, program):
        print(f"[hello-plugin] videl som {self.functions} funkcií")


def register(api):
    plugin = HelloPlugin()
    api.add_view("Hello plugin", lambda p: f"Analyzovaných funkcií: {plugin.functions}\nPremenovaní: {len(plugin.renames)}")
    return plugin
