//===- Diagnostics.cpp - C+ diagnostic reporting ----------------*- C++ -*-===//

#include "Diagnostics.h"

#include <algorithm>

namespace cplus {

void DiagnosticEngine::report(Diag d) {
  // Macro expansions can visit the same expression several times; emit the
  // identical diagnostic once so users don't see duplicated errors.
  for (const auto &prev : diags_)
    if (prev.level == d.level && prev.message == d.message &&
        prev.file == d.file && prev.line == d.line &&
        prev.column == d.column)
      return;
  switch (d.level) {
  case DiagLevel::Error:
  case DiagLevel::Fatal:
    ++error_count_;
    break;
  case DiagLevel::Warning:
    ++warning_count_;
    break;
  case DiagLevel::Note:
    break;
  }
  diags_.push_back(std::move(d));
}

void DiagnosticEngine::error(const std::string &msg, const std::string &file,
                              unsigned line, unsigned column) {
  Diag d;
  d.level = DiagLevel::Error;
  d.message = msg;
  d.file = file;
  d.line = line;
  d.column = column;
  report(std::move(d));
}

void DiagnosticEngine::warning(const std::string &msg, const std::string &file,
                                unsigned line, unsigned column) {
  Diag d;
  d.level = DiagLevel::Warning;
  d.message = msg;
  d.file = file;
  d.line = line;
  d.column = column;
  report(std::move(d));
}

void DiagnosticEngine::note(const std::string &msg, const std::string &file,
                             unsigned line, unsigned column) {
  Diag d;
  d.level = DiagLevel::Note;
  d.message = msg;
  d.file = file;
  d.line = line;
  d.column = column;
  report(std::move(d));
}

} // namespace cplus
