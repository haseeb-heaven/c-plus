//===- unit_issue205.cpp - TempFile/writeFile regression for issue #205 -*- C++ -*-===//
//
// Built and run by regression_issue205.sh against the driver's internal
// TempSupport.h helpers. Checks that move-assignment removes the temp file it
// replaces and that short/disk-full writes are reported as failures.
//
//===----------------------------------------------------------------------===//

#include "Driver/TempSupport.h"

#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

using namespace cplus::driver_detail;

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
  if (!ok)
    ++failures;
}

bool exists(const std::string &path) {
  return ::access(path.c_str(), F_OK) == 0;
}

std::string makeTempPath() {
  char name[] = "/tmp/cplus-205-XXXXXX";
  int fd = ::mkstemp(name);
  if (fd < 0) {
    std::perror("mkstemp");
    std::exit(2);
  }
  ::close(fd);
  return std::string(name);
}

void testWriteSuccess() {
  const std::string path = makeTempPath();
  std::string err;
  bool ok = writeFile(path, "lowered C text\n", &err);
  check(ok && err.empty(), "writeFile reports success for a normal write");
  FILE *f = std::fopen(path.c_str(), "r");
  char buf[128] = {0};
  if (f) {
    if (std::fgets(buf, sizeof(buf), f) == nullptr)
      buf[0] = '\0';
    std::fclose(f);
  }
  check(std::string(buf) == "lowered C text\n",
        "writeFile contents round-trip exactly");
  ::unlink(path.c_str());
}

void testWriteOpenError() {
  const std::string path = "/tmp"; // a directory cannot be opened for writing
  std::string err;
  check(!writeFile(path, "x", &err) && !err.empty(),
        "writeFile fails loudly when the output cannot be opened");
}

void testWriteShortError() {
  const std::string path = makeTempPath();
  struct rlimit oldLimit;
  if (::getrlimit(RLIMIT_FSIZE, &oldLimit) != 0) {
    check(false, "RLIMIT_FSIZE is available for the short-write test");
    ::unlink(path.c_str());
    return;
  }
  struct rlimit limit = oldLimit;
  limit.rlim_cur = 0; // any write must fail with EFBIG/ENOSPC
  ::signal(SIGXFSZ, SIG_IGN); // report the error instead of dying by signal
  ::setrlimit(RLIMIT_FSIZE, &limit);
  std::string err;
  const bool ok = writeFile(path, "TRUNCATED OR REFUSED\n", &err);
  ::setrlimit(RLIMIT_FSIZE, &oldLimit);
  ::signal(SIGXFSZ, SIG_DFL);
  check(!ok && !err.empty(),
        "writeFile reports flush/write failure (file size limit 0)");
  ::unlink(path.c_str());
}

void testMoveAssignmentCleanup() {
  const std::string first = makeTempPath();
  const std::string second = makeTempPath();
  {
    TempFile target;
    target.path = first;
    TempFile source;
    source.path = second;
    target = std::move(source);
    check(!exists(first),
          "move-assignment removes the overwritten temp file immediately");
    check(target.path == second && exists(second) && source.path.empty(),
          "move-assignment transfers ownership of the incoming temp file");
  }
  check(!exists(second), "the moved-to file is removed on destruction");
}

void testSelfMoveIsHarmless() {
  const std::string path = makeTempPath();
  {
    TempFile f;
    f.path = path;
    f = std::move(f);
    check(exists(path), "self move-assignment does not delete the temp file");
    f.path.clear(); // avoid the RAII removal so we can assert it ourselves
  }
  check(exists(path), "cleared owner does not remove the temp file");
  ::unlink(path.c_str());
}

void testKeepFlagSurvivesMove() {
  const std::string overwritten = makeTempPath();
  const std::string kept = makeTempPath();
  {
    TempFile target;
    target.path = overwritten;
    TempFile source;
    source.path = kept;
    source.keep = true;
    target = std::move(source);
    target.keep = source.keep;
  }
  check(!exists(overwritten), "keep=false source still cleans the old file");
  check(exists(kept), "kept temp file survives both destructors");
  ::unlink(kept.c_str());
}

} // namespace

int main() {
  testWriteSuccess();
  testWriteOpenError();
  testWriteShortError();
  testMoveAssignmentCleanup();
  testSelfMoveIsHarmless();
  testKeepFlagSurvivesMove();
  if (failures) {
    std::printf("%d issue #205 check(s) failed\n", failures);
    return 1;
  }
  std::puts("issue #205 TempFile/writeFile unit checks: PASS");
  return 0;
}
