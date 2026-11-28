#!/usr/bin/env python3
"""Case manifest for the adversarial RAII cleanup-order suite.

Each body is wrapped by run_suite.py with the shared resource prelude and the
live-allocation ledger main(). Expectations are exact stdout, including the
per-case rc and live=0.
"""

CASES = [
    dict(name="nested_if_return", expect="A\nD3\nD2\nD1\nrc=7 live=0\n", body=r'''
static int scenario(void){
  R a = R_new(1);
  if (a.id) {
    R b = R_new(2);
    if (b.id) {
      R c = R_new(3);
      printf("A\n");
      return 7;
    } else {
      R d = R_new(4);
      printf("B\n");
    }
    printf("C\n");
  } else {
    R e = R_new(5);
    printf("D\n");
  }
  printf("E\n");
  return 0;
}
'''),
    dict(name="nested_else_return", expect="F\nD4\nD3\nD1\nrc=9 live=0\n", body=r'''
static int scenario(void){
  R a = R_new(1);
  int c = 0;
  if (c) { R b = R_new(2); printf("T\n"); }
  else { R d = R_new(3); { R e = R_new(4); printf("F\n"); return 9; } printf("NF\n"); }
  return 0;
}
'''),
    dict(name="nested_if_false", expect="F\nD4\nAFTER\nD3\nEND\nD1\nrc=0 live=0\n", body=r'''
static int scenario(void){
  R a = R_new(1);
  int c = 0;
  if (c) {
    R b = R_new(2); printf("T\n");
  } else {
    R d = R_new(3);
    { R e = R_new(4); printf("F\n"); }
    printf("AFTER\n");
  }
  printf("END\n");
  return 0;
}
'''),
    dict(name="for_break",
         expect="I1\nD11\nX1\nD10\nB2\nD21\nD20\nEND\nrc=0 live=0\n", body=r'''
static int scenario(void){
  for (int i = 1; i <= 3; ++i) {
    R x = R_new(i * 10);
    { R y = R_new(i * 10 + 1);
      if (i == 2) { printf("B%d\n", i); break; }
      printf("I%d\n", i);
    }
    printf("X%d\n", i);
  }
  printf("END\n"); return 0;
}
'''),
    dict(name="for_continue",
         expect="I1\nD11\nX1\nD10\nC2\nD21\nD20\nI3\nD31\nX3\nD30\nEND\nrc=0 live=0\n", body=r'''
static int scenario(void){
  for (int i = 1; i <= 3; ++i) {
    R x = R_new(i * 10);
    { R y = R_new(i * 10 + 1);
      if (i == 2) { printf("C%d\n", i); continue; }
      printf("I%d\n", i);
    }
    printf("X%d\n", i);
  }
  printf("END\n"); return 0;
}
'''),
    dict(name="return_inside_for",
         expect="D11\nX1\nD10\nRET\nD21\nD20\nD1\nrc=5 live=0\n", body=r'''
static int scenario(void){
  R a = R_new(1);
  for (int i = 1; i <= 3; ++i) {
    R x = R_new(i * 10);
    { R y = R_new(i * 10 + 1); if (i == 2) { printf("RET\n"); return 5; } }
    printf("X%d\n", i);
  }
  return 0;
}
'''),
    dict(name="while_break",
         expect="I1\nD11\nX1\nD10\nB\nD21\nD20\nEND\nrc=0 live=0\n", body=r'''
static int scenario(void){
  int i = 0;
  while (i < 3) {
    ++i;
    R x = R_new(i * 10);
    { R y = R_new(i * 10 + 1);
      if (i == 2) { printf("B\n"); break; }
      printf("I%d\n", i);
    }
    printf("X%d\n", i);
  }
  printf("END\n"); return 0;
}
'''),
    dict(name="while_continue",
         expect="I1\nD11\nX1\nD10\nC\nD21\nD20\nI3\nD31\nX3\nD30\nEND\nrc=0 live=0\n", body=r'''
static int scenario(void){
  int i = 0;
  while (i < 3) {
    ++i;
    R x = R_new(i * 10);
    { R y = R_new(i * 10 + 1);
      if (i == 2) { printf("C\n"); continue; }
      printf("I%d\n", i);
    }
    printf("X%d\n", i);
  }
  printf("END\n"); return 0;
}
'''),
    dict(name="switch_break",
         expect="TWO\nD4\nD3\nEND\nD1\nrc=0 live=0\n", body=r'''
static int scenario(void){
  int c = 2;
  R a = R_new(1);
  switch (c) {
    case 1: { R b = R_new(2); printf("ONE\n"); break; }
    case 2: { R d = R_new(3); { R e = R_new(4); printf("TWO\n"); break; } printf("NF\n"); }
    default: { R f = R_new(5); printf("DEF\n"); break; }
  }
  printf("END\n"); return 0;
}
'''),
    dict(name="switch_in_loop_break",
         expect="TWO1\nD16\nD15\nI1\nD10\nTWO2\nD26\nD25\nI2\nD20\nEND\nD1\nrc=0 live=0\n", body=r'''
static int scenario(void){
  int c = 2;
  R a = R_new(1);
  for (int i = 1; i <= 2; ++i) {
    R x = R_new(i * 10);
    switch (c) {
      case 1: { R y = R_new(90); printf("ONE\n"); break; }
      case 2: { R z = R_new(i * 10 + 5); { R w = R_new(i * 10 + 6); printf("TWO%d\n", i); break; } }
      default: { R q = R_new(80); printf("DEF\n"); break; }
    }
    printf("I%d\n", i);
  }
  printf("END\n"); return 0;
}
'''),
    dict(name="goto_cleanup", expect="G\nD3\nD2\nOUT\nD1\nrc=0 live=0\n",
         no_werror=True, body=r'''
static int scenario(void){
  R a = R_new(1);
  { R b = R_new(2);
    { R c = R_new(3); printf("G\n"); goto L; }
    printf("NO\n");
  }
 L: printf("OUT\n"); return 0;
}
'''),
    dict(name="goto_same_block", expect="A\nB\nD1\nrc=0 live=0\n",
         no_werror=True, body=r'''
static int scenario(void){
  R a = R_new(1);
  printf("A\n");
  goto L;
 L: printf("B\n");
  return 0;
}
'''),
    dict(name="goto_from_loop", expect="X1\nD10\nG2\nD20\nOUT\nD1\nrc=0 live=0\n",
         no_werror=True, body=r'''
static int scenario(void){
  R a = R_new(1);
  for (int i = 1; i <= 3; ++i) {
    R x = R_new(i * 10);
    if (i == 2) { printf("G%d\n", i); goto L; }
    printf("X%d\n", i);
  }
 L: printf("OUT\n"); return 0;
}
'''),
    dict(name="goto_cleanup_chain", expect="D2\nINNER\nD3\nOUT\nD1\nrc=0 live=0\n",
         no_werror=True, body=r'''
static int scenario(void){
  R a = R_new(1);
  if (a.id) {
    R b = R_new(2);
    goto inner;
  }
 inner: {
    R c = R_new(3);
    printf("INNER\n");
    goto L;
  }
 L: printf("OUT\n"); return 0;
}
'''),
]
