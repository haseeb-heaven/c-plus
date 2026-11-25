/* ============================================================
 *  CGAME — Learn C, in C, from the command line
 *
 *  Build:   gcc -Wall -Wextra -std=c11 -o cgame cgame.c
 *  Run:     ./cgame
 *
 *  Progress is saved to ~/.cgame_progress
 *
 *  The game is data-driven: to add content, add Question
 *  entries and register a new Level in the levels[] table
 *  at the bottom of the DATA section. No engine changes needed.
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L   /* for popen/pclose with -std=c11 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* ---------- ANSI colors (disable with NO_COLOR env var) ---------- */
static int use_color = 1;
#define C_RESET  (use_color ? "\x1b[0m"  : "")
#define C_BOLD   (use_color ? "\x1b[1m"  : "")
#define C_DIM    (use_color ? "\x1b[2m"  : "")
#define C_GREEN  (use_color ? "\x1b[32m" : "")
#define C_RED    (use_color ? "\x1b[31m" : "")
#define C_YELLOW (use_color ? "\x1b[33m" : "")
#define C_CYAN   (use_color ? "\x1b[36m" : "")
#define C_MAG    (use_color ? "\x1b[35m" : "")

/* ============================================================
 *  ENGINE TYPES
 * ============================================================ */

typedef enum { Q_CHOICE, Q_TEXT } QType;

typedef struct {
    QType       type;
    const char *prompt;      /* the question                       */
    const char *code;        /* optional code snippet (or NULL)    */
    const char *choices[5];  /* up to 5 options, NULL-terminated   */
    const char *answer;      /* "a".."e" for Q_CHOICE, or exact
                                expected text for Q_TEXT           */
    const char *explain;     /* shown after answering              */
} Question;

typedef struct {
    const char     *title;
    const char     *lesson;     /* teaching text shown first */
    const Question *questions;
    int             n_questions;
    int             pass_needed; /* correct answers required to pass */
} Level;

typedef struct {
    const char *file;      /* filename created under exercises/   */
    const char *task;      /* what the fixed program must do       */
    const char *hint;      /* shown on demand                      */
    const char *expected;  /* required stdout (trailing \n ignored)*/
    const char *source;    /* the broken code, clings-style        */
} Exercise;

typedef struct {
    const char *file;       /* filename created under library/          */
    const char *title;      /* e.g. "my_strlen — how long is a string?" */
    const char *forbidden;  /* real stdlib name the player may NOT call */
    const char *theory;     /* beginner-friendly lesson, shown first    */
    const char *hint;
    const char *expected;   /* required stdout of the test program      */
    const char *stub;       /* starting file the player completes       */
    const char *test;       /* hidden test main(), compiled alongside   */
} LibExercise;

/* ============================================================
 *  DATA — Levels and questions
 * ============================================================ */

/* ---------- Level 1: Hello, C ---------- */
static const Question q1[] = {
{ Q_CHOICE,
  "Which function is the entry point of every C program?",
  NULL,
  { "a) start()", "b) main()", "c) init()", "d) run()", NULL },
  "b",
  "Execution always begins at main(). Its return value becomes the\n"
  "program's exit status (0 usually means success)." },

{ Q_TEXT,
  "What does this print? (type the exact output)",
  "#include <stdio.h>\n"
  "int main(void) {\n"
  "    printf(\"%d\", 2 + 3 * 4);\n"
  "    return 0;\n"
  "}",
  { NULL }, "14",
  "Multiplication binds tighter than addition: 2 + (3*4) = 14." },

{ Q_CHOICE,
  "Which format specifier prints an int?",
  NULL,
  { "a) %f", "b) %s", "c) %d", "d) %c", NULL },
  "c",
  "%d (or %i) is for int, %f for double, %s for strings, %c for a\n"
  "single character. Mismatched specifiers are undefined behavior!" },

{ Q_TEXT,
  "What is the value of x after this line? (type a number)",
  "int x = 7 / 2;",
  { NULL }, "3",
  "Integer division truncates toward zero: 7/2 is 3, not 3.5.\n"
  "Use 7.0/2 or (double)7/2 to get 3.5." },
};

/* ---------- Level 2: Types & operators ---------- */
static const Question q2[] = {
{ Q_TEXT,
  "What does this print?",
  "printf(\"%d\", 10 % 3);",
  { NULL }, "1",
  "% is the remainder operator: 10 = 3*3 + 1, so 10 %% 3 is 1." },

{ Q_CHOICE,
  "sizeof(char) is guaranteed to be...",
  NULL,
  { "a) 8", "b) 1", "c) 4", "d) implementation-defined", NULL },
  "b",
  "sizeof(char) is 1 *by definition* — always. A char is at least\n"
  "8 bits, but sizeof measures in chars, so it's exactly 1." },

{ Q_TEXT,
  "What does this print?",
  "int a = 5;\n"
  "int b = a++;\n"
  "printf(\"%d %d\", a, b);",
  { NULL }, "6 5",
  "Post-increment (a++) yields the OLD value, then increments.\n"
  "So b gets 5, and a becomes 6." },

{ Q_CHOICE,
  "What is the result of: 1 == 2 == 2  ?",
  NULL,
  { "a) 1 (true)", "b) 0 (false)", "c) 2", "d) compile error", NULL },
  "b",
  "== is left-associative: (1==2)==2 → 0==2 → 0.\n"
  "Chained comparisons don't work like in math (or Python)!" },

{ Q_TEXT,
  "Gotcha! What does this print?",
  "int x = 5;\n"
  "if (x = 0)\n"
  "    printf(\"zero\");\n"
  "else\n"
  "    printf(\"not zero\");",
  { NULL }, "not zero",
  "That's = (assignment), not == (comparison). x becomes 0, the\n"
  "expression's value is 0 (false), so the else branch runs.\n"
  "Compile with -Wall and gcc will warn you about this classic bug." },
};

/* ---------- Level 3: Control flow ---------- */
static const Question q3[] = {
{ Q_TEXT,
  "How many times does this loop print \"hi\"? (type a number)",
  "for (int i = 0; i < 5; i++)\n"
  "    printf(\"hi\");",
  { NULL }, "5",
  "i runs 0,1,2,3,4 — five iterations. At i==5 the condition fails." },

{ Q_TEXT,
  "Gotcha! What does this print?",
  "int i = 0;\n"
  "while (i < 3);   /* note the semicolon! */\n"
  "{\n"
  "    printf(\"%d\", i);\n"
  "    i++;\n"
  "}",
  { NULL }, "nothing",
  "The stray ';' makes the while-body empty → infinite loop with\n"
  "i stuck at 0. It never prints. (Answer 'nothing' — it hangs!)\n"
  "The { } block below is just a plain scope, not the loop body." },

{ Q_TEXT,
  "What does this print?",
  "switch (2) {\n"
  "    case 1: printf(\"A\");\n"
  "    case 2: printf(\"B\");\n"
  "    case 3: printf(\"C\"); break;\n"
  "    case 4: printf(\"D\");\n"
  "}",
  { NULL }, "BC",
  "No break → fall-through! Execution enters at case 2, prints B,\n"
  "falls into case 3, prints C, then break exits. Forgetting break\n"
  "is one of the most common C bugs." },

{ Q_CHOICE,
  "What does 'continue' do inside a loop?",
  NULL,
  { "a) exits the loop", "b) restarts the program",
    "c) skips to the next iteration", "d) pauses execution", NULL },
  "c",
  "continue jumps to the loop's next iteration (in a for loop, the\n"
  "increment expression still runs). break exits the loop entirely." },
};

/* ---------- Level 4: Functions & scope ---------- */
static const Question q4[] = {
{ Q_TEXT,
  "What does this print?",
  "void change(int x) { x = 99; }\n"
  "int main(void) {\n"
  "    int a = 5;\n"
  "    change(a);\n"
  "    printf(\"%d\", a);\n"
  "}",
  { NULL }, "5",
  "C is pass-by-value: change() gets a COPY of a. Modifying the copy\n"
  "does nothing to the original. To modify, pass a pointer: change(&a)." },

{ Q_TEXT,
  "What does this print?",
  "int counter(void) {\n"
  "    static int n = 0;\n"
  "    return ++n;\n"
  "}\n"
  "int main(void) {\n"
  "    counter(); counter();\n"
  "    printf(\"%d\", counter());\n"
  "}",
  { NULL }, "3",
  "static local variables keep their value between calls and are\n"
  "initialized only once. Three calls: 1, 2, 3." },

{ Q_CHOICE,
  "What is wrong with this function?",
  "int *broken(void) {\n"
  "    int x = 42;\n"
  "    return &x;\n"
  "}",
  { "a) nothing", "b) returns address of a dead local variable",
    "c) x must be const", "d) missing malloc cast", NULL },
  "b",
  "x lives on the stack and dies when the function returns. The\n"
  "returned pointer is dangling — using it is undefined behavior.\n"
  "This is THE classic dangling-pointer gotcha." },

{ Q_CHOICE,
  "In C, 'void f();' and 'void f(void);' are...",
  NULL,
  { "a) identical", "b) different: () means unspecified parameters",
    "c) both compile errors", "d) different only in C++", NULL },
  "b",
  "In C (before C23), f() declares a function with UNSPECIFIED\n"
  "parameters — the compiler won't check calls! f(void) means truly\n"
  "no parameters. Always write (void) in C." },
};

