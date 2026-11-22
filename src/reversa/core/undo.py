"""Command-pattern undo/redo stack used for every user edit."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Callable


@dataclass
class Command:
    description: str
    do: Callable[[], None]
    undo: Callable[[], None]


class UndoStack:
    def __init__(self, limit: int = 1000):
        self._done: list[Command] = []
        self._undone: list[Command] = []
        self.limit = limit

    def push(self, cmd: Command) -> None:
        cmd.do()
        self._done.append(cmd)
        if len(self._done) > self.limit:
            self._done.pop(0)
        self._undone.clear()

    def undo(self) -> str | None:
        if not self._done:
            return None
        cmd = self._done.pop()
        cmd.undo()
        self._undone.append(cmd)
        return cmd.description

    def redo(self) -> str | None:
        if not self._undone:
            return None
        cmd = self._undone.pop()
        cmd.do()
        self._done.append(cmd)
        return cmd.description

    @property
    def can_undo(self) -> bool:
        return bool(self._done)

    @property
    def can_redo(self) -> bool:
        return bool(self._undone)

    def clear(self) -> None:
        self._done.clear()
        self._undone.clear()
