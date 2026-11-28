//===- ownership_issue_227_probe.cpp - issue #227 direct API probe --*- C++ -*-===//
//
// Drives the two OwnershipAnalysis entry points named in issue #227 on one
// fixture and prints a stable, line-oriented record of what they saw:
//
//   parsed=<0|1>
//   unverified=<name>   // SafetyAstProbe::unverifiedPointerExterns
//   header=<name>       // SafetyAstProbe::headerDeclaredExterns
//   unresolved=<name>   // SafetyAstProbe::unresolvedFunctions
//   helper=<name>       // findUserDeclaredHelperSymbols()
//
// Names are sorted and de-duplicated so the output is deterministic. This
// probe is compiled against a FRESHLY built OwnershipAnalysis.cpp (see
// run_suite.py), so a stale object inside libcplus_core.a cannot mask either
// half of the fix.
//
//===----------------------------------------------------------------------===//

#include "Analysis/OwnershipAnalysis.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

static void printNames(const char *tag, std::vector<std::string> names) {
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  for (const std::string &name : names)
    std::cout << tag << "=" << name << "\n";
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: ownership_issue_227_probe <fixture.c>\n";
    return 2;
  }
  std::ifstream input(argv[1]);
  if (!input) {
    std::cerr << "cannot open fixture: " << argv[1] << "\n";
    return 2;
  }
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());

  // The parse-relevant args mirror what the driver passes: C mode plus an
  // include path so quoted user headers in cases/ resolve regardless of cwd.
  std::string dir = argv[1];
  const size_t slash = dir.find_last_of('/');
  if (slash == std::string::npos)
    dir = ".";
  else
    dir.resize(slash);
  const std::vector<std::string> args = {"-std=c11", "-I" + dir};

  cplus::SafetyAstProbe probe = cplus::probeSafetyAst(source, args, argv[1]);
  std::cout << "parsed=" << (probe.parsed ? 1 : 0) << "\n";
  printNames("unverified", probe.unverifiedPointerExterns);
  printNames("header", probe.headerDeclaredExterns);
  printNames("unresolved", probe.unresolvedFunctions);

  const std::set<std::string> helpers =
      cplus::findUserDeclaredHelperSymbols(source, args, argv[1]);
  printNames("helper", std::vector<std::string>(helpers.begin(), helpers.end()));
  return 0;
}
