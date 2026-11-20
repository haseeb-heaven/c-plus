//===- SourceManager.cpp - C+ source file management ------------*- C++ -*-===//

#include "SourceManager.h"

#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <sstream>

namespace cplus {

SourceManager::SourceManager() = default;
SourceManager::~SourceManager() = default;

void SourceManager::addVirtualFile(const std::string &name,
                                   const std::string &contents) {
  files_.push_back(name);
  contents_.push_back(contents);
}

bool SourceManager::addFileFromDisk(const std::string &path,
                                    std::string *error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error)
      *error = "cannot open file: " + path;
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  files_.push_back(path);
  contents_.push_back(ss.str());
  return true;
}

const std::string &SourceManager::contents(const std::string &name) const {
  static const std::string kEmpty;
  for (size_t i = 0; i < files_.size(); ++i)
    if (files_[i] == name)
      return contents_[i];
  return kEmpty;
}

} // namespace cplus