/* ---------- Level 5: Pointers ---------- */
static const Question q5[] = {
{ Q_TEXT,
  "What does this print?",
  "int a = 10;\n"
  "int *p = &a;\n"
  "*p = 20;\n"
  "printf(\"%d\", a);",
  { NULL }, "20",
  "p holds the address of a; *p dereferences it. Writing through *p\n"
  "writes to a itself." },

{ Q_CHOICE,
  "In 'int *a, b;' what is b?",
  NULL,
  { "a) a pointer to int", "b) a plain int",
    "c) undefined", "d) compile error", NULL },
  "b",
  "The * binds to the declarator, not the type! 'int *a, b;'\n"
  "declares a as int* but b as plain int. A very common trap —\n"
  "declare pointers one per line to stay safe." },

{ Q_TEXT,
  "If p is an int* and sizeof(int)==4, how many BYTES does\n"
  "the address advance when you do p+1? (type a number)",
  NULL,
  { NULL }, "4",
  "Pointer arithmetic scales by the pointed-to type's size.\n"
  "p+1 points to the NEXT int, i.e. 4 bytes further." },

{ Q_CHOICE,
  "What does dereferencing a NULL pointer do?",
  NULL,
  { "a) returns 0", "b) returns garbage",
    "c) undefined behavior (usually a crash)", "d) throws an exception", NULL },
  "c",
  "Undefined behavior. On most systems you get a segfault, but the\n"
  "standard makes NO guarantee — anything can happen. C has no\n"
  "exceptions to catch it." },
};

/* ---------- Level 6: Arrays & strings ---------- */
static const Question q6[] = {
{ Q_TEXT,
  "What does this print?",
  "int arr[] = {10, 20, 30};\n"
  "printf(\"%d\", *(arr + 1));",
  { NULL }, "20",
  "arr decays to a pointer to its first element. *(arr+1) is\n"
  "identical to arr[1]. In fact a[i] is defined as *(a+i) —\n"
  "which is why the bizarre 1[arr] also compiles!" },

{ Q_TEXT,
  "sizeof(arr) inside main vs inside f — what does this print?",
  "void f(int arr[]) { printf(\"%zu \", sizeof(arr)); }\n"
  "/* on a 64-bit system, sizeof(int)==4, sizeof(int*)==8 */\n"
  "int main(void) {\n"
  "    int arr[10];\n"
  "    printf(\"%zu \", sizeof(arr));\n"
  "    f(arr);\n"
  "}",
  { NULL }, "40 8",
  "In main, sizeof(arr) is the whole array: 10*4 = 40 bytes.\n"
  "But array parameters DECAY to pointers — inside f, arr is really\n"
  "an int*, so sizeof gives 8. You cannot pass arrays by value;\n"
  "always pass the length separately." },

{ Q_TEXT,
  "How many bytes does char s[] = \"cat\"; occupy? (number)",
  NULL,
  { NULL }, "4",
  "String literals include a hidden terminating '\\0':\n"
  "'c','a','t','\\0' → 4 bytes. Forgetting the terminator space\n"
  "(e.g. malloc(strlen(s)) instead of strlen(s)+1) is a classic bug." },

{ Q_CHOICE,
  "What's wrong here?",
  "char *s = \"hello\";\n"
  "s[0] = 'H';",
  { "a) nothing", "b) modifying a string literal is undefined behavior",
    "c) s must be s* instead", "d) 'H' needs double quotes", NULL },
  "b",
  "String literals live in read-only memory; writing to one is UB\n"
  "(typically a crash). Use char s[] = \"hello\"; to get a writable\n"
  "copy, and prefer const char* for literal pointers." },

{ Q_CHOICE,
  "Why is gets() infamous?",
  NULL,
  { "a) it's slow", "b) it can't read spaces",
    "c) no way to limit input length → buffer overflow", "d) it's C++ only", NULL },
  "c",
  "gets() cannot know your buffer size, so any long input overflows\n"
  "it. It was removed from the C11 standard. Use fgets(buf,\n"
  "sizeof buf, stdin) instead." },
};

/* ---------- Level 7: Memory ---------- */
static const Question q7[] = {
{ Q_CHOICE,
  "After free(p), what is p?",
  NULL,
  { "a) NULL", "b) a dangling pointer — the value is unchanged",
    "c) zeroed memory", "d) a fresh allocation", NULL },
  "b",
  "free() releases the memory but does NOT touch your pointer\n"
  "variable. Using it afterwards is use-after-free UB. Good habit:\n"
  "free(p); p = NULL;" },

{ Q_CHOICE,
  "What does malloc(n) return if it fails?",
  NULL,
  { "a) 0 as an int", "b) NULL", "c) -1", "d) it aborts the program", NULL },
  "b",
  "malloc returns NULL on failure — always check before using the\n"
  "pointer. Note: with malloc(0), returning NULL is also allowed\n"
  "even on 'success'." },

{ Q_CHOICE,
  "What's the bug?",
  "int *p = malloc(10 * sizeof(int));\n"
  "p = malloc(20 * sizeof(int));\n"
  "free(p);",
  { "a) double free", "b) the first allocation is leaked",
    "c) sizeof(int) is wrong", "d) no bug", NULL },
  "b",
  "Reassigning p loses the only reference to the first block —\n"
  "a memory leak. Every malloc needs exactly one matching free." },

{ Q_CHOICE,
  "What does calling free(p) twice on the same pointer do?",
  NULL,
  { "a) nothing, it's safe", "b) frees more memory",
    "c) undefined behavior", "d) returns an error code", NULL },
  "c",
  "Double free is UB and a common security vulnerability (heap\n"
  "corruption). free(NULL) however is explicitly safe and does\n"
  "nothing — another reason to NULL pointers after freeing." },
};

/* ---------- Level 8: Integer gotchas ---------- */
static const Question q8[] = {
{ Q_TEXT,
  "Gotcha! Does this print \"yes\" or \"no\"?",
  "unsigned int u = 1;\n"
  "int i = -1;\n"
  "if (i < u) printf(\"yes\");\n"
  "else       printf(\"no\");",
  { NULL }, "no",
  "When int meets unsigned int, the int is CONVERTED to unsigned.\n"
  "-1 becomes UINT_MAX (huge), so i < u is false. Signed/unsigned\n"
  "comparison is a notorious bug source — enable -Wsign-compare." },

{ Q_CHOICE,
  "Signed integer overflow (e.g. INT_MAX + 1) is...",
  NULL,
  { "a) wraps to INT_MIN, guaranteed", "b) undefined behavior",
    "c) saturates at INT_MAX", "d) a compile error", NULL },
  "b",
  "Signed overflow is UB — compilers even optimize assuming it never\n"
  "happens! UNSIGNED overflow, in contrast, wraps around modulo 2^n\n"
  "and is perfectly defined." },

{ Q_TEXT,
  "What does this print? (sizeof returns an unsigned type!)",
  "int n = -1;\n"
  "if (n < sizeof(int)) printf(\"small\");\n"
  "else                 printf(\"big\");",
  { NULL }, "big",
  "sizeof yields size_t (unsigned). n is converted to unsigned →\n"
  "huge number → 'big'. Same trap as before, hidden behind sizeof." },

{ Q_TEXT,
  "char arithmetic: what does this print?",
  "printf(\"%d\", 'A' + 1);",
  { NULL }, "66",
  "Character constants are ints in C. 'A' is 65 (ASCII), so 'A'+1\n"
  "is 66. printf(\"%c\", 'A'+1) would print B." },
};

/* ---------- Level 9: Undefined behavior hall of fame ---------- */
static const Question q9[] = {
{ Q_CHOICE,
  "What does this print?",
  "int i = 1;\n"
  "printf(\"%d\", i++ + i++);",
  { "a) 3", "b) 4", "c) 2", "d) undefined behavior — anything", NULL },
  "d",
  "Modifying i twice without a sequence point between = UB.\n"
  "Different compilers give different answers, and all are 'right'.\n"
  "Same goes for i = i++; and printf(\"%d %d\", i, i++);" },

{ Q_CHOICE,
  "int arr[5]; arr[5] = 1;  — this is...",
  NULL,
  { "a) fine, arrays grow", "b) sets the last element",
    "c) out-of-bounds write: undefined behavior", "d) a compile error", NULL },
  "c",
  "Valid indices are 0..4. arr[5] is one past the end — writing\n"
  "there is UB (stack corruption, security holes). C does NO bounds\n"
  "checking, ever. Off-by-one errors are eternal." },

{ Q_CHOICE,
  "Reading an uninitialized local variable...",
  "int x;\n"
  "printf(\"%d\", x);",
  { "a) prints 0", "b) prints garbage but is legal",
    "c) is undefined behavior", "d) is a compile error", NULL },
  "c",
  "Locals are NOT zero-initialized (unlike static/global variables,\n"
  "which are). Reading an indeterminate value is UB. Initialize\n"
  "everything; -Wuninitialized helps catch these." },

{ Q_TEXT,
  "Macro gotcha! What does this print?",
  "#define SQUARE(x) x * x\n"
  "int main(void) {\n"
  "    printf(\"%d\", SQUARE(2 + 1));\n"
  "}",
  { NULL }, "5",
  "Text substitution: SQUARE(2+1) → 2 + 1 * 2 + 1 = 5, not 9!\n"
  "Always parenthesize macro args AND the whole body:\n"
  "#define SQUARE(x) ((x) * (x))" },

{ Q_CHOICE,
  "Final boss: which of these is NOT undefined behavior?",
  NULL,
  { "a) unsigned int wrap-around", "b) dereferencing NULL",
    "c) signed overflow", "d) use-after-free", NULL },
  "a",
  "Unsigned arithmetic wraps modulo 2^n by definition — it's the\n"
  "only one here that's fully defined. Congratulations, you've\n"
  "survived the C gotcha gauntlet. Now go read the standard. :)" },
};

