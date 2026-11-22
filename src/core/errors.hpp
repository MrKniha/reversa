// Error hierarchy: every error carries a friendly message and an optional technical detail.
#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace rv {

class ReversaError : public std::runtime_error {
public:
    explicit ReversaError(const std::string& message, std::string detail = "")
        : std::runtime_error(message), detail(std::move(detail)) {}
    std::string detail;
};
class LoadError : public ReversaError { using ReversaError::ReversaError; };
class ProjectError : public ReversaError { using ReversaError::ReversaError; };
class AnnotationError : public ReversaError { using ReversaError::ReversaError; };
class Cancelled : public ReversaError { using ReversaError::ReversaError; };

// Raised by bounds-checked readers; loaders catch it and degrade gracefully.
class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& message, uint64_t offset)
        : std::runtime_error(message), message(message), offset(offset) {}
    std::string message;
    uint64_t offset;
};

}  // namespace rv
