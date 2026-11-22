"""Exceptions with user-facing (Slovak) messages and a technical detail."""


class ReversaError(Exception):
    """Base error. ``message`` is shown to the user, ``detail`` is for bug reports."""

    def __init__(self, message: str, detail: str = ""):
        super().__init__(message)
        self.message = message
        self.detail = detail


class LoadError(ReversaError):
    pass


class ProjectError(ReversaError):
    pass


class AnnotationError(ReversaError):
    pass


class Cancelled(ReversaError):
    def __init__(self) -> None:
        super().__init__("Operácia bola zrušená používateľom.")
