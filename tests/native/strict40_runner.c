/* Native runner for the 40-test STRICT suite.
 * Compiled by the C+ driver; no Python involved.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  const char *root = argc > 1 ? argv[1] : "scratch/cplus_safety_suite/tests";
  const char *cplus = getenv("CPLUS_BIN");
  if (!cplus || !*cplus) cplus = "./build/c+";
  DIR *d = opendir(root);
  if (!d) { fprintf(stderr, "cannot open %s\n", root); return 2; }
  struct dirent *e;
  char cmd[4096], check_out[256], build_out[256], check_rc[256], build_rc[256];
  int total=0, check_reject=0, build_reject=0, mismatch=0;
  while ((e = readdir(d)) != NULL) {
    size_t n = strlen(e->d_name);
    if (n < 4 || strcmp(e->d_name+n-3, ".cp") != 0) continue;
    char src[1024];
    snprintf(src, sizeof(src), "%s/%s", root, e->d_name);
    snprintf(check_out, sizeof(check_out), "/tmp/strict40_check_%d.txt", (int)getpid());
    snprintf(build_out, sizeof(build_out), "/tmp/strict40_build_%d.txt", (int)getpid());
    snprintf(check_rc, sizeof(check_rc), "/tmp/strict40_check_%d.rc", (int)getpid());
    snprintf(build_rc, sizeof(build_rc), "/tmp/strict40_build_%d.rc", (int)getpid());
    snprintf(cmd, sizeof(cmd),
             "CPLUS_CLANG=/usr/bin/clang \"%s\" check --strict \"%s\" > \"%s\" 2>&1; echo $? > \"%s\"",
             cplus, src, check_out, check_rc);
    system(cmd);
    snprintf(cmd, sizeof(cmd),
             "CPLUS_CLANG=/usr/bin/clang \"%s\" --strict --sanitize=none \"%s\" -o /tmp/strict40_%d.bin > \"%s\" 2>&1; echo $? > \"%s\"",
             cplus, src, (int)getpid(), build_out, build_rc);
    system(cmd);
    FILE *fc = fopen(check_rc, "r"), *fb = fopen(build_rc, "r");
    int rc1=-1, rc2=-1;
    if (fc) { if (fscanf(fc, "%d", &rc1) != 1) rc1=-1; fclose(fc); }
    if (fb) { if (fscanf(fb, "%d", &rc2) != 1) rc2=-1; fclose(fb); }
    int r1 = (rc1 != 0), r2 = (rc2 != 0);
    total++; check_reject += r1; build_reject += r2; mismatch += (r1 != r2);
    printf("%-42s check=%-6s build=%-6s %s\n", e->d_name,
           r1?"REJECT":"accept", r2?"REJECT":"accept", r1==r2?"":"MISMATCH");
    unlink(check_out); unlink(build_out); unlink(check_rc); unlink(build_rc);
  }
  closedir(d);
  printf("STRICT 40-test suite: %d tests, check rejected %d, build rejected %d, mismatches %d\n",
         total, check_reject, build_reject, mismatch);
  return mismatch ? 1 : 0;
}
