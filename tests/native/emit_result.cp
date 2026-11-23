/* Native JSONL result emitter for tools/run_final_evidence.sh.
 * Usage: emit_result <out.jsonl> <label> <command> <status> <rc> <duration> <reason> <logpath>
 * Reads the suite log and appends one JSON record. No Python dependency. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/file.hp"

static void esc(const char *s) {
    for (; *s != '\0'; ++s) {
        if (*s == '"' || *s == '\\') { putchar('\\'); putchar(*s); }
        else if (*s == '\n') fputs("\\n", stdout);
        else if (*s == '\r') fputs("\\r", stdout);
        else if ((unsigned char)*s < 0x20) printf("\\u%04x", (unsigned char)*s);
        else putchar(*s);
    }
}

int main(int argc, char **argv) {
    if (argc != 9) return 2;
    char log[32768];
    long n;
    FILE *f = fopen(argv[8], "r");
    if (f == NULL) { log[0] = '\0'; n = 0; }
    else {
        n = (long)fread(log, 1, sizeof(log) - 1, f);
        log[n] = '\0';
        fclose(f);
    }
    char final[2048] = "";
    char last[5][2048];
    int count = 0;
    for (long i = 0; i < n && count < 5; ++i) {
        if (log[i] != '\n' && log[i] != '\r') {
            long j = i;
            while (j < n && log[j] != '\n' && log[j] != '\r') ++j;
            long len = j - i;
            if (len > 2047) len = 2047;
            memcpy(last[count], log + i, (size_t)len);
            last[count][len] = '\0';
            strncpy(final, last[count], sizeof(final) - 1);
            final[sizeof(final) - 1] = '\0';
            ++count;
            i = j;
        }
    }
    FILE *out = fopen(argv[1], "a");
    if (out == NULL) return 3;
    fprintf(out, "{\"label\":\""); esc(argv[2]);
    fprintf(out, "\",\"command\":\""); esc(argv[3]);
    fprintf(out, "\",\"status\":\""); esc(argv[4]);
    fprintf(out, "\",\"exit_code\":%s,\"duration_seconds\":%s,\"final_line\":\"",
            argv[5], argv[6]);
    esc(final);
    fputs("\",\"final_lines\":[", out);
    for (int k = 0; k < count; ++k) {
        if (k > 0) fputs(",", out);
        fputs("\"", out); esc(last[k]); fputs("\"", out);
    }
    fputs("],\"reason\":\"", out); esc(argv[7]);
    fputs("\",\"log\":\"build/final-evidence/logs/", out); esc(argv[2]);
    fputs(".log\"}\n", out);
    fclose(out);
    return 0;
}
