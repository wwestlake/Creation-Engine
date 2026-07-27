#pragma once

#include "lang/ast.h"
#include "lang/diagnostics.h"

namespace ce::lang {

// Type-checks and validates a parsed Program in place: resolves every
// Expr::type, Stmt::resolvedType (VarDecl), and GlobalVarDecl::resolvedType;
// validates scoping, function signatures/arity, operator/assignment type
// rules, control flow (return-on-all-paths, break/continue-in-loop-only),
// and the "string literal only as an intrinsic argument" rule. Reports
// CEL20xx diagnostics into `diagnostics` -- doesn't stop at the first
// error, so a single celc invocation surfaces as many real problems as
// it can find in one pass, the same way the parser already does.
//
// Returns true iff the program is well-typed (equivalent to
// !diagnostics.HasErrors() after the call, but lets a caller avoid
// needing to know that detail).
bool AnalyzeProgram(Program& program, DiagnosticEngine& diagnostics);

} // namespace ce::lang
