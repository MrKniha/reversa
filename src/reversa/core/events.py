"""Tiny synchronous publish/subscribe bus.

Every change to annotations (rename, comment, bookmark, ...) is published here so
that all views, plugins and the GUI server stay consistent. A failing subscriber
is isolated: its exception is reported through the ``on_error`` callback and the
remaining subscribers still run.
"""
from __future__ import annotations

from collections import defaultdict
from typing import Any, Callable

Handler = Callable[..., Any]


class EventBus:
    def __init__(self, on_error: Callable[[str, Handler, BaseException], None] | None = None):
        self._subs: dict[str, list[Handler]] = defaultdict(list)
        self._on_error = on_error
        self.revision = 0  # increments on every published event; views use it to refresh

    def subscribe(self, topic: str, handler: Handler) -> Callable[[], None]:
        self._subs[topic].append(handler)
        return lambda: self._subs[topic].remove(handler) if handler in self._subs[topic] else None

    def publish(self, topic: str, **payload: Any) -> None:
        self.revision += 1
        for handler in list(self._subs.get(topic, ())) + list(self._subs.get("*", ())):
            try:
                handler(topic=topic, **payload)
            except Exception as exc:  # isolation: one bad listener must not break others
                if self._on_error:
                    self._on_error(topic, handler, exc)
