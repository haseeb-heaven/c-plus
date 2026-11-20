//===- SourceManager.h - C+ source file management --------------*- C++ -*-===//

#ifndef CPLUS_SOURCEMANAGER_H
#define CPLUS_SOURCEMANAGER_H

#include <memory>
#include <string>
#include <vector>

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace clang {
class ASTContext;
class CompilerInstance;
class SourceManager;
} // namespace clang

namespace cplus {

/// Owns the files read during a compilation and maps them into Clang's
/// SourceManager so that diagnostics can be printed with correct file/line
/// information even for content that did not exist on disk.
class SourceManager {
public:
  SourceManager();
  ~SourceManager();

  /// Register a virtual file with the given contents.  Used for compiler
  /// generated buffers (e.g. the lowered C+ runtime shims).
  void addVirtualFile(const std::string &name, const std::string &contents);

  /// Read a file from disk into a virtual buffer.
  bool addFileFromDisk(const std::string &path, std::string *error);

  /// All registered files, in registration order.
  const std::vector<std::string> &files() const { return files_; }

  /// Raw contents of a registered file (empty if unknown).
  const std::string &contents(const std::string &name) const;

  clang::SourceManager &clangSourceManager() { return *sm_; }

private:
  std::vector<std::string> files_;
  std::vector<std::string> contents_;
  clang::SourceManager *sm_ = nullptr;
};

} // namespace cplus

#endif // CPLUS_SOURCEMANAGER_H