/* ============================================================
 *  SANDBOX EXERCISES — one broken file per level (clings-style)
 * ============================================================ */

static const Exercise exercises[] = {

{ "ex01_average.c",
  "The program must print the average of 9 and 4 as a decimal:\n"
  "exactly  6.5",
  "Integer division truncates! Make the division floating point\n"
  "(divide by 2.0), store it in a double, and print with %.1f.",
  "6.5",
  "/* ex01: The broken average\n"
  " * TASK: print the average of 9 and 4 as a decimal: 6.5\n"
  " * Fix this file, then press Enter in the game to check it.\n"
  " */\n"
  "#include <stdio.h>\n"
  "\n"
  "int main(void) {\n"
  "    int a = 9, b = 4;\n"
  "    int avg = (a + b) / 2;   /* something truncates here...      */\n"
  "    printf(\"%d\\n\", avg);    /* ...and this specifier can't do 6.5 */\n"
  "    return 0;\n"
  "}\n" },

{ "ex02_doorlock.c",
  "With secret code 1111 the door must stay shut:\n"
  "the program must print exactly  locked",
  "Look very closely at the if condition. Is that a comparison\n"
  "(==) or an assignment (=)? The compiler even warns you.",
  "locked",
  "/* ex02: The door that always opens\n"
  " * TASK: print \"open\" only if code == 1234, otherwise \"locked\".\n"
  " * With code = 1111 it must print: locked\n"
  " */\n"
  "#include <stdio.h>\n"
  "\n"
  "int main(void) {\n"
  "    int code = 1111;\n"
  "    if (code = 1234)          /* hmm, this door always opens... */\n"
  "        printf(\"open\\n\");\n"
  "    else\n"
  "        printf(\"locked\\n\");\n"
  "    return 0;\n"
  "}\n" },

{ "ex03_grades.c",
  "grade is 'B', so the program must print exactly one line:\n"
  "Good",
  "switch cases FALL THROUGH into each other unless you stop\n"
  "them. What keyword ends a case?",
  "Good",
  "/* ex03: The generous grader\n"
  " * TASK: print exactly ONE description for the grade.\n"
  " * With grade = 'B' it must print only: Good\n"
  " */\n"
  "#include <stdio.h>\n"
  "\n"
  "int main(void) {\n"
  "    char grade = 'B';\n"
  "    switch (grade) {\n"
  "        case 'A': printf(\"Excellent\\n\");\n"
  "        case 'B': printf(\"Good\\n\");\n"
  "        case 'C': printf(\"Okay\\n\");\n"
  "        default:  printf(\"Unknown\\n\");\n"
  "    }\n"
  "    return 0;\n"
  "}\n" },

{ "ex04_double_it.c",
  "double_it() must actually double the caller's variable.\n"
  "The program must print  42",
  "C passes by value — double_it gets a COPY. Change it to take\n"
  "an int* parameter, write through *x, and call it with &n.",
  "42",
  "/* ex04: The function that changes nothing\n"
  " * TASK: make double_it() really double the caller's variable.\n"
  " * The program must print: 42\n"
  " */\n"
  "#include <stdio.h>\n"
  "\n"
  "void double_it(int x) {\n"
  "    x = x * 2;               /* doubles... a copy. */\n"
  "}\n"
  "\n"
  "int main(void) {\n"
  "    int n = 21;\n"
  "    double_it(n);\n"
  "    printf(\"%d\\n\", n);\n"
  "    return 0;\n"
  "}\n" },

{ "ex05_swap.c",
  "swap() must really swap a and b.\n"
  "The program must print  a=2 b=1",
  "Same disease as ex04: swap() swaps two copies. Take int*\n"
  "parameters, swap *x and *y, and pass &a, &b.",
  "a=2 b=1",
  "/* ex05: The swap that doesn't\n"
  " * TASK: make swap() actually exchange a and b.\n"
  " * The program must print: a=2 b=1\n"
  " */\n"
  "#include <stdio.h>\n"
  "\n"
  "void swap(int x, int y) {\n"
  "    int tmp = x;\n"
  "    x = y;\n"
  "    y = tmp;\n"
  "}\n"
  "\n"
  "int main(void) {\n"
  "    int a = 1, b = 2;\n"
  "    swap(a, b);\n"
  "    printf(\"a=%d b=%d\\n\", a, b);\n"
  "    return 0;\n"
  "}\n" },

{ "ex06_offbyone.c",
  "Sum the five array elements 1..5.\n"
  "The program must print  15",
  "Valid indices of nums[5] are 0..4. Look at the loop condition:\n"
  "<= reads one element PAST the end (undefined behavior!).",
  "15",
  "/* ex06: One step too far\n"
  " * TASK: sum the 5 elements. The program must print: 15\n"
  " * Right now it reads past the end of the array (UB!).\n"
  " */\n"
  "#include <stdio.h>\n"
  "\n"
  "int main(void) {\n"
  "    int nums[5] = {1, 2, 3, 4, 5};\n"
  "    int sum = 0;\n"
  "    for (int i = 0; i <= 5; i++)   /* how many times does this run? */\n"
  "        sum += nums[i];\n"
  "    printf(\"%d\\n\", sum);\n"
  "    return 0;\n"
  "}\n" },

{ "ex07_squares.c",
  "Print the squares of 1..5, one per line:\n"
  "1 4 9 16 25  (each on its own line)",
  "Someone changed count to 3. Fix it to 5 — the malloc size and\n"
  "both loops all depend on it. That's WHY you use one variable\n"
  "for the size instead of repeating magic numbers.",
  "1\n4\n9\n16\n25",
  "/* ex07: The shrunken buffer\n"
  " * TASK: print the squares of 1..5, one per line: 1 4 9 16 25\n"
  " * Allocate exactly as much memory as you need — and free it.\n"
  " */\n"
  "#include <stdio.h>\n"
  "#include <stdlib.h>\n"
  "\n"
  "int main(void) {\n"
  "    int count = 3;                          /* ...that's not 5 */\n"
  "    int *sq = malloc(count * sizeof(int));\n"
  "    if (sq == NULL)\n"
  "        return 1;\n"
  "    for (int i = 0; i < count; i++)\n"
  "        sq[i] = (i + 1) * (i + 1);\n"
  "    for (int i = 0; i < count; i++)\n"
  "        printf(\"%d\\n\", sq[i]);\n"
  "    free(sq);\n"
  "    return 0;\n"
  "}\n" },

{ "ex08_liftoff.c",
  "Count down 5 4 3 2 1 0 (one per line), then print liftoff.\n"
  "Careful: right now the program NEVER stops. The checker will\n"
  "kill it after 5 seconds.",
  "An unsigned int is ALWAYS >= 0 — when it passes 0 it wraps to\n"
  "a huge number. Use a signed int for the loop counter.",
  "5\n4\n3\n2\n1\n0\nliftoff",
  "/* ex08: The rocket that never launches\n"
  " * TASK: count down from 5 to 0, then print liftoff.\n"
  " * Expected: 5 4 3 2 1 0 liftoff (one per line)\n"
  " * WARNING: this currently loops forever. Why?\n"
  " */\n"
  "#include <stdio.h>\n"
  "\n"
  "int main(void) {\n"
  "    for (unsigned int i = 5; i >= 0; i--)   /* when is this false? */\n"
  "        printf(\"%u\\n\", i);\n"
  "    printf(\"liftoff\\n\");\n"
  "    return 0;\n"
  "}\n" },

{ "ex09_macro.c",
  "SQUARE(x) must really square its argument.\n"
  "The program must print  9 16",
  "Macros are TEXT substitution: SQUARE(n + 1) expands to\n"
  "n + 1 * n + 1. Parenthesize each argument AND the whole body:\n"
  "((x) * (x))",
  "9 16",
  "/* ex09: The macro that lies\n"
  " * TASK: SQUARE must square its argument.\n"
  " * The program must print: 9 16\n"
  " */\n"
  "#include <stdio.h>\n"
  "\n"
  "#define SQUARE(x) x * x        /* looks fine... is it? */\n"
  "\n"
  "int main(void) {\n"
  "    int n = 2;\n"
  "    printf(\"%d %d\\n\", SQUARE(n + 1), SQUARE(4));\n"
  "    return 0;\n"
  "}\n" },
};

/* ============================================================
 *  LIBRARY PATH — build the C standard library yourself
 *  (for complete beginners: theory first, then write the code)
 * ============================================================ */

