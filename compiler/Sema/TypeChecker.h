//===- TypeChecker.h - C+ type checking on top of Clang Sema ----*- C++ -*-===//
//
// C+ relies on Clang's Sema for all ordinary C type checking.  This file adds
// the C+-specific rules:
//
//   * `resource struct` must have a destructor (either an explicit
//     `cplus_drop_<Name>` function or a declared `void drop(void)` method).
//   * An owning resource value may not be copied implicitly.
//   * A `class` method may not be called on a non-class type.
//
//===----------------------------------------------------------------------===//

#ifndef CPLUS_TYPECHECKER_H
#define CPLUS_TYPECHECKER_H

#include "Diagnostics.h"
#include "LangFeatures.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"

#include <string>
#include <vector>

namespace cplus {

/// Extra information attached to C+ declarations.
struct ClassInfo {
  /// Name of the class as written.
  std::string name;
  /// The underlying record declaration.
  clang::RecordDecl *record = nullptr;
  /// Methods declared inside the class body, in declaration order.
  std::vector<clang::CXXMethodDecl *> methods;
  /// Name of the destructor lowering target, e.g. "Player_drop".
  std::string dropSymbol;
  /// True if the class is a `resource` type.
  bool isResource = false;
  /// True if the class has an explicit constructor.
  bool hasConstructor = false;
  /// Name of the constructor lowering target, e.g. "Player_new".
  std::string ctorSymbol;
};

/// Registry of C+ classes and resources discovered while analysing a
/// translation unit.
class TypeRegistry {
public:
  /// Register a class/resource record.
  void add(ClassInfo info);

  /// Look up a class by record declaration.
  const ClassInfo *findByRecord(const clang::RecordDecl *rd) const;

  /// Look up a class by name.
  const ClassInfo *findByName(const std::string &name) const;

  const std::vector<ClassInfo> &all() const { return classes_; }

private:
  std::vector<ClassInfo> classes_;
};

/// Runs the C+-specific semantic checks over an AST.
class TypeChecker {
public:
  TypeChecker(DiagnosticEngine &diags, Mode mode, OptMode opt)
      : diags_(diags), mode_(mode), opt_(opt) {}

  /// Analyse the translation unit.
  void run(clang::ASTContext &ctx);

  TypeRegistry &registry() { return registry_; }
  const TypeRegistry &registry() const { return registry_; }

private:
  DiagnosticEngine &diags_;
  Mode mode_;
  OptMode opt_;
  TypeRegistry registry_;
};

} // namespace cplus

#endif // CPLUS_TYPECHECKER_H
