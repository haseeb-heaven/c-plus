//===- Diagnostics.h - C+ diagnostic reporting -------------------*- C++ -*-===//
//
// Structured diagnostics for the C+ compiler.  Diagnostics are produced by
// the analysis passes and rendered by the CLI.
//
//===----------------------------------------------------------------------===//

#ifndef CPLUS_DIAGNOSTICS_H
#define CPLUS_DIAGNOSTICS_H

#include <string>
#include <vector>

namespace cplus {

enum class DiagLevel { Note, Warning, Error, Fatal };

/// A single diagnostic message.
struct Diag {
  DiagLevel level = DiagLevel::Error;
  std::string message;      ///< primary message text
  std::string file;         ///< primary file (may be empty)
  unsigned line = 0;        ///< 1-based line
  unsigned column = 0;      ///< 1-based column
  std::string help;         ///< optional "help:" line

  /// Ordered list of notes attached to this diagnostic.
  std::vector<Diag> notes;
};

/// Collects diagnostics produced during a compilation.
class DiagnosticEngine {
public:
  void report(Diag d);
  void error(const std::string &msg, const std::string &file = {},
             unsigned line = 0, unsigned column = 0);
  void warning(const std::string &msg, const std::string &file = {},
               unsigned line = 0, unsigned column = 0);
  void note(const std::string &msg, const std::string &file = {},
            unsigned line = 0, unsigned column = 0);

  bool hasErrors() const { return error_count_ > 0; }
  unsigned errorCount() const { return error_count_; }
  unsigned warningCount() const { return warning_count_; }
  const std::vector<Diag> &diags() const { return diags_; }

  void clear() {
    diags_.clear();
    error_count_ = 0;
    warning_count_ = 0;
  }

private:
  std::vector<Diag> diags_;
  unsigned error_count_ = 0;
  unsigned warning_count_ = 0;
};

} // namespace cplus

#endif // CPLUS_DIAGNOSTICS_H