static const LibExercise libex[] = {

/* ---------- Ex00: boilerplate / the grammar of C ---------- */
{ "ex00_answer.c",
  "Ex00: your very first function (the grammar of C)",
  NULL,
  "Welcome! Before writing library functions, you need to know\n"
  "what a C source file even looks like. Here is the anatomy of\n"
  "a function — the basic building block of every C program:\n"
  "\n"
  "      int  my_answer (void)      <- the 'signature'\n"
  "      ^    ^          ^\n"
  "      |    |          +-- parameters: what goes IN.\n"
  "      |    |              'void' means: nothing goes in.\n"
  "      |    +-- the name. You call it by this name.\n"
  "      +-- return type: what comes OUT. 'int' = a whole number.\n"
  "\n"
  "      {                          <- '{' opens the body\n"
  "          return 42;             <- statements end with ';'\n"
  "      }                          <- '}' closes the body\n"
  "\n"
  "'return' hands a value back to whoever called the function and\n"
  "ends it immediately. That's it — that's a complete, valid piece\n"
  "of C. The game compiles your file together with a hidden test\n"
  "program that calls your function and checks the result.",
  "Change the number after 'return' to 42. Don't forget the\n"
  "semicolon. Don't touch anything else.",
  "42",
  "/* Ex00: The Answer\n"
  " * TASK: make my_answer give back the number 42.\n"
  " * Do NOT add a main() — the game brings its own test program.\n"
  " */\n"
  "int my_answer(void)\n"
  "{\n"
  "    return 0;    /* <-- that is not the Answer. Fix me! */\n"
  "}\n",
  "#include <stdio.h>\n"
  "int my_answer(void);\n"
  "int main(void) {\n"
  "    printf(\"%d\\n\", my_answer());\n"
  "    return 0;\n"
  "}\n" },

/* ---------- Ex01: my_abs ---------- */
{ "ex01_abs.c",
  "Ex01: my_abs — parameters and decisions",
  "abs",
  "Real library function: abs() from <stdlib.h>.\n"
  "\n"
  "This time your function takes a PARAMETER — a value handed in:\n"
  "\n"
  "      int my_abs(int n)\n"
  "\n"
  "Inside the body, 'n' is a variable holding whatever number the\n"
  "caller passed. The absolute value means: strip the minus sign.\n"
  "my_abs(5) is 5, my_abs(-7) is 7.\n"
  "\n"
  "To decide between two cases, C has if/else:\n"
  "\n"
  "      if (n < 0)          <- condition in parentheses\n"
  "          return -n;      <- runs only if the condition is true\n"
  "      return n;           <- otherwise we get here\n"
  "\n"
  "Note '-n' flips the sign: if n is -7, then -n is 7.",
  "If n is less than 0, return -n. Otherwise return n as it is.\n"
  "Two lines of logic — you saw them in the theory. ;)",
  "5\n7\n0",
  "/* Ex01: my_abs\n"
  " * TASK: return the absolute value of n (the number without\n"
  " * its minus sign). my_abs(-7) must give 7, my_abs(5) gives 5.\n"
  " * You may NOT call the real library function.\n"
  " */\n"
  "int my_abs(int n)\n"
  "{\n"
  "    return n;    /* only correct for half the numbers... */\n"
  "}\n",
  "#include <stdio.h>\n"
  "int my_abs(int n);\n"
  "int main(void) {\n"
  "    printf(\"%d\\n\", my_abs(5));\n"
  "    printf(\"%d\\n\", my_abs(-7));\n"
  "    printf(\"%d\\n\", my_abs(0));\n"
  "    return 0;\n"
  "}\n" },

/* ---------- Ex02: my_max ---------- */
{ "ex02_max.c",
  "Ex02: my_max — two parameters",
  NULL,
  "Functions can take several parameters, separated by commas:\n"
  "\n"
  "      int my_max(int a, int b)\n"
  "\n"
  "my_max must return whichever of the two numbers is bigger\n"
  "(if they're equal, either one — they're the same anyway).\n"
  "\n"
  "Comparison operators you can use in conditions:\n"
  "      a >  b     greater than\n"
  "      a <  b     less than\n"
  "      a >= b     greater or equal\n"
  "      a == b     equal        (TWO equals signs!)\n"
  "\n"
  "One '=' ASSIGNS a value; '==' COMPARES. Mixing those up is one\n"
  "of the most classic C mistakes — you'll meet it again in the\n"
  "gotcha levels.",
  "if (a > b) return a;  otherwise return b.",
  "9\n10\n4",
  "/* Ex02: my_max\n"
  " * TASK: return the larger of the two numbers.\n"
  " */\n"
  "int my_max(int a, int b)\n"
  "{\n"
  "    (void)b;     /* delete this line when you use b */\n"
  "    return a;    /* a is not ALWAYS the winner... */\n"
  "}\n",
  "#include <stdio.h>\n"
  "int my_max(int a, int b);\n"
  "int main(void) {\n"
  "    printf(\"%d\\n\", my_max(3, 9));\n"
  "    printf(\"%d\\n\", my_max(10, 2));\n"
  "    printf(\"%d\\n\", my_max(4, 4));\n"
  "    return 0;\n"
  "}\n" },

/* ---------- Ex03: my_toupper ---------- */
{ "ex03_toupper.c",
  "Ex03: my_toupper — characters are just numbers",
  "toupper",
  "Real library function: toupper() from <ctype.h>.\n"
  "\n"
  "Big secret of C: a character IS a number. 'a' is 97, 'b' is 98,\n"
  "... 'z' is 122. 'A' is 65 ... 'Z' is 90 (the ASCII table).\n"
  "You can do math with them! 'a' - 32 gives 65, which is 'A'.\n"
  "The gap between every lowercase letter and its uppercase twin\n"
  "is exactly 32, or written more readably: ('a' - 'A').\n"
  "\n"
  "You can also compare characters like numbers:\n"
  "\n"
  "      if (c >= 'a' && c <= 'z')    <- is c a lowercase letter?\n"
  "\n"
  "'&&' means AND — both sides must be true. Your job: if c is a\n"
  "lowercase letter, return its uppercase twin; any other character\n"
  "(digits, punctuation, already-uppercase) comes back UNCHANGED.",
  "if (c >= 'a' && c <= 'z') return c - 32;\n"
  "return c;   /* everything else stays as it is */",
  "A\nZ\n!\nM",
  "/* Ex03: my_toupper\n"
  " * TASK: if c is a lowercase letter, return the uppercase\n"
  " * version. Anything else must come back unchanged.\n"
  " * You may NOT call the real library function.\n"
  " */\n"
  "int my_toupper(int c)\n"
  "{\n"
  "    return c;    /* nothing upper about this yet */\n"
  "}\n",
  "#include <stdio.h>\n"
  "int my_toupper(int c);\n"
  "int main(void) {\n"
  "    printf(\"%c\\n\", my_toupper('a'));\n"
  "    printf(\"%c\\n\", my_toupper('Z'));\n"
  "    printf(\"%c\\n\", my_toupper('!'));\n"
  "    printf(\"%c\\n\", my_toupper('m'));\n"
  "    return 0;\n"
  "}\n" },

/* ---------- Ex04: my_strlen ---------- */
{ "ex04_strlen.c",
  "Ex04: my_strlen — how long is a string?",
  "strlen",
  "Real library function: strlen() from <string.h>.\n"
  "\n"
  "A C string is a row of characters in memory, ending with an\n"
  "invisible zero byte written '\\0' (the terminator):\n"
  "\n"
  "      \"cat\"  is really   'c' 'a' 't' '\\0'\n"
  "       index:              0   1   2   3\n"
  "\n"
  "s[0] is the first character, s[1] the second, and so on.\n"
  "The length of a string = how many characters come BEFORE the\n"
  "'\\0'. To find it, walk along the string with a loop:\n"
  "\n"
  "      int i = 0;\n"
  "      while (s[i] != '\\0')   <- as long as we haven't hit the end\n"
  "          i++;                <- move one step right\n"
  "\n"
  "'i++' means i = i + 1. When the loop stops, i is the length.\n"
  "(The 'const char *s' parameter just means: a string we promise\n"
  "not to modify. Treat it like a read-only row of characters.)",
  "Count up i while s[i] is not '\\0', then return i. The loop in\n"
  "the theory is 90% of the answer.",
  "5\n0\n9",
  "/* Ex04: my_strlen\n"
  " * TASK: return the number of characters before the '\\0'.\n"
  " * my_strlen(\"hello\") must give 5. Empty string gives 0.\n"
  " * You may NOT call the real library function.\n"
  " */\n"
  "int my_strlen(const char *s)\n"
  "{\n"
  "    (void)s;     /* delete this line when you use s */\n"
  "    return 0;    /* every string has length 0? doubt it. */\n"
  "}\n",
  "#include <stdio.h>\n"
  "int my_strlen(const char *s);\n"
  "int main(void) {\n"
  "    printf(\"%d\\n\", my_strlen(\"hello\"));\n"
  "    printf(\"%d\\n\", my_strlen(\"\"));\n"
  "    printf(\"%d\\n\", my_strlen(\"C is fun!\"));\n"
  "    return 0;\n"
  "}\n" },

/* ---------- Ex05: my_strcpy ---------- */
{ "ex05_strcpy.c",
  "Ex05: my_strcpy — copying a string",
  "strcpy",
  "Real library function: strcpy() from <string.h>.\n"
  "\n"
  "      char *my_strcpy(char *dst, const char *src)\n"
  "\n"
  "src is the string to copy FROM (read-only), dst is the memory\n"
  "to copy INTO. Copy character by character:\n"
  "\n"
  "      int i = 0;\n"
  "      while (src[i] != '\\0') {\n"
  "          dst[i] = src[i];     <- copy one character\n"
  "          i++;\n"
  "      }\n"
  "\n"
  "THE CLASSIC TRAP: the loop stops AT the '\\0' but never copies\n"
  "it! Without the terminator, dst is not a valid string — printing\n"
  "it shows your text plus whatever garbage follows in memory.\n"
  "After the loop you must add:   dst[i] = '\\0';\n"
  "\n"
  "The function returns dst (so calls can be chained) — the\n"
  "'return dst;' at the end is already written for you.\n"
  "The hidden test fills the buffer with 'X' characters first,\n"
  "so a forgotten terminator WILL be caught.",
  "Copy src[i] into dst[i] in a loop until you hit '\\0' — then\n"
  "write dst[i] = '\\0'; after the loop. Don't forget it!",
  "hi\n[]\nC C",
  "/* Ex05: my_strcpy\n"
  " * TASK: copy the string src into dst, INCLUDING the\n"
  " * terminating '\\0'. Return dst.\n"
  " * You may NOT call the real library function.\n"
  " */\n"
  "char *my_strcpy(char *dst, const char *src)\n"
  "{\n"
  "    (void)src;   /* delete this line when you use src */\n"
  "    /* your copying loop here */\n"
  "    return dst;\n"
  "}\n",
  "#include <stdio.h>\n"
  "char *my_strcpy(char *dst, const char *src);\n"
  "int main(void) {\n"
  "    char buf[32];\n"
  "    for (int i = 0; i < 31; i++) buf[i] = 'X';\n"
  "    buf[31] = '\\0';\n"
  "    my_strcpy(buf, \"hi\");\n"
  "    printf(\"%s\\n\", buf);\n"
  "    my_strcpy(buf, \"\");\n"
  "    printf(\"[%s]\\n\", buf);\n"
  "    char *r = my_strcpy(buf, \"C\");\n"
  "    printf(\"%s %s\\n\", buf, r);\n"
  "    return 0;\n"
  "}\n" },

/* ---------- Ex06: my_strcat ---------- */
{ "ex06_strcat.c",
  "Ex06: my_strcat — gluing strings together",
  "strcat",
  "Real library function: strcat() from <string.h>.\n"
  "\n"
  "      char *my_strcat(char *dst, const char *src)\n"
  "\n"
  "Append src to the END of the string already in dst:\n"
  "if dst holds \"Hello\" and src is \", world\", afterwards dst\n"
  "holds \"Hello, world\".\n"
  "\n"
  "Plan of attack (two steps you already know!):\n"
  "  1. Walk to the end of dst — find the index of its '\\0'.\n"
  "     (that's the my_strlen idea from Ex04)\n"
  "  2. From there, copy src character by character, and finish\n"
  "     with a '\\0'. (that's the my_strcpy idea from Ex05)\n"
  "\n"
  "You can use TWO index variables: i walking dst, j walking src:\n"
  "\n"
  "      dst[i + j] = src[j];\n"
  "\n"
  "This is how libraries are really built — small functions\n"
  "combined into bigger ones.",
  "First: while (dst[i] != '\\0') i++;   — find the end.\n"
  "Then:  copy src[j] to dst[i + j] until src[j] is '\\0',\n"
  "and finally dst[i + j] = '\\0';",
  "Hello, world\nHello, world!\nHello, world!",
  "/* Ex06: my_strcat\n"
  " * TASK: append src to the end of dst (dst already contains\n"
  " * a valid string). Terminate the result with '\\0'. Return dst.\n"
  " * You may NOT call the real library function.\n"
  " */\n"
  "char *my_strcat(char *dst, const char *src)\n"
  "{\n"
  "    (void)src;   /* delete this line when you use src */\n"
  "    /* step 1: find the end of dst */\n"
  "    /* step 2: copy src there, then terminate */\n"
  "    return dst;\n"
  "}\n",
  "#include <stdio.h>\n"
  "char *my_strcat(char *dst, const char *src);\n"
  "int main(void) {\n"
  "    char buf[40] = \"Hello\";\n"
  "    my_strcat(buf, \", world\");\n"
  "    printf(\"%s\\n\", buf);\n"
  "    my_strcat(buf, \"!\");\n"
  "    printf(\"%s\\n\", buf);\n"
  "    char *r = my_strcat(buf, \"\");\n"
  "    printf(\"%s\\n\", r);\n"
  "    return 0;\n"
  "}\n" },

/* ---------- Ex07: my_strcmp ---------- */
{ "ex07_strcmp.c",
  "Ex07: my_strcmp — comparing strings",
  "strcmp",
  "Real library function: strcmp() from <string.h>.\n"
  "\n"
  "You CANNOT compare strings with ==  (that compares memory\n"
  "addresses, not contents — a huge beginner trap!). Instead the\n"
  "library walks both strings and compares character by character.\n"
  "\n"
  "The contract of strcmp(s1, s2):\n"
  "      returns 0          if the strings are identical\n"
  "      returns negative   if s1 comes before s2 (alphabetically)\n"
  "      returns positive   if s1 comes after s2\n"
  "\n"
  "The algorithm: walk with i while the characters match AND we\n"
  "haven't reached the end:\n"
  "\n"
  "      while (s1[i] != '\\0' && s1[i] == s2[i])\n"
  "          i++;\n"
  "      return s1[i] - s2[i];\n"
  "\n"
  "That final subtraction does all the magic: equal chars give 0,\n"
  "and 'a' - 'b' is negative because characters are numbers\n"
  "(remember Ex03?). If one string is just shorter, its '\\0'\n"
  "(value 0) loses against any real character. Beautiful, no?",
  "The two lines in the theory ARE the whole function. Read them\n"
  "until you understand WHY they work, then type them yourself.",
  "0\n-1\n1\n-1",
  "/* Ex07: my_strcmp\n"
  " * TASK: compare two strings character by character.\n"
  " * Return 0 if equal, a negative number if s1 < s2,\n"
  " * a positive number if s1 > s2.\n"
  " * You may NOT call the real library function.\n"
  " */\n"
  "int my_strcmp(const char *s1, const char *s2)\n"
  "{\n"
  "    (void)s1;    /* delete these when you use them */\n"
  "    (void)s2;\n"
  "    return 0;    /* 'all strings are equal' — really? */\n"
  "}\n",
  "#include <stdio.h>\n"
  "int my_strcmp(const char *s1, const char *s2);\n"
  "static int sign(int r) { return (r > 0) - (r < 0); }\n"
  "int main(void) {\n"
  "    printf(\"%d\\n\", sign(my_strcmp(\"apple\", \"apple\")));\n"
  "    printf(\"%d\\n\", sign(my_strcmp(\"apple\", \"banana\")));\n"
  "    printf(\"%d\\n\", sign(my_strcmp(\"pear\", \"peach\")));\n"
  "    printf(\"%d\\n\", sign(my_strcmp(\"ab\", \"abc\")));\n"
  "    return 0;\n"
  "}\n" },

/* ---------- Ex08: my_strchr ---------- */
{ "ex08_strchr.c",
  "Ex08: my_strchr — finding a character (hello, pointers)",
  "strchr",
  "Real library function: strchr() from <string.h>.\n"
  "\n"
  "strchr(s, c) searches string s for the FIRST occurrence of the\n"
  "character c. It doesn't return an index — it returns a POINTER:\n"
  "the memory address of the found character. If c isn't in the\n"
  "string, it returns NULL ('points at nothing').\n"
  "\n"
  "How to make a pointer to s[i]:   &s[i]   ('&' = address of).\n"
  "Because our parameter is 'const char *' but the return type is\n"
  "plain 'char *', you need a cast:   return (char *)&s[i];\n"
  "(Yes, that's a bit ugly — the REAL strchr has the exact same\n"
  "wart. You're learning authentic library scars here.)\n"
  "\n"
  "TWO FAMOUS GOTCHAS, both tested:\n"
  "  1. If c is not found, return NULL (a name for 'no address';\n"
  "     it's available without any extra include here).\n"
  "  2. Searching for '\\0' must SUCCEED and return a pointer to\n"
  "     the terminator! Check s[i] == c BEFORE testing for the\n"
  "     end of the string.",
  "Loop i over the string. If s[i] == (char)c, return\n"
  "(char *)&s[i]. If s[i] is '\\0' (and didn't match), return NULL.\n"
  "Order matters: compare first, THEN check for the end —\n"
  "e.g. with an infinite loop:  for (i = 0;; i++) { ... }",
  "4\n-1\n11",
  "/* Ex08: my_strchr\n"
  " * TASK: return a pointer to the first occurrence of c in s,\n"
  " * or NULL if it does not occur. Searching for '\\0' must\n"
  " * return a pointer to the string's terminator.\n"
  " * You may NOT call the real library function.\n"
  " */\n"
  "#include <stddef.h>   /* gives you NULL */\n"
  "\n"
  "char *my_strchr(const char *s, int c)\n"
  "{\n"
  "    (void)s;     /* delete these when you use them */\n"
  "    (void)c;\n"
  "    return NULL; /* 'never found anything' — try harder */\n"
  "}\n",
  "#include <stdio.h>\n"
  "char *my_strchr(const char *s, int c);\n"
  "int main(void) {\n"
  "    const char *s = \"hello world\";\n"
  "    char *p = my_strchr(s, 'o');\n"
  "    printf(\"%d\\n\", p ? (int)(p - s) : -1);\n"
  "    p = my_strchr(s, 'z');\n"
  "    printf(\"%d\\n\", p ? (int)(p - s) : -1);\n"
  "    p = my_strchr(s, '\\0');\n"
  "    printf(\"%d\\n\", p ? (int)(p - s) : -1);\n"
  "    return 0;\n"
  "}\n" },

/* ---------- Ex09: my_atoi ---------- */
{ "ex09_atoi.c",
  "Ex09: my_atoi — from text to number (final boss)",
  "atoi",
  "Real library function: atoi() from <stdlib.h>.\n"
  "\n"
  "atoi(\"42\") turns the TEXT \"42\" into the NUMBER 42. How?\n"
  "\n"
  "Trick 1 — one digit: the character '7' is NOT the number 7\n"
  "(it's ASCII 55). But the digit characters sit in a row, so:\n"
  "      '7' - '0'  ==  7        <- character math again!\n"
  "\n"
  "Trick 2 — many digits: build the number left to right.\n"
  "For \"42\": start with result = 0, then for each digit:\n"
  "      result = result * 10 + (s[i] - '0');\n"
  "      step 1:  0 * 10 + 4  =  4\n"
  "      step 2:  4 * 10 + 2  =  42\n"
  "\n"
  "Trick 3 — negative numbers: if the FIRST character is '-',\n"
  "remember that (e.g. int sign = -1;), skip it (i++), and\n"
  "return sign * result at the end.\n"
  "\n"
  "Stop at the first character that is not a digit\n"
  "(digit test: s[i] >= '0' && s[i] <= '9').\n"
  "So my_atoi(\"7abc\") is 7. Good luck — this one combines\n"
  "everything you've learned!",
  "int sign = 1, result = 0, i = 0;\n"
  "if (s[0] == '-') { sign = -1; i = 1; }\n"
  "then loop while s[i] is a digit, doing\n"
  "result = result * 10 + (s[i] - '0'); i++;\n"
  "finally return sign * result;",
  "42\n-13\n7\n0",
  "/* Ex09: my_atoi (simplified)\n"
  " * TASK: convert the text in s to an int.\n"
  " * Handle an optional leading '-'. Stop at the first\n"
  " * character that is not a digit.\n"
  " * my_atoi(\"42\") -> 42,  my_atoi(\"-13\") -> -13,\n"
  " * my_atoi(\"7abc\") -> 7\n"
  " * You may NOT call the real library function.\n"
  " */\n"
  "int my_atoi(const char *s)\n"
  "{\n"
  "    (void)s;     /* delete this line when you use s */\n"
  "    return 0;    /* zero for everything? bold strategy. */\n"
  "}\n",
  "#include <stdio.h>\n"
  "int my_atoi(const char *s);\n"
  "int main(void) {\n"
  "    printf(\"%d\\n\", my_atoi(\"42\"));\n"
  "    printf(\"%d\\n\", my_atoi(\"-13\"));\n"
  "    printf(\"%d\\n\", my_atoi(\"7abc\"));\n"
  "    printf(\"%d\\n\", my_atoi(\"0\"));\n"
  "    return 0;\n"
  "}\n" },
};
static const int N_LIBEX = (int)(sizeof libex / sizeof libex[0]);

