"""User annotations: names, comments, bookmarks, variable names."""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class Annotations:
    names: dict[int, str] = field(default_factory=dict)
    comments: dict[int, str] = field(default_factory=dict)
    bookmarks: dict[int, str] = field(default_factory=dict)
    var_names: dict[tuple[int, str], str] = field(default_factory=dict)
