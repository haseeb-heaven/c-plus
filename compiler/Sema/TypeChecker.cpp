//===- TypeChecker.cpp - C+ type checking on top of Clang Sema --*- C++ -*-===//

#include "Sema/TypeChecker.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

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

/// Only these exact annotation payloads mark a lowered C+ record.  An
/// unrelated `annotate("...")` (from user code, a tool or another attribute
/// on the same record) is not a class/resource marker.
bool isCPlusMarker(const AnnotateAttr *a) {
  if (!a)
    return false;
  const StringRef payload = a->getAnnotation();
  return payload == "cplus_class" || payload == "cplus_resource";
}

bool isCPlusRecord(const RecordDecl *rd) {
  if (!rd)
    return false;
  for (const auto *a : rd->specific_attrs<AnnotateAttr>())
    if (isCPlusMarker(a))
      return true;
  return false;
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

      // Determine resource-ness from any exact `cplus_resource` marker.
      // A different annotation listed first must not hide the marker.
      for (const auto *a : rd->specific_attrs<AnnotateAttr>()) {
        if (a->getAnnotation() == "cplus_resource") {
          info.isResource = true;
          break;
        }
      }

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

// ---------------------------------------------------------------------------
// Issue #184: static validation of erased spatial contracts.
//
// The syntax rewriter erases `@notnull` / `@zeroterm` / `@numelts(...)` before
// Clang sees the translation unit and emits dynamic guards at each annotated
// dereference.  This pass consumes the serialized contracts and checks call
// sites in the original source, where the annotation spellings are still
// available: passing a null literal through a non-null contract is a proven
// error and must not be deferred to the runtime hook.
// ---------------------------------------------------------------------------

namespace {

struct SpatialContract {
  size_t param = 0;
  std::string pointer;
  std::string kind;
  std::string lengthExpr;
  unsigned line = 0;
  unsigned column = 0;
};

std::vector<std::string> splitPipe(const std::string &s) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == '|') {
      out.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

std::string trimSpace(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

bool isNullLiteral(const std::string &raw) {
  const std::string s = trimSpace(raw);
  if (s == "NULL" || s == "nullptr" || s == "0")
    return true;
  if (s == "(void*)0" || s == "(void *)0")
    return true;
  return false;
}

bool identStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool identCont(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // namespace

void checkAnnotatedCallSites(const std::string &source,
                             const std::vector<std::string> &contracts,
                             DiagnosticEngine &diags,
                             const std::string &filename) {
  std::map<std::string, std::vector<SpatialContract>> byFunction;
  for (const auto &line : contracts) {
    std::vector<std::string> parts = splitPipe(line);
    // F|function|paramIndex|pointer|kind|lengthExpr|line|column
    if (parts.size() < 8 || parts[0] != "F")
      continue;
    SpatialContract c;
    c.param = static_cast<size_t>(std::strtoul(parts[2].c_str(), nullptr, 10));
    c.pointer = parts[3];
    c.kind = parts[4];
    c.lengthExpr = parts[5];
    c.line = static_cast<unsigned>(std::strtoul(parts[6].c_str(), nullptr, 10));
    c.column =
        static_cast<unsigned>(std::strtoul(parts[7].c_str(), nullptr, 10));
    byFunction[parts[1]].push_back(c);
  }
  if (byFunction.empty())
    return;

  auto reportNull = [&](const SpatialContract &c, const std::string &fn,
                        unsigned line, unsigned column) {
    Diag d;
    d.level = DiagLevel::Error;
    d.message = "[CPLUS-SPATIAL] null passed to @" + c.kind +
                " parameter '" + c.pointer + "' of '" + fn + "'";
    d.file = filename;
    d.line = line;
    d.column = column;
    d.help = "pass a non-null pointer; the annotation is erased in emitted C "
             "but checked at compile time";
    diags.report(std::move(d));
  };

  const size_t n = source.size();
  size_t i = 0;
  unsigned line = 1, column = 1;
  auto advance = [&](size_t count) {
    for (size_t k = 0; k < count && i < n; ++k, ++i) {
      if (source[i] == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
    }
  };

  while (i < n) {
    char c = source[i];
    if (c == '/' && i + 1 < n && source[i + 1] == '/') {
      while (i < n && source[i] != '\n')
        advance(1);
      continue;
    }
    if (c == '/' && i + 1 < n && source[i + 1] == '*') {
      advance(2);
      while (i < n && !(source[i] == '*' && i + 1 < n && source[i + 1] == '/'))
        advance(1);
      if (i < n)
        advance(2);
      continue;
    }
    if (c == '"' || c == '\'') {
      const char quote = c;
      advance(1);
      while (i < n && source[i] != quote) {
        if (source[i] == '\\' && i + 1 < n)
          advance(2);
        else
          advance(1);
      }
      if (i < n)
        advance(1);
      continue;
    }
    if (!identStart(c)) {
      advance(1);
      continue;
    }

    const size_t nameStart = i;
    const unsigned callLine = line, callColumn = column;
    while (i < n && identCont(source[i]))
      advance(1);
    const std::string name = source.substr(nameStart, i - nameStart);

    size_t peek = i;
    while (peek < n && std::isspace(static_cast<unsigned char>(source[peek])))
      ++peek;
    if (peek >= n || source[peek] != '(' || name == "if" || name == "while" ||
        name == "for" || name == "switch" || name == "return" ||
        name == "sizeof") {
      continue;
    }

    auto fit = byFunction.find(name);
    if (fit == byFunction.end()) {
      continue;
    }

    // Collect top-level arguments without duplicating the C parser: nesting,
    // comments and literals are skipped, commas at depth zero split args.
    std::vector<std::string> args;
    size_t argStart = peek + 1;
    int depth = 0;
    size_t j = peek + 1;
    bool closed = false;
    while (j < n) {
      const char d = source[j];
      if (d == '"' || d == '\'') {
        const char quote = d;
        ++j;
        while (j < n && source[j] != quote) {
          if (source[j] == '\\' && j + 1 < n)
            j += 2;
          else
            ++j;
        }
        if (j < n)
          ++j;
        continue;
      }
      if (d == '/' && j + 1 < n && source[j + 1] == '*') {
        j += 2;
        while (j + 1 < n && !(source[j] == '*' && source[j + 1] == '/'))
          ++j;
        j = (j + 1 < n) ? j + 2 : n;
        continue;
      }
      if (d == '(' || d == '[' || d == '{')
        ++depth;
      else if (d == ')' || d == ']' || d == '}') {
        if (d == ')' && depth == 0) {
          args.push_back(source.substr(argStart, j - argStart));
          closed = true;
          ++j;
          break;
        }
        --depth;
      } else if (d == ',' && depth == 0) {
        args.push_back(source.substr(argStart, j - argStart));
        argStart = j + 1;
      }
      ++j;
    }
    if (!closed)
      continue;
    i = j;
    column = 1; // advance() would need a re-scan; recount below instead.
    // Restore exact line/column after jumping over the argument list.
    line = callLine;
    column = callColumn;
    for (size_t k = nameStart; k < j; ++k) {
      if (source[k] == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
    }

    for (const auto &c : fit->second) {
      if (c.param >= args.size())
        continue;
      if (isNullLiteral(args[c.param]))
        reportNull(c, name, callLine, callColumn);
    }
  }
}

// ---------------------------------------------------------------------------
// Issue #185: static validation of `@tagged` union variant accesses.
//
// CPlusRewriter erases `@tagged` and emits a side tag plus a runtime trap at
// every member access.  This pass consumes the serialized facts and rejects an
// access whose active variant is provably wrong in the same straight-line
// block.  Reads in a different block (if/else branches) are left to the
// runtime trap, so valid branch-dependent code is never rejected.
// Facts: D|union|f1,f2 / V|var|union|line|col|block /
//        S|var|field|line|col|block / R|var|field|line|col|block
// ---------------------------------------------------------------------------

namespace {

struct TaggedFact {
  char kind = 0;
  std::string subject;
  std::string detail;
  unsigned line = 1;
  unsigned column = 1;
  std::string block;
};

std::vector<std::string> taggedSplit(const std::string &s) {
  return splitPipe(s);
}

} // namespace

void checkTaggedVariantAccess(const std::string &source,
                              const std::vector<std::string> &facts,
                              DiagnosticEngine &diags,
                              const std::string &filename) {
  (void)source; // facts carry original source locations verbatim
  struct Active {
    std::string field;
    std::string unionName;
    unsigned line = 0;
    unsigned column = 0;
  };
  std::map<std::string, Active> active; // key: block + "::" + variable
  std::set<std::string> reported;

  for (const auto &raw : facts) {
    std::vector<std::string> p = taggedSplit(raw);
    if (p.empty() || p[0].size() != 1)
      continue;
    TaggedFact f;
    f.kind = p[0][0];
    if (f.kind == 'D')
      continue;
    if (p.size() < 6)
      continue;
    f.subject = p[1];
    f.detail = p[2];
    f.line = static_cast<unsigned>(std::strtoul(p[3].c_str(), nullptr, 10));
    f.column = static_cast<unsigned>(std::strtoul(p[4].c_str(), nullptr, 10));
    f.block = p[5];
    const std::string key = f.block + "::" + f.subject;

    if (f.kind == 'V') {
      // A fresh declaration in this block starts with no active variant.
      active.erase(key);
      continue;
    }
    if (f.kind == 'S') {
      Active &a = active[key];
      a.field = f.detail;
      a.line = f.line;
      a.column = f.column;
      continue;
    }
    if (f.kind != 'R')
      continue;
    auto it = active.find(key);
    if (it == active.end() || it->second.field.empty() ||
        it->second.field == f.detail)
      continue;
    const std::string id = key + "::" + f.detail + "::" + it->second.field;
    if (!reported.insert(id).second)
      continue;
    Diag d;
    d.level = DiagLevel::Error;
    d.message = "[CPLUS-VARIANT] member '" + f.subject + "." + f.detail +
                "' is not the active variant (active: '" + f.subject + "." +
                it->second.field + "')";
    d.file = filename;
    d.line = f.line;
    d.column = f.column;
    d.help = "assign the member before reading it; the variant is tracked "
             "and trapped at runtime when it cannot be proven statically";
    diags.report(std::move(d));
  }
}

} // namespace cplus