/* ---------- Level registry ---------- */
static const Level levels[] = {
  { "Hello, C — the very basics",
    "Every C program starts at main(). printf() writes formatted\n"
    "output: %d for int, %f for double, %s for strings, %c for chars.\n"
    "Integer math truncates: 7/2 is 3. Operator precedence follows\n"
    "school math: * and / before + and -.",
    q1, 4, 3 },

  { "Types & operators",
    "C has int, char, float, double and unsigned variants. % gives\n"
    "the division remainder. a++ (post) yields the old value, ++a\n"
    "(pre) the new one. Beware: = assigns, == compares — mixing them\n"
    "up compiles fine and bites hard.",
    q2, 5, 4 },

  { "Control flow",
    "if/else, while, for, switch. Two famous traps: a stray ';' after\n"
    "while() or for() creates an empty loop body, and switch cases\n"
    "FALL THROUGH unless you break.",
    q3, 4, 3 },

  { "Functions & scope",
    "C passes everything BY VALUE — functions get copies. To let a\n"
    "function modify your variable, pass its address. 'static' locals\n"
    "survive between calls. Never return the address of a local.",
    q4, 4, 3 },

  { "Pointers",
    "A pointer stores an address. &x gives x's address, *p reads or\n"
    "writes what p points at. Pointer arithmetic moves in units of\n"
    "the pointed-to type. NULL means 'points at nothing' — never\n"
    "dereference it.",
    q5, 4, 3 },

  { "Arrays & strings",
    "Arrays decay to pointers in most expressions — a[i] is literally\n"
    "*(a+i). Strings are char arrays ending in '\\0' (count it when\n"
    "allocating!). String literals are read-only.",
    q6, 5, 4 },

  { "Dynamic memory",
    "malloc() allocates, free() releases. Rules of the game: check\n"
    "for NULL, free exactly once, never use memory after freeing,\n"
    "never lose your only pointer to a block (leak).",
    q7, 4, 3 },

  { "Integer gotchas",
    "When signed meets unsigned, signed loses: it gets converted, and\n"
    "-1 becomes a huge number. sizeof yields an unsigned size_t.\n"
    "Signed overflow is undefined; unsigned wraps around.",
    q8, 4, 3 },

  { "Undefined behavior — hall of fame",
    "UB means the standard makes NO promises: crash, garbage, or\n"
    "seemingly-working code that breaks with -O2. Classics: modifying\n"
    "a variable twice in one expression, out-of-bounds access,\n"
    "uninitialized reads, and unparenthesized macros.",
    q9, 5, 4 },
};
static const int N_LEVELS = (int)(sizeof levels / sizeof levels[0]);

