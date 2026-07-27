#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "lang/source_location.h"

namespace ce::lang {

enum class DiagnosticSeverity { Error, Warning };

// Numbered diagnostic codes -- CEL1xxx = lexer/parser (syntax) errors.
// Later phases add their own ranges (CEL2xxx semantic, CEL9xxx
// runtime) without renumbering these; kept as one enum rather than
// per-phase enums so a code's number stays stable even if a later
// refactor moves which phase actually detects it.
enum class DiagCode {
    UnterminatedString = 1001,
    UnterminatedBlockComment = 1002,
    InvalidCharacter = 1003,
    SyntaxError = 1101,
};

struct Diagnostic {
    DiagCode code;
    DiagnosticSeverity severity;
    SourceLocation loc;
    std::string message;
};

class DiagnosticEngine {
public:
    void Report(DiagCode code, DiagnosticSeverity severity, SourceLocation loc, std::string message);

    bool HasErrors() const;
    const std::vector<Diagnostic>& Diagnostics() const { return diagnostics_; }

    void PrintAll(std::ostream& out) const;

private:
    std::vector<Diagnostic> diagnostics_;
};

} // namespace ce::lang
