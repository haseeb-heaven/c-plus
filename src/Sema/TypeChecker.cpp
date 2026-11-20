//===- TypeChecker.cpp - C+ type checking on top of Clang Sema --*- C++ -*-===//

#include "Sema/TypeChecker.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>

using namespace clang;

namespace cplus {

void TypeRegistry::add(ClassInfo info) {
  for (auto &existing : classes_) {
    if (existing.record == info.record) {
      existing = std::move(info);
      return;
    }
  }
  classes_.push_back(std::move(info));
}

const ClassInfo *TypeRegistry::findByRecord(const RecordDecl *rd) const {
  for (const auto &c : classes_)
    if (c.record == rd)
      return &c;
  return nullptr;
}

const ClassInfo *TypeRegistry::findByName(const std::string &name) const {
  for (const auto &c : classes_)
    if (c.name == name)
      return &c;
  return nullptr;
}

namespace {

/// A C+ class/resource is represented in Clang as a C++ record with a
/// `cplus_class` marker attribute.  We detect it by looking for the
/// attribute we attach during parsing.
bool isCPlusRecord(const RecordDecl *rd) {
  return rd && rd->hasAttr<AnnotateAttr>();
}

std::string dropSymbolFor(const std::string &name) { return name + "_drop"; }
std::string ctorSymbolFor(const std::string &name) { return name + "_new"; }

} // namespace

void TypeChecker::run(ASTContext &ctx) {
  SourceManager &sm = ctx.getSourceManager();

  {
    TranslationUnitDecl *tu = ctx.getTranslationUnitDecl();
    for (Decl *d : tu->decls()) {
      auto *rd = dyn_cast<RecordDecl>(d);
      if (!rd || !isCPlusRecord(rd))
        continue;
      if (!rd->isCompleteDefinition())
        continue;

      ClassInfo info;
      info.name = rd->getNameAsString();
      info.record = rd;
      info.dropSymbol = dropSymbolFor(info.name);
      info.ctorSymbol = ctorSymbolFor(info.name);

      // Collect methods.
      if (const auto *cxxrd = dyn_cast<CXXRecordDecl>(rd)) {
        for (const CXXMethodDecl *m : cxxrd->methods()) {
          info.methods.push_back(const_cast<CXXMethodDecl *>(m));
          if (isa<CXXConstructorDecl>(m) &&
              !m->isDeleted()) {
            info.hasConstructor = true;
          }
          if (isa<CXXDestructorDecl>(m)) {
            info.dropSymbol = info.name + "_drop";
          }
        }
      }

      // Determine resource-ness from the marker attribute payload.
      if (const auto *a = rd->getAttr<AnnotateAttr>())
        info.isResource = a->getAnnotation() == "cplus_resource";

      // A resource must have a way to be destroyed.
      if (info.isResource) {
        bool hasDrop = false;
        for (const auto *m : info.methods)
          if (isa<CXXDestructorDecl>(m))
            hasDrop = true;
        if (!hasDrop) {
          SourceLocation loc = sm.getExpansionLoc(rd->getLocation());
          diags_.error("resource '" + info.name +
                           "' must declare a destructor: 'void drop(void)'",
                       sm.getFilename(loc).str(),
                       sm.getExpansionLineNumber(loc),
                       sm.getExpansionColumnNumber(loc));
        }
      }

      registry_.add(std::move(info));
    }
  }
}

} // namespace cplus