/* ============================================================
 *  ENGINE
 * ============================================================ */

static char progress_path[512];

static void progress_file_path(void) {
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(progress_path, sizeof progress_path, "%s/.cgame_progress", home);
}

static int load_progress(int *lvl, int *lib) {
    FILE *f = fopen(progress_path, "r");
    *lvl = 0; *lib = 0;
    if (f) {
        if (fscanf(f, "%d", lvl) != 1) *lvl = 0;
        if (fscanf(f, "%d", lib) != 1) *lib = 0;  /* old save files */
        fclose(f);
    }
    if (*lvl < 0) *lvl = 0;
    if (*lvl > N_LEVELS) *lvl = N_LEVELS;
    if (*lib < 0) *lib = 0;
    if (*lib > N_LIBEX) *lib = N_LIBEX;
    return 0;
}

static void save_progress(int lvl, int lib) {
    FILE *f = fopen(progress_path, "w");
    if (f) { fprintf(f, "%d %d\n", lvl, lib); fclose(f); }
}

static void read_line(char *buf, size_t n) {
    if (!fgets(buf, (int)n, stdin)) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\n")] = '\0';
    /* trim leading/trailing spaces */
    char *s = buf;
    while (isspace((unsigned char)*s)) s++;
    memmove(buf, s, strlen(s) + 1);
    size_t len = strlen(buf);
    while (len && isspace((unsigned char)buf[len-1])) buf[--len] = '\0';
}

static void print_code(const char *code) {
    printf("%s  ┌──────────────────────────────────────┐%s\n", C_DIM, C_RESET);
    const char *p = code;
    while (*p) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        printf("%s  │%s %s%.*s%s\n", C_DIM, C_RESET, C_CYAN, len, p, C_RESET);
        if (!nl) break;
        p = nl + 1;
    }
    printf("%s  └──────────────────────────────────────┘%s\n", C_DIM, C_RESET);
}

/* returns 1 if correct */
static int ask(const Question *q, int num, int total) {
    char buf[256];
    printf("\n%sQuestion %d/%d%s\n", C_BOLD, num, total, C_RESET);
    printf("%s\n", q->prompt);
    if (q->code) print_code(q->code);

    if (q->type == Q_CHOICE) {
        for (int i = 0; q->choices[i]; i++)
            printf("  %s\n", q->choices[i]);
        printf("%sYour answer (a-e): %s", C_YELLOW, C_RESET);
    } else {
        printf("%sYour answer: %s", C_YELLOW, C_RESET);
    }
    read_line(buf, sizeof buf);
    for (char *p = buf; *p; p++) *p = (char)tolower((unsigned char)*p);

    char expect[256];
    snprintf(expect, sizeof expect, "%s", q->answer);
    for (char *p = expect; *p; p++) *p = (char)tolower((unsigned char)*p);

    int ok = strcmp(buf, expect) == 0;
    if (ok) printf("%s✔ Correct!%s\n", C_GREEN, C_RESET);
    else    printf("%s✘ Not quite. Answer: %s%s\n", C_RED, q->answer, C_RESET);
    printf("%s%s%s\n", C_DIM, q->explain, C_RESET);
    return ok;
}

