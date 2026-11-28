//===- SemaCPlus.cpp - C+ frontend entry points ------------------*- C++ -*-===//
//
// Assembles the pieces of the C+ frontend: the source rewriter and the
// Clang argument construction.
//

#include "Sema/SemaCPlus.h"

#include "Sema/CPlusRewriter.h"

namespace cplus {

RewrittenSource rewriteCPlusSyntax(const std::string &source,
                                   DiagnosticEngine &diags,
                                   const std::string &filename) {
  return rewriteCPlusSource(source, diags, filename);
}

std::vector<std::string> buildClangArgs(const FrontendOptions &opts,
                                         const std::string &file) {
  std::vector<std::string> args;
  args.push_back("-x");
  args.push_back("c");
  args.push_back("-std=" + opts.stdVersion);
  args.push_back("-fno-common");

  if (opts.mode == Mode::Safe)
    args.push_back("-fcplus-safe");
  else
    args.push_back("-fcplus-legacy");

  if (opts.opt == OptMode::Release) {
    args.push_back("-O2");
  } else {
    args.push_back("-O0");
    args.push_back("-g");
  }

  const bool promoteWarnings =
      opts.warningsAsErrors && (opts.mode != Mode::LegacyC ||
                                opts.warningsAsErrorsExplicit);
  if (promoteWarnings)
    args.push_back("-Werror");

  for (const auto &d : opts.defines)
    args.push_back("-D" + d);
  for (const auto &d : opts.undefs)
    args.push_back("-U" + d);
  for (const auto &d : opts.includeDirs)
    args.push_back("-I" + d);
  for (const auto &a : opts.extraArgs)
    args.push_back(a);

  if (!opts.sysroot.empty())
    args.push_back("-isysroot");
  if (!opts.sysroot.empty())
    args.push_back(opts.sysroot);

  args.push_back(file);
  return args;
}

} // namespace cplus