/* ============================================================
 *  SANDBOX — compile & test the player's fix (clings-style)
 * ============================================================ */

#define EX_DIR "exercises"

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void write_exercise(const Exercise *ex, int force) {
    char path[600];
    snprintf(path, sizeof path, EX_DIR "/%s", ex->file);
    mkdir(EX_DIR, 0755);                 /* ok if it already exists */
    if (!force && file_exists(path)) return;
    FILE *f = fopen(path, "w");
    if (!f) { perror("cannot write exercise"); return; }
    fputs(ex->source, f);
    fclose(f);
}

static void trim_trailing(char *s) {
    size_t len = strlen(s);
    while (len && isspace((unsigned char)s[len-1])) s[--len] = '\0';
}

/* Compiles `sources` (one or more files, space-separated), runs the
 * result with a 5s timeout, and compares stdout to `expected`.
 * Returns 1 on success. Shared by both learning paths. */
static int check_build(const char *sources, const char *expected) {
    char cmd[1200];

    /* 1) compile */
    snprintf(cmd, sizeof cmd,
        "gcc -Wall -Wextra -std=c11 -o .cgame_build %s 2> .cgame_cerr",
        sources);
    int rc = system(cmd);
    FILE *e = fopen(".cgame_cerr", "r");
    char line[512];
    int had_diag = 0;
    if (e) {
        while (fgets(line, sizeof line, e)) {
            if (!had_diag)
                printf("%s--- compiler says: ---%s\n", C_DIM, C_RESET);
            had_diag = 1;
            printf("%s%s%s", C_YELLOW, line, C_RESET);
        }
        fclose(e);
    }
    if (rc != 0) {
        printf("%s✘ It doesn't compile yet.%s\n", C_RED, C_RESET);
        return 0;
    }

    /* 2) run, with a 5s timeout to catch infinite loops */
    FILE *p = popen("timeout 5 ./.cgame_build 2>&1", "r");
    if (!p) { perror("run failed"); return 0; }
    char out[2048] = "";
    size_t got = 0, total = 0;
    char chunk[1024];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, p)) > 0) {
        total += n;
        if (got < sizeof out - 1) {           /* keep only the start */
            size_t space = sizeof out - 1 - got;
            size_t take = n < space ? n : space;
            memcpy(out + got, chunk, take);
            got += take;
        }
        if (total > 1024 * 1024) break;       /* runaway: stop reading */
    }
    out[got] = '\0';
    int status = pclose(p);
    int runaway  = total > 1024 * 1024;
    int timedout = WIFEXITED(status) && WEXITSTATUS(status) == 124;

    if (timedout || runaway) {
        printf("%s✘ Your program %s — looks like an infinite loop!%s\n",
               C_RED,
               runaway ? "flooded the output"
                       : "ran for 5 seconds and was killed",
               C_RESET);
        return 0;
    }
    if (WIFSIGNALED(status)) {
        printf("%s✘ Your program was killed by signal %d (a crash —\n"
               "  segfault? out-of-bounds access?).%s\n",
               C_RED, WTERMSIG(status), C_RESET);
        return 0;
    }
    int exitcode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (exitcode != 0) {
        printf("%s✘ Your program exited with code %d (crash or error).%s\n",
               C_RED, exitcode, C_RESET);
        if (got) printf("Output start:\n%.400s%s\n",
                        out, got > 400 ? "\n[...truncated]" : "");
        return 0;
    }

    /* 3) compare */
    char expect[512];
    snprintf(expect, sizeof expect, "%s", expected);
    trim_trailing(out);
    trim_trailing(expect);
    if (strcmp(out, expect) == 0) {
        printf("%s✔ Compiles, runs, and prints exactly the right thing!%s\n",
               C_GREEN, C_RESET);
        return 1;
    }
    printf("%s✘ It runs, but the output is wrong.%s\n", C_RED, C_RESET);
    printf("%sExpected:%s\n%s\n%sGot:%s\n%s\n",
           C_BOLD, C_RESET, expect, C_BOLD, C_RESET,
           out[0] ? out : "(nothing)");
    return 0;
}

/* returns 1 if solved (or deliberately skipped), 0 if the player quit */
static int sandbox(int idx) {
    if (idx >= (int)(sizeof exercises / sizeof exercises[0]))
        return 1;                        /* no exercise for this level */
    const Exercise *ex = &exercises[idx];
    write_exercise(ex, 0);

    printf("\n%s%s────── Sandbox: fix the broken code! ──────%s\n",
           C_BOLD, C_MAG, C_RESET);
    printf("A broken C file has been written to:\n"
           "  %s%s" EX_DIR "/%s%s\n", C_BOLD, C_CYAN, ex->file, C_RESET);
    printf("\nOpen it in your editor and fix it so that:\n%s%s%s\n",
           C_GREEN, ex->task, C_RESET);
    printf("\n%s[Enter]%s check my fix   %s[h]%s hint   %s[r]%s reset file\n"
           "%s[s]%s skip exercise      %s[q]%s back to menu\n",
           C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET,
           C_BOLD, C_RESET, C_BOLD, C_RESET);

    char buf[64];
    for (;;) {
        printf("%ssandbox> %s", C_YELLOW, C_RESET);
        read_line(buf, sizeof buf);
        if (buf[0] == '\0') {
            char sources[700];
            snprintf(sources, sizeof sources, EX_DIR "/%s", ex->file);
            if (check_build(sources, ex->expected)) return 1;
            printf("%sEdit " EX_DIR "/%s and press Enter to re-check.%s\n",
                   C_DIM, ex->file, C_RESET);
        } else if (buf[0] == 'h') {
            printf("%sHint: %s%s\n", C_CYAN, ex->hint, C_RESET);
        } else if (buf[0] == 'r') {
            write_exercise(ex, 1);
            printf("File reset to its original broken state.\n");
        } else if (buf[0] == 's') {
            printf("%sSkipped — but the bug is still out there...%s\n",
                   C_DIM, C_RESET);
            return 1;
        } else if (buf[0] == 'q') {
            return 0;
        } else {
            printf("Enter, h, r, s, or q.\n");
        }
    }
}

/* ============================================================
 *  LIBRARY PATH ENGINE — theory, stub, compile against tests
 * ============================================================ */

#define LIB_DIR "library"

static void write_lib_stub(const LibExercise *ex, int force) {
    char path[600];
    snprintf(path, sizeof path, LIB_DIR "/%s", ex->file);
    mkdir(LIB_DIR, 0755);
    if (!force && file_exists(path)) return;
    FILE *f = fopen(path, "w");
    if (!f) { perror("cannot write exercise"); return; }
    fputs(ex->stub, f);
    fclose(f);
}

/* Does the player's file CALL the real library function `name`?
 * A call is: `name` not preceded by an identifier character
 * (so my_strlen doesn't count) and followed by '(' (spaces ok). */
static int calls_forbidden(const char *path, const char *name) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    static char buf[65536];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    fclose(f);

    size_t nlen = strlen(name);
    for (const char *p = buf; (p = strstr(p, name)) != NULL; p += nlen) {
        if (p > buf) {
            char prev = p[-1];
            if (isalnum((unsigned char)prev) || prev == '_')
                continue;                      /* part of my_xxx etc. */
        }
        const char *q = p + nlen;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '(')
            return 1;                          /* looks like a call  */
    }
    return 0;
}

/* returns 1 if solved (or skipped), 0 if the player quit */
static int play_libex(int idx) {
    const LibExercise *ex = &libex[idx];

    printf("\n%s%s══════ %s ══════%s\n", C_BOLD, C_MAG, ex->title, C_RESET);
    printf("\n%s\n", ex->theory);
    printf("\n%s(Press Enter when you're ready to write it yourself)%s",
           C_DIM, C_RESET);
    char buf[64];
    read_line(buf, sizeof buf);

    write_lib_stub(ex, 0);
    printf("\nYour file is waiting at:\n"
           "  %s%s" LIB_DIR "/%s%s\n", C_BOLD, C_CYAN, ex->file, C_RESET);
    printf("Open it in your editor, write the function, come back.\n");
    if (ex->forbidden)
        printf("%s(Calling the real %s() is detected and rejected!)%s\n",
               C_DIM, ex->forbidden, C_RESET);
    printf("\n%s[Enter]%s check my code   %s[t]%s show theory again   %s[h]%s hint\n"
           "%s[r]%s reset file          %s[s]%s skip   %s[q]%s back to menu\n",
           C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET,
           C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET);

    for (;;) {
        printf("%slibrary> %s", C_YELLOW, C_RESET);
        read_line(buf, sizeof buf);
        if (buf[0] == '\0') {
            char path[600];
            snprintf(path, sizeof path, LIB_DIR "/%s", ex->file);
            if (ex->forbidden && calls_forbidden(path, ex->forbidden)) {
                printf("%s✘ Nice try — you're calling the real %s()!\n"
                       "  The whole point is to build it yourself. ;)%s\n",
                       C_RED, ex->forbidden, C_RESET);
                continue;
            }
            /* write the hidden test program and build both together */
            FILE *t = fopen(LIB_DIR "/.test.c", "w");
            if (t) { fputs(ex->test, t); fclose(t); }
            char sources[700];
            snprintf(sources, sizeof sources,
                     LIB_DIR "/%s " LIB_DIR "/.test.c", ex->file);
            if (check_build(sources, ex->expected)) return 1;
            printf("%sEdit " LIB_DIR "/%s and press Enter to re-check.%s\n",
                   C_DIM, ex->file, C_RESET);
        } else if (buf[0] == 't') {
            printf("\n%s\n", ex->theory);
        } else if (buf[0] == 'h') {
            printf("%sHint:\n%s%s\n", C_CYAN, ex->hint, C_RESET);
        } else if (buf[0] == 'r') {
            write_lib_stub(ex, 1);
            printf("File reset to the empty starting version.\n");
        } else if (buf[0] == 's') {
            printf("%sSkipped — the library builds itself, apparently.%s\n",
                   C_DIM, C_RESET);
            return 1;
        } else if (buf[0] == 'q') {
            return 0;
        } else {
            printf("Enter, t, h, r, s, or q.\n");
        }
    }
}

/* returns 1 if level passed */
static int play_level(int idx) {
    const Level *L = &levels[idx];
    printf("\n%s%s══════ Level %d: %s ══════%s\n",
           C_BOLD, C_MAG, idx + 1, L->title, C_RESET);
    printf("\n%s\n", L->lesson);
    printf("%s(You need %d/%d correct to pass. Press Enter to start)%s",
           C_DIM, L->pass_needed, L->n_questions, C_RESET);
    char tmp[16]; read_line(tmp, sizeof tmp);

    int score = 0;
    for (int i = 0; i < L->n_questions; i++)
        score += ask(&L->questions[i], i + 1, L->n_questions);

    printf("\n%sScore: %d/%d%s — ", C_BOLD, score, L->n_questions, C_RESET);
    if (score < L->pass_needed) {
        printf("%snot enough this time. Try again!%s\n", C_RED, C_RESET);
        return 0;
    }
    printf("%squiz passed!%s Now prove it in code...\n", C_GREEN, C_RESET);

    if (!sandbox(idx)) {
        printf("%sSandbox left unsolved — level not completed yet.%s\n",
               C_YELLOW, C_RESET);
        return 0;
    }
    printf("\n%s%sLEVEL %d COMPLETE! 🎉%s\n", C_BOLD, C_GREEN, idx + 1, C_RESET);
    return 1;
}

static void banner(int lvl_done, int lib_done) {
    printf("\n%s%s", C_BOLD, C_CYAN);
    printf("   ██████╗ ██████╗  █████╗ ███╗   ███╗███████╗\n");
    printf("  ██╔════╝██╔════╝ ██╔══██╗████╗ ████║██╔════╝\n");
    printf("  ██║     ██║  ███╗███████║██╔████╔██║█████╗  \n");
    printf("  ██║     ██║   ██║██╔══██║██║╚██╔╝██║██╔══╝  \n");
    printf("  ╚██████╗╚██████╔╝██║  ██║██║ ╚═╝ ██║███████╗\n");
    printf("   ╚═════╝ ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝\n");
    printf("%s        Learn C, in C — gotchas included\n%s", C_DIM, C_RESET);
    printf("\n  Gotcha levels: %d/%d    Library built: %d/%d\n",
           lvl_done, N_LEVELS, lib_done, N_LIBEX);
}

static void menu(int unlocked) {
    printf("\n%sGotcha levels:%s\n", C_BOLD, C_RESET);
    for (int i = 0; i < N_LEVELS; i++) {
        if (i < unlocked)
            printf("  %s✔%s %2d) %s\n", C_GREEN, C_RESET, i + 1, levels[i].title);
        else if (i == unlocked)
            printf("  %s▶%s %2d) %s\n", C_YELLOW, C_RESET, i + 1, levels[i].title);
        else
            printf("  %s🔒 %2d) %s%s\n", C_DIM, i + 1, levels[i].title, C_RESET);
    }
    printf("\n  Type a level number to play (unlocked/current only),\n"
           "  or %sb%s to go back to the main menu.\n",
           C_BOLD, C_RESET);
}

static void lib_menu(int unlocked) {
    printf("\n%sBuild your own C library:%s\n", C_BOLD, C_RESET);
    for (int i = 0; i < N_LIBEX; i++) {
        if (i < unlocked)
            printf("  %s✔%s %2d) %s\n", C_GREEN, C_RESET, i, libex[i].title);
        else if (i == unlocked)
            printf("  %s▶%s %2d) %s\n", C_YELLOW, C_RESET, i, libex[i].title);
        else
            printf("  %s🔒 %2d) %s%s\n", C_DIM, i, libex[i].title, C_RESET);
    }
    printf("\n  Type an exercise number (0-%d, unlocked/current only),\n"
           "  or %sb%s to go back to the main menu.\n",
           N_LIBEX - 1, C_BOLD, C_RESET);
}

static void levels_path(int *lvl_done, int lib_done) {
    char buf[64];
    for (;;) {
        menu(*lvl_done);
        printf("%slevels> %s", C_YELLOW, C_RESET);
        read_line(buf, sizeof buf);
        if (buf[0] == 'b' || buf[0] == 'q') return;
        int choice = atoi(buf);
        if (choice < 1 || choice > N_LEVELS) {
            printf("%sPlease enter 1-%d or b.%s\n", C_RED, N_LEVELS, C_RESET);
            continue;
        }
        if (choice - 1 > *lvl_done) {
            printf("%sThat level is still locked — beat level %d first!%s\n",
                   C_RED, *lvl_done + 1, C_RESET);
            continue;
        }
        if (play_level(choice - 1) && choice - 1 == *lvl_done) {
            (*lvl_done)++;
            save_progress(*lvl_done, lib_done);
            if (*lvl_done == N_LEVELS)
                printf("\n%s%s🏆 ALL LEVELS COMPLETE — you know C's dark corners now!%s\n",
                       C_BOLD, C_GREEN, C_RESET);
        }
    }
}

static void library_path(int lvl_done, int *lib_done) {
    char buf[64];
    for (;;) {
        lib_menu(*lib_done);
        printf("%slibrary> %s", C_YELLOW, C_RESET);
        read_line(buf, sizeof buf);
        if (buf[0] == 'b' || buf[0] == 'q') return;
        if (!isdigit((unsigned char)buf[0])) {
            printf("%sPlease enter 0-%d or b.%s\n", C_RED, N_LIBEX - 1, C_RESET);
            continue;
        }
        int choice = atoi(buf);
        if (choice < 0 || choice >= N_LIBEX) {
            printf("%sPlease enter 0-%d or b.%s\n", C_RED, N_LIBEX - 1, C_RESET);
            continue;
        }
        if (choice > *lib_done) {
            printf("%sStill locked — finish Ex%02d first!%s\n",
                   C_RED, *lib_done, C_RESET);
            continue;
        }
        if (play_libex(choice) && choice == *lib_done) {
            printf("\n%s%sEx%02d COMPLETE! 🎉%s\n",
                   C_BOLD, C_GREEN, choice, C_RESET);
            (*lib_done)++;
            save_progress(lvl_done, *lib_done);
            if (*lib_done == N_LIBEX)
                printf("\n%s%s🏆 YOU BUILT YOUR OWN C LIBRARY — from zero"
                       " to strchr!%s\n", C_BOLD, C_GREEN, C_RESET);
        }
    }
}

int main(void) {
    if (getenv("NO_COLOR")) use_color = 0;
    progress_file_path();
    int lvl_done, lib_done;
    load_progress(&lvl_done, &lib_done);

    banner(lvl_done, lib_done);

    char buf[64];
    for (;;) {
        printf("\n%sChoose your path:%s\n", C_BOLD, C_RESET);
        printf("  %s1%s) Gotcha levels — quizzes + fix broken code  (%d/%d)\n",
               C_BOLD, C_RESET, lvl_done, N_LEVELS);
        printf("  %s2%s) Build your own C library — write stdlib\n"
               "     functions from scratch, zero experience needed  (%d/%d)\n",
               C_BOLD, C_RESET, lib_done, N_LIBEX);
        printf("  %sr%s) reset all progress    %sq%s) quit\n",
               C_BOLD, C_RESET, C_BOLD, C_RESET);
        printf("%s> %s", C_YELLOW, C_RESET);
        read_line(buf, sizeof buf);

        if (buf[0] == 'q' || buf[0] == 'Q') {
            printf("Progress saved. Happy hacking!\n");
            break;
        }
        if (buf[0] == 'r' || buf[0] == 'R') {
            lvl_done = 0; lib_done = 0;
            save_progress(0, 0);
            printf("Progress reset.\n");
            continue;
        }
        if (buf[0] == '1')      levels_path(&lvl_done, lib_done);
        else if (buf[0] == '2') library_path(lvl_done, &lib_done);
        else printf("%sPlease enter 1, 2, r, or q.%s\n", C_RED, C_RESET);
    }
    return 0;
}
