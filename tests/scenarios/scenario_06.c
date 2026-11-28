/* scenario_06.c -- NEGATIVE FIXTURE: pointer escapes local stack lifetime.
 *
 * This file is a compile-time-only negative test fixture.  Every escape_<n>
 * function below creates a function-local object (fixed array, VLA, alloca
 * block, compound literal or scalar) and lets its address outlive the scope
 * that owns it: by returning the address, storing it into a global/static
 * pointer, or handing it to a helper "sink" that retains it.  In every case
 * the resulting pointer dangles as soon as the function returns.
 *
 * `c+ check tests/scenarios/scenario_06.c --default` MUST REJECT this file
 * with [CPLUS-...] diagnostics.  This file must NEVER be built or run; a
 * successful build or execution would be a safety failure, not a pass.
 *
 * The cases deliberately cover many escape shapes (char/int/struct arrays,
 * VLAs, nested scopes, compound literals, alloca, pointer-to-pointer and
 * helper sinks) so the checker's coverage can be measured honestly.
 */

#include <stdlib.h>
#include <alloca.h>

struct S { int a; char b[8]; double c; };

/* Global and static escape sinks. */
static char *g_char;
static int *g_int;
static short *g_short;
static long *g_long;
static float *g_float;
static double *g_double;
static unsigned char *g_uchar;
static unsigned int *g_uint;
static struct S *g_struct;
static void **g_voidpp;

/* Helper sinks that retain the pointer they are given. */
static void sink_char(char *p) { g_char = p; }
static void sink_int(int *p) { g_int = p; }
static void sink_short(short *p) { g_short = p; }
static void sink_long(long *p) { g_long = p; }
static void sink_float(float *p) { g_float = p; }
static void sink_double(double *p) { g_double = p; }
static void sink_uchar(unsigned char *p) { g_uchar = p; }
static void sink_uint(unsigned int *p) { g_uint = p; }
static void sink_struct(struct S *p) { g_struct = p; }

/* CASE 1: [VLA] VLA of char returned directly */
char *escape_1(int sz) { char b[sz]; return b; }

/* CASE 2: [VLA] VLA of int stored into global pointer */
void escape_2(int sz) { int b[sz]; g_int = b; }

/* CASE 3: [VLA] nested-scope VLA of short stored into global pointer */
void escape_3(int sz) { short b[sz]; { short c[sz]; g_short = c; } (void)b; }

/* CASE 4: [VLA] VLA of long returned through a local alias pointer */
long *escape_4(int sz) { long b[sz]; long *p = b; return p; }

/* CASE 5: [VLA] VLA of float passed to helper function sink */
void escape_5(int sz) { float b[sz]; sink_float(b); }

/* CASE 6: [VLA] VLA of double stored into function-static pointer */
void escape_6(int sz) { double b[sz]; static double *sp; sp = b; (void)sp; }

/* CASE 7: [VLA] VLA of unsigned char address-of-local-pointer returned (pointer-to-pointer) */
unsigned char **escape_7(int sz) { unsigned char b[sz]; unsigned char *p = b; return &p; }

/* CASE 8: [VLA] VLA of unsigned int address-of-local-pointer stored into global void** */
void escape_8(int sz) { unsigned int b[sz]; unsigned int *p = b; g_voidpp = (void **)&p; }

/* CASE 9: [alloca] alloca block of struct S returned directly */
struct S *escape_9(void) { struct S *p = alloca(4); return p; }

/* CASE 10: [alloca] alloca block of char stored into global pointer */
void escape_10(void) { char *p = alloca(7); g_char = p; }

/* CASE 11: [alloca] alloca block of int passed to helper function sink */
void escape_11(void) { int *p = alloca(10); sink_int(p); }

/* CASE 12: [alloca] alloca block of short stored into function-static pointer */
void escape_12(void) { short *p = alloca(13); static short *sp; sp = p; (void)sp; }

/* CASE 13: [alloca] alloca block of long aliased in nested scope then stored globally */
void escape_13(void) { long *p = alloca(16); { long *q = p; g_long = q; } }

/* CASE 14: [alloca] alloca block of float returned through pointer-to-pointer */
float **escape_14(void) { float *p = alloca(19); return &p; }

/* CASE 15: [alloca] alloca block of double pointer-to-pointer stored into global void** */
void escape_15(void) { double *p = alloca(22); g_voidpp = (void **)&p; }

/* CASE 16: [fixed] fixed unsigned char array returned directly */
unsigned char *escape_16(void) { unsigned char b[25]; return b; }

/* CASE 17: [fixed] fixed unsigned int array stored into global pointer */
void escape_17(void) { unsigned int b[28]; g_uint = b; }

/* CASE 18: [fixed] fixed struct S array passed to helper function sink */
void escape_18(void) { struct S b[4]; sink_struct(b); }

/* CASE 19: [fixed] fixed char array stored into function-static pointer */
void escape_19(void) { char b[7]; static char *sp; sp = b; (void)sp; }

/* CASE 20: [fixed] nested-scope fixed int array returned */
int *escape_20(void) { int b[10]; { int c[10]; return c; } }

/* CASE 21: [fixed] fixed short array address-of-local-pointer returned */
short **escape_21(void) { short b[13]; short *p = b; return &p; }

/* CASE 22: [fixed] address of local long scalar returned */
long *escape_22(void) { long x = (long)0; return &x; }

/* CASE 23: [fixed] address of local float scalar stored into global pointer */
void escape_23(void) { float x = (float)0; g_float = &x; }

/* CASE 24: [compound] address of double compound literal returned */
double *escape_24(void) { return &(double){0}; }

/* CASE 25: [compound] unsigned char compound-literal array stored into global pointer */
void escape_25(void) { g_uchar = (unsigned char[25]){0}; }

/* CASE 26: [compound] address of unsigned int compound literal passed to helper sink */
void escape_26(void) { sink_uint(&(unsigned int){0}); }

/* CASE 27: [VLA] VLA of struct S returned directly */
struct S *escape_27(int sz) { struct S b[sz]; return b; }

/* CASE 28: [VLA] VLA of char stored into global pointer */
void escape_28(int sz) { char b[sz]; g_char = b; }

/* CASE 29: [VLA] nested-scope VLA of int stored into global pointer */
void escape_29(int sz) { int b[sz]; { int c[sz]; g_int = c; } (void)b; }

/* CASE 30: [VLA] VLA of short returned through a local alias pointer */
short *escape_30(int sz) { short b[sz]; short *p = b; return p; }

/* CASE 31: [VLA] VLA of long passed to helper function sink */
void escape_31(int sz) { long b[sz]; sink_long(b); }

/* CASE 32: [VLA] VLA of float stored into function-static pointer */
void escape_32(int sz) { float b[sz]; static float *sp; sp = b; (void)sp; }

/* CASE 33: [VLA] VLA of double address-of-local-pointer returned (pointer-to-pointer) */
double **escape_33(int sz) { double b[sz]; double *p = b; return &p; }

/* CASE 34: [VLA] VLA of unsigned char address-of-local-pointer stored into global void** */
void escape_34(int sz) { unsigned char b[sz]; unsigned char *p = b; g_voidpp = (void **)&p; }

/* CASE 35: [alloca] alloca block of unsigned int returned directly */
unsigned int *escape_35(void) { unsigned int *p = alloca(28); return p; }

/* CASE 36: [alloca] alloca block of struct S stored into global pointer */
void escape_36(void) { struct S *p = alloca(4); g_struct = p; }

/* CASE 37: [alloca] alloca block of char passed to helper function sink */
void escape_37(void) { char *p = alloca(7); sink_char(p); }

/* CASE 38: [alloca] alloca block of int stored into function-static pointer */
void escape_38(void) { int *p = alloca(10); static int *sp; sp = p; (void)sp; }

/* CASE 39: [alloca] alloca block of short aliased in nested scope then stored globally */
void escape_39(void) { short *p = alloca(13); { short *q = p; g_short = q; } }

/* CASE 40: [alloca] alloca block of long returned through pointer-to-pointer */
long **escape_40(void) { long *p = alloca(16); return &p; }

/* CASE 41: [alloca] alloca block of float pointer-to-pointer stored into global void** */
void escape_41(void) { float *p = alloca(19); g_voidpp = (void **)&p; }

/* CASE 42: [fixed] fixed double array returned directly */
double *escape_42(void) { double b[22]; return b; }

/* CASE 43: [fixed] fixed unsigned char array stored into global pointer */
void escape_43(void) { unsigned char b[25]; g_uchar = b; }

/* CASE 44: [fixed] fixed unsigned int array passed to helper function sink */
void escape_44(void) { unsigned int b[28]; sink_uint(b); }

/* CASE 45: [fixed] fixed struct S array stored into function-static pointer */
void escape_45(void) { struct S b[4]; static struct S *sp; sp = b; (void)sp; }

/* CASE 46: [fixed] nested-scope fixed char array returned */
char *escape_46(void) { char b[7]; { char c[7]; return c; } }

/* CASE 47: [fixed] fixed int array address-of-local-pointer returned */
int **escape_47(void) { int b[10]; int *p = b; return &p; }

/* CASE 48: [fixed] address of local short scalar returned */
short *escape_48(void) { short x = (short)0; return &x; }

/* CASE 49: [fixed] address of local long scalar stored into global pointer */
void escape_49(void) { long x = (long)0; g_long = &x; }

/* CASE 50: [compound] address of float compound literal returned */
float *escape_50(void) { return &(float){0}; }

/* CASE 51: [compound] double compound-literal array stored into global pointer */
void escape_51(void) { g_double = (double[22]){0}; }

/* CASE 52: [compound] address of unsigned char compound literal passed to helper sink */
void escape_52(void) { sink_uchar(&(unsigned char){0}); }

/* CASE 53: [VLA] VLA of unsigned int returned directly */
unsigned int *escape_53(int sz) { unsigned int b[sz]; return b; }

/* CASE 54: [VLA] VLA of struct S stored into global pointer */
void escape_54(int sz) { struct S b[sz]; g_struct = b; }

/* CASE 55: [VLA] nested-scope VLA of char stored into global pointer */
void escape_55(int sz) { char b[sz]; { char c[sz]; g_char = c; } (void)b; }

/* CASE 56: [VLA] VLA of int returned through a local alias pointer */
int *escape_56(int sz) { int b[sz]; int *p = b; return p; }

/* CASE 57: [VLA] VLA of short passed to helper function sink */
void escape_57(int sz) { short b[sz]; sink_short(b); }

/* CASE 58: [VLA] VLA of long stored into function-static pointer */
void escape_58(int sz) { long b[sz]; static long *sp; sp = b; (void)sp; }

/* CASE 59: [VLA] VLA of float address-of-local-pointer returned (pointer-to-pointer) */
float **escape_59(int sz) { float b[sz]; float *p = b; return &p; }

/* CASE 60: [VLA] VLA of double address-of-local-pointer stored into global void** */
void escape_60(int sz) { double b[sz]; double *p = b; g_voidpp = (void **)&p; }

/* CASE 61: [alloca] alloca block of unsigned char returned directly */
unsigned char *escape_61(void) { unsigned char *p = alloca(25); return p; }

/* CASE 62: [alloca] alloca block of unsigned int stored into global pointer */
void escape_62(void) { unsigned int *p = alloca(28); g_uint = p; }

/* CASE 63: [alloca] alloca block of struct S passed to helper function sink */
void escape_63(void) { struct S *p = alloca(4); sink_struct(p); }

/* CASE 64: [alloca] alloca block of char stored into function-static pointer */
void escape_64(void) { char *p = alloca(7); static char *sp; sp = p; (void)sp; }

/* CASE 65: [alloca] alloca block of int aliased in nested scope then stored globally */
void escape_65(void) { int *p = alloca(10); { int *q = p; g_int = q; } }

/* CASE 66: [alloca] alloca block of short returned through pointer-to-pointer */
short **escape_66(void) { short *p = alloca(13); return &p; }

/* CASE 67: [alloca] alloca block of long pointer-to-pointer stored into global void** */
void escape_67(void) { long *p = alloca(16); g_voidpp = (void **)&p; }

/* CASE 68: [fixed] fixed float array returned directly */
float *escape_68(void) { float b[19]; return b; }

/* CASE 69: [fixed] fixed double array stored into global pointer */
void escape_69(void) { double b[22]; g_double = b; }

/* CASE 70: [fixed] fixed unsigned char array passed to helper function sink */
void escape_70(void) { unsigned char b[25]; sink_uchar(b); }

/* CASE 71: [fixed] fixed unsigned int array stored into function-static pointer */
void escape_71(void) { unsigned int b[28]; static unsigned int *sp; sp = b; (void)sp; }

/* CASE 72: [fixed] nested-scope fixed struct S array returned */
struct S *escape_72(void) { struct S b[4]; { struct S c[4]; return c; } }

/* CASE 73: [fixed] fixed char array address-of-local-pointer returned */
char **escape_73(void) { char b[7]; char *p = b; return &p; }

/* CASE 74: [fixed] address of local int scalar returned */
int *escape_74(void) { int x = (int)0; return &x; }

/* CASE 75: [fixed] address of local short scalar stored into global pointer */
void escape_75(void) { short x = (short)0; g_short = &x; }

/* CASE 76: [compound] address of long compound literal returned */
long *escape_76(void) { return &(long){0}; }

/* CASE 77: [compound] float compound-literal array stored into global pointer */
void escape_77(void) { g_float = (float[19]){0}; }

/* CASE 78: [compound] address of double compound literal passed to helper sink */
void escape_78(void) { sink_double(&(double){0}); }

/* CASE 79: [VLA] VLA of unsigned char returned directly */
unsigned char *escape_79(int sz) { unsigned char b[sz]; return b; }

/* CASE 80: [VLA] VLA of unsigned int stored into global pointer */
void escape_80(int sz) { unsigned int b[sz]; g_uint = b; }

/* CASE 81: [VLA] nested-scope VLA of struct S stored into global pointer */
void escape_81(int sz) { struct S b[sz]; { struct S c[sz]; g_struct = c; } (void)b; }

/* CASE 82: [VLA] VLA of char returned through a local alias pointer */
char *escape_82(int sz) { char b[sz]; char *p = b; return p; }

/* CASE 83: [VLA] VLA of int passed to helper function sink */
void escape_83(int sz) { int b[sz]; sink_int(b); }

/* CASE 84: [VLA] VLA of short stored into function-static pointer */
void escape_84(int sz) { short b[sz]; static short *sp; sp = b; (void)sp; }

/* CASE 85: [VLA] VLA of long address-of-local-pointer returned (pointer-to-pointer) */
long **escape_85(int sz) { long b[sz]; long *p = b; return &p; }

/* CASE 86: [VLA] VLA of float address-of-local-pointer stored into global void** */
void escape_86(int sz) { float b[sz]; float *p = b; g_voidpp = (void **)&p; }

/* CASE 87: [alloca] alloca block of double returned directly */
double *escape_87(void) { double *p = alloca(22); return p; }

/* CASE 88: [alloca] alloca block of unsigned char stored into global pointer */
void escape_88(void) { unsigned char *p = alloca(25); g_uchar = p; }

/* CASE 89: [alloca] alloca block of unsigned int passed to helper function sink */
void escape_89(void) { unsigned int *p = alloca(28); sink_uint(p); }

/* CASE 90: [alloca] alloca block of struct S stored into function-static pointer */
void escape_90(void) { struct S *p = alloca(4); static struct S *sp; sp = p; (void)sp; }

/* CASE 91: [alloca] alloca block of char aliased in nested scope then stored globally */
void escape_91(void) { char *p = alloca(7); { char *q = p; g_char = q; } }

/* CASE 92: [alloca] alloca block of int returned through pointer-to-pointer */
int **escape_92(void) { int *p = alloca(10); return &p; }

/* CASE 93: [alloca] alloca block of short pointer-to-pointer stored into global void** */
void escape_93(void) { short *p = alloca(13); g_voidpp = (void **)&p; }

/* CASE 94: [fixed] fixed long array returned directly */
long *escape_94(void) { long b[16]; return b; }

/* CASE 95: [fixed] fixed float array stored into global pointer */
void escape_95(void) { float b[19]; g_float = b; }

/* CASE 96: [fixed] fixed double array passed to helper function sink */
void escape_96(void) { double b[22]; sink_double(b); }

/* CASE 97: [fixed] fixed unsigned char array stored into function-static pointer */
void escape_97(void) { unsigned char b[25]; static unsigned char *sp; sp = b; (void)sp; }

/* CASE 98: [fixed] nested-scope fixed unsigned int array returned */
unsigned int *escape_98(void) { unsigned int b[28]; { unsigned int c[28]; return c; } }

/* CASE 99: [fixed] fixed struct S array address-of-local-pointer returned */
struct S **escape_99(void) { struct S b[4]; struct S *p = b; return &p; }

/* CASE 100: [fixed] address of local char scalar returned */
char *escape_100(void) { char x = (char)0; return &x; }

/* CASE 101: [fixed] address of local int scalar stored into global pointer */
void escape_101(void) { int x = (int)0; g_int = &x; }

/* CASE 102: [compound] address of short compound literal returned */
short *escape_102(void) { return &(short){0}; }

/* CASE 103: [compound] long compound-literal array stored into global pointer */
void escape_103(void) { g_long = (long[16]){0}; }

/* CASE 104: [compound] address of float compound literal passed to helper sink */
void escape_104(void) { sink_float(&(float){0}); }

/* CASE 105: [VLA] VLA of double returned directly */
double *escape_105(int sz) { double b[sz]; return b; }

/* CASE 106: [VLA] VLA of unsigned char stored into global pointer */
void escape_106(int sz) { unsigned char b[sz]; g_uchar = b; }

/* CASE 107: [VLA] nested-scope VLA of unsigned int stored into global pointer */
void escape_107(int sz) { unsigned int b[sz]; { unsigned int c[sz]; g_uint = c; } (void)b; }

/* CASE 108: [VLA] VLA of struct S returned through a local alias pointer */
struct S *escape_108(int sz) { struct S b[sz]; struct S *p = b; return p; }

/* CASE 109: [VLA] VLA of char passed to helper function sink */
void escape_109(int sz) { char b[sz]; sink_char(b); }

/* CASE 110: [VLA] VLA of int stored into function-static pointer */
void escape_110(int sz) { int b[sz]; static int *sp; sp = b; (void)sp; }

/* CASE 111: [VLA] VLA of short address-of-local-pointer returned (pointer-to-pointer) */
short **escape_111(int sz) { short b[sz]; short *p = b; return &p; }

/* CASE 112: [VLA] VLA of long address-of-local-pointer stored into global void** */
void escape_112(int sz) { long b[sz]; long *p = b; g_voidpp = (void **)&p; }

/* CASE 113: [alloca] alloca block of float returned directly */
float *escape_113(void) { float *p = alloca(19); return p; }

/* CASE 114: [alloca] alloca block of double stored into global pointer */
void escape_114(void) { double *p = alloca(22); g_double = p; }

/* CASE 115: [alloca] alloca block of unsigned char passed to helper function sink */
void escape_115(void) { unsigned char *p = alloca(25); sink_uchar(p); }

/* CASE 116: [alloca] alloca block of unsigned int stored into function-static pointer */
void escape_116(void) { unsigned int *p = alloca(28); static unsigned int *sp; sp = p; (void)sp; }

/* CASE 117: [alloca] alloca block of struct S aliased in nested scope then stored globally */
void escape_117(void) { struct S *p = alloca(4); { struct S *q = p; g_struct = q; } }

/* CASE 118: [alloca] alloca block of char returned through pointer-to-pointer */
char **escape_118(void) { char *p = alloca(7); return &p; }

/* CASE 119: [alloca] alloca block of int pointer-to-pointer stored into global void** */
void escape_119(void) { int *p = alloca(10); g_voidpp = (void **)&p; }

/* CASE 120: [fixed] fixed short array returned directly */
short *escape_120(void) { short b[13]; return b; }

/* CASE 121: [fixed] fixed long array stored into global pointer */
void escape_121(void) { long b[16]; g_long = b; }

/* CASE 122: [fixed] fixed float array passed to helper function sink */
void escape_122(void) { float b[19]; sink_float(b); }

/* CASE 123: [fixed] fixed double array stored into function-static pointer */
void escape_123(void) { double b[22]; static double *sp; sp = b; (void)sp; }

/* CASE 124: [fixed] nested-scope fixed unsigned char array returned */
unsigned char *escape_124(void) { unsigned char b[25]; { unsigned char c[25]; return c; } }

/* CASE 125: [fixed] fixed unsigned int array address-of-local-pointer returned */
unsigned int **escape_125(void) { unsigned int b[28]; unsigned int *p = b; return &p; }

/* CASE 126: [fixed] address of local struct S scalar returned */
struct S *escape_126(void) { struct S x = {0}; return &x; }

/* CASE 127: [fixed] address of local char scalar stored into global pointer */
void escape_127(void) { char x = (char)0; g_char = &x; }

/* CASE 128: [compound] address of int compound literal returned */
int *escape_128(void) { return &(int){0}; }

/* CASE 129: [compound] short compound-literal array stored into global pointer */
void escape_129(void) { g_short = (short[13]){0}; }

/* CASE 130: [compound] address of long compound literal passed to helper sink */
void escape_130(void) { sink_long(&(long){0}); }

/* CASE 131: [VLA] VLA of float returned directly */
float *escape_131(int sz) { float b[sz]; return b; }

/* CASE 132: [VLA] VLA of double stored into global pointer */
void escape_132(int sz) { double b[sz]; g_double = b; }

/* CASE 133: [VLA] nested-scope VLA of unsigned char stored into global pointer */
void escape_133(int sz) { unsigned char b[sz]; { unsigned char c[sz]; g_uchar = c; } (void)b; }

/* CASE 134: [VLA] VLA of unsigned int returned through a local alias pointer */
unsigned int *escape_134(int sz) { unsigned int b[sz]; unsigned int *p = b; return p; }

/* CASE 135: [VLA] VLA of struct S passed to helper function sink */
void escape_135(int sz) { struct S b[sz]; sink_struct(b); }

/* CASE 136: [VLA] VLA of char stored into function-static pointer */
void escape_136(int sz) { char b[sz]; static char *sp; sp = b; (void)sp; }

/* CASE 137: [VLA] VLA of int address-of-local-pointer returned (pointer-to-pointer) */
int **escape_137(int sz) { int b[sz]; int *p = b; return &p; }

/* CASE 138: [VLA] VLA of short address-of-local-pointer stored into global void** */
void escape_138(int sz) { short b[sz]; short *p = b; g_voidpp = (void **)&p; }

/* CASE 139: [alloca] alloca block of long returned directly */
long *escape_139(void) { long *p = alloca(16); return p; }

/* CASE 140: [alloca] alloca block of float stored into global pointer */
void escape_140(void) { float *p = alloca(19); g_float = p; }

/* CASE 141: [alloca] alloca block of double passed to helper function sink */
void escape_141(void) { double *p = alloca(22); sink_double(p); }

/* CASE 142: [alloca] alloca block of unsigned char stored into function-static pointer */
void escape_142(void) { unsigned char *p = alloca(25); static unsigned char *sp; sp = p; (void)sp; }

/* CASE 143: [alloca] alloca block of unsigned int aliased in nested scope then stored globally */
void escape_143(void) { unsigned int *p = alloca(28); { unsigned int *q = p; g_uint = q; } }

/* CASE 144: [alloca] alloca block of struct S returned through pointer-to-pointer */
struct S **escape_144(void) { struct S *p = alloca(4); return &p; }

/* CASE 145: [alloca] alloca block of char pointer-to-pointer stored into global void** */
void escape_145(void) { char *p = alloca(7); g_voidpp = (void **)&p; }

/* CASE 146: [fixed] fixed int array returned directly */
int *escape_146(void) { int b[10]; return b; }

/* CASE 147: [fixed] fixed short array stored into global pointer */
void escape_147(void) { short b[13]; g_short = b; }

/* CASE 148: [fixed] fixed long array passed to helper function sink */
void escape_148(void) { long b[16]; sink_long(b); }

/* CASE 149: [fixed] fixed float array stored into function-static pointer */
void escape_149(void) { float b[19]; static float *sp; sp = b; (void)sp; }

/* CASE 150: [fixed] nested-scope fixed double array returned */
double *escape_150(void) { double b[22]; { double c[22]; return c; } }

/* CASE 151: [fixed] fixed unsigned char array address-of-local-pointer returned */
unsigned char **escape_151(void) { unsigned char b[25]; unsigned char *p = b; return &p; }

/* CASE 152: [fixed] address of local unsigned int scalar returned */
unsigned int *escape_152(void) { unsigned int x = (unsigned int)0; return &x; }

/* CASE 153: [fixed] address of local struct S scalar stored into global pointer */
void escape_153(void) { struct S x = {0}; g_struct = &x; }

/* CASE 154: [compound] address of char compound literal returned */
char *escape_154(void) { return &(char){0}; }

/* CASE 155: [compound] int compound-literal array stored into global pointer */
void escape_155(void) { g_int = (int[10]){0}; }

/* CASE 156: [compound] address of short compound literal passed to helper sink */
void escape_156(void) { sink_short(&(short){0}); }

/* CASE 157: [VLA] VLA of long returned directly */
long *escape_157(int sz) { long b[sz]; return b; }

/* CASE 158: [VLA] VLA of float stored into global pointer */
void escape_158(int sz) { float b[sz]; g_float = b; }

/* CASE 159: [VLA] nested-scope VLA of double stored into global pointer */
void escape_159(int sz) { double b[sz]; { double c[sz]; g_double = c; } (void)b; }

/* CASE 160: [VLA] VLA of unsigned char returned through a local alias pointer */
unsigned char *escape_160(int sz) { unsigned char b[sz]; unsigned char *p = b; return p; }

/* CASE 161: [VLA] VLA of unsigned int passed to helper function sink */
void escape_161(int sz) { unsigned int b[sz]; sink_uint(b); }

/* CASE 162: [VLA] VLA of struct S stored into function-static pointer */
void escape_162(int sz) { struct S b[sz]; static struct S *sp; sp = b; (void)sp; }

/* CASE 163: [VLA] VLA of char address-of-local-pointer returned (pointer-to-pointer) */
char **escape_163(int sz) { char b[sz]; char *p = b; return &p; }

/* CASE 164: [VLA] VLA of int address-of-local-pointer stored into global void** */
void escape_164(int sz) { int b[sz]; int *p = b; g_voidpp = (void **)&p; }

/* CASE 165: [alloca] alloca block of short returned directly */
short *escape_165(void) { short *p = alloca(13); return p; }

/* CASE 166: [alloca] alloca block of long stored into global pointer */
void escape_166(void) { long *p = alloca(16); g_long = p; }

/* CASE 167: [alloca] alloca block of float passed to helper function sink */
void escape_167(void) { float *p = alloca(19); sink_float(p); }

/* CASE 168: [alloca] alloca block of double stored into function-static pointer */
void escape_168(void) { double *p = alloca(22); static double *sp; sp = p; (void)sp; }

/* CASE 169: [alloca] alloca block of unsigned char aliased in nested scope then stored globally */
void escape_169(void) { unsigned char *p = alloca(25); { unsigned char *q = p; g_uchar = q; } }

/* CASE 170: [alloca] alloca block of unsigned int returned through pointer-to-pointer */
unsigned int **escape_170(void) { unsigned int *p = alloca(28); return &p; }

/* CASE 171: [alloca] alloca block of struct S pointer-to-pointer stored into global void** */
void escape_171(void) { struct S *p = alloca(4); g_voidpp = (void **)&p; }

/* CASE 172: [fixed] fixed char array returned directly */
char *escape_172(void) { char b[7]; return b; }

/* CASE 173: [fixed] fixed int array stored into global pointer */
void escape_173(void) { int b[10]; g_int = b; }

/* CASE 174: [fixed] fixed short array passed to helper function sink */
void escape_174(void) { short b[13]; sink_short(b); }

/* CASE 175: [fixed] fixed long array stored into function-static pointer */
void escape_175(void) { long b[16]; static long *sp; sp = b; (void)sp; }

/* CASE 176: [fixed] nested-scope fixed float array returned */
float *escape_176(void) { float b[19]; { float c[19]; return c; } }

/* CASE 177: [fixed] fixed double array address-of-local-pointer returned */
double **escape_177(void) { double b[22]; double *p = b; return &p; }

/* CASE 178: [fixed] address of local unsigned char scalar returned */
unsigned char *escape_178(void) { unsigned char x = (unsigned char)0; return &x; }

/* CASE 179: [fixed] address of local unsigned int scalar stored into global pointer */
void escape_179(void) { unsigned int x = (unsigned int)0; g_uint = &x; }

/* CASE 180: [compound] address of struct S compound literal returned */
struct S *escape_180(void) { return &(struct S){0}; }

/* CASE 181: [compound] char compound-literal array stored into global pointer */
void escape_181(void) { g_char = (char[7]){0}; }

/* CASE 182: [compound] address of int compound literal passed to helper sink */
void escape_182(void) { sink_int(&(int){0}); }

/* CASE 183: [VLA] VLA of short returned directly */
short *escape_183(int sz) { short b[sz]; return b; }

/* CASE 184: [VLA] VLA of long stored into global pointer */
void escape_184(int sz) { long b[sz]; g_long = b; }

/* CASE 185: [VLA] nested-scope VLA of float stored into global pointer */
void escape_185(int sz) { float b[sz]; { float c[sz]; g_float = c; } (void)b; }

/* CASE 186: [VLA] VLA of double returned through a local alias pointer */
double *escape_186(int sz) { double b[sz]; double *p = b; return p; }

/* CASE 187: [VLA] VLA of unsigned char passed to helper function sink */
void escape_187(int sz) { unsigned char b[sz]; sink_uchar(b); }

/* CASE 188: [VLA] VLA of unsigned int stored into function-static pointer */
void escape_188(int sz) { unsigned int b[sz]; static unsigned int *sp; sp = b; (void)sp; }

/* CASE 189: [VLA] VLA of struct S address-of-local-pointer returned (pointer-to-pointer) */
struct S **escape_189(int sz) { struct S b[sz]; struct S *p = b; return &p; }

/* CASE 190: [VLA] VLA of char address-of-local-pointer stored into global void** */
void escape_190(int sz) { char b[sz]; char *p = b; g_voidpp = (void **)&p; }

/* CASE 191: [alloca] alloca block of int returned directly */
int *escape_191(void) { int *p = alloca(10); return p; }

/* CASE 192: [alloca] alloca block of short stored into global pointer */
void escape_192(void) { short *p = alloca(13); g_short = p; }

/* CASE 193: [alloca] alloca block of long passed to helper function sink */
void escape_193(void) { long *p = alloca(16); sink_long(p); }

/* CASE 194: [alloca] alloca block of float stored into function-static pointer */
void escape_194(void) { float *p = alloca(19); static float *sp; sp = p; (void)sp; }

/* CASE 195: [alloca] alloca block of double aliased in nested scope then stored globally */
void escape_195(void) { double *p = alloca(22); { double *q = p; g_double = q; } }

/* CASE 196: [alloca] alloca block of unsigned char returned through pointer-to-pointer */
unsigned char **escape_196(void) { unsigned char *p = alloca(25); return &p; }

/* CASE 197: [alloca] alloca block of unsigned int pointer-to-pointer stored into global void** */
void escape_197(void) { unsigned int *p = alloca(28); g_voidpp = (void **)&p; }

/* CASE 198: [fixed] fixed struct S array returned directly */
struct S *escape_198(void) { struct S b[4]; return b; }

/* CASE 199: [fixed] fixed char array stored into global pointer */
void escape_199(void) { char b[7]; g_char = b; }

/* CASE 200: [fixed] fixed int array passed to helper function sink */
void escape_200(void) { int b[10]; sink_int(b); }

/* CASE 201: [fixed] fixed short array stored into function-static pointer */
void escape_201(void) { short b[13]; static short *sp; sp = b; (void)sp; }

/* CASE 202: [fixed] nested-scope fixed long array returned */
long *escape_202(void) { long b[16]; { long c[16]; return c; } }

/* CASE 203: [fixed] fixed float array address-of-local-pointer returned */
float **escape_203(void) { float b[19]; float *p = b; return &p; }

/* CASE 204: [fixed] address of local double scalar returned */
double *escape_204(void) { double x = (double)0; return &x; }

/* CASE 205: [fixed] address of local unsigned char scalar stored into global pointer */
void escape_205(void) { unsigned char x = (unsigned char)0; g_uchar = &x; }

/* CASE 206: [compound] address of unsigned int compound literal returned */
unsigned int *escape_206(void) { return &(unsigned int){0}; }

/* CASE 207: [compound] struct S compound-literal array stored into global pointer */
void escape_207(void) { g_struct = (struct S[4]){0}; }

/* CASE 208: [compound] address of char compound literal passed to helper sink */
void escape_208(void) { sink_char(&(char){0}); }

/* CASE 209: [VLA] VLA of int returned directly */
int *escape_209(int sz) { int b[sz]; return b; }

/* CASE 210: [VLA] VLA of short stored into global pointer */
void escape_210(int sz) { short b[sz]; g_short = b; }

/* CASE 211: [VLA] nested-scope VLA of long stored into global pointer */
void escape_211(int sz) { long b[sz]; { long c[sz]; g_long = c; } (void)b; }

/* CASE 212: [VLA] VLA of float returned through a local alias pointer */
float *escape_212(int sz) { float b[sz]; float *p = b; return p; }

/* CASE 213: [VLA] VLA of double passed to helper function sink */
void escape_213(int sz) { double b[sz]; sink_double(b); }

/* CASE 214: [VLA] VLA of unsigned char stored into function-static pointer */
void escape_214(int sz) { unsigned char b[sz]; static unsigned char *sp; sp = b; (void)sp; }

/* CASE 215: [VLA] VLA of unsigned int address-of-local-pointer returned (pointer-to-pointer) */
unsigned int **escape_215(int sz) { unsigned int b[sz]; unsigned int *p = b; return &p; }

/* CASE 216: [VLA] VLA of struct S address-of-local-pointer stored into global void** */
void escape_216(int sz) { struct S b[sz]; struct S *p = b; g_voidpp = (void **)&p; }

/* CASE 217: [alloca] alloca block of char returned directly */
char *escape_217(void) { char *p = alloca(7); return p; }

/* CASE 218: [alloca] alloca block of int stored into global pointer */
void escape_218(void) { int *p = alloca(10); g_int = p; }

/* CASE 219: [alloca] alloca block of short passed to helper function sink */
void escape_219(void) { short *p = alloca(13); sink_short(p); }

/* CASE 220: [alloca] alloca block of long stored into function-static pointer */
void escape_220(void) { long *p = alloca(16); static long *sp; sp = p; (void)sp; }

/* CASE 221: [alloca] alloca block of float aliased in nested scope then stored globally */
void escape_221(void) { float *p = alloca(19); { float *q = p; g_float = q; } }

/* CASE 222: [alloca] alloca block of double returned through pointer-to-pointer */
double **escape_222(void) { double *p = alloca(22); return &p; }

/* CASE 223: [alloca] alloca block of unsigned char pointer-to-pointer stored into global void** */
void escape_223(void) { unsigned char *p = alloca(25); g_voidpp = (void **)&p; }

/* CASE 224: [fixed] fixed unsigned int array returned directly */
unsigned int *escape_224(void) { unsigned int b[28]; return b; }

/* CASE 225: [fixed] fixed struct S array stored into global pointer */
void escape_225(void) { struct S b[4]; g_struct = b; }

/* CASE 226: [fixed] fixed char array passed to helper function sink */
void escape_226(void) { char b[7]; sink_char(b); }

/* CASE 227: [fixed] fixed int array stored into function-static pointer */
void escape_227(void) { int b[10]; static int *sp; sp = b; (void)sp; }

/* CASE 228: [fixed] nested-scope fixed short array returned */
short *escape_228(void) { short b[13]; { short c[13]; return c; } }

/* CASE 229: [fixed] fixed long array address-of-local-pointer returned */
long **escape_229(void) { long b[16]; long *p = b; return &p; }

/* CASE 230: [fixed] address of local float scalar returned */
float *escape_230(void) { float x = (float)0; return &x; }

/* CASE 231: [fixed] address of local double scalar stored into global pointer */
void escape_231(void) { double x = (double)0; g_double = &x; }

/* CASE 232: [compound] address of unsigned char compound literal returned */
unsigned char *escape_232(void) { return &(unsigned char){0}; }

/* CASE 233: [compound] unsigned int compound-literal array stored into global pointer */
void escape_233(void) { g_uint = (unsigned int[28]){0}; }

/* CASE 234: [compound] address of struct S compound literal passed to helper sink */
void escape_234(void) { sink_struct(&(struct S){0}); }

/* CASE 235: [VLA] VLA of char returned directly */
char *escape_235(int sz) { char b[sz]; return b; }

/* CASE 236: [VLA] VLA of int stored into global pointer */
void escape_236(int sz) { int b[sz]; g_int = b; }

/* CASE 237: [VLA] nested-scope VLA of short stored into global pointer */
void escape_237(int sz) { short b[sz]; { short c[sz]; g_short = c; } (void)b; }

/* CASE 238: [VLA] VLA of long returned through a local alias pointer */
long *escape_238(int sz) { long b[sz]; long *p = b; return p; }

/* CASE 239: [VLA] VLA of float passed to helper function sink */
void escape_239(int sz) { float b[sz]; sink_float(b); }

/* CASE 240: [VLA] VLA of double stored into function-static pointer */
void escape_240(int sz) { double b[sz]; static double *sp; sp = b; (void)sp; }

/* CASE 241: [VLA] VLA of unsigned char address-of-local-pointer returned (pointer-to-pointer) */
unsigned char **escape_241(int sz) { unsigned char b[sz]; unsigned char *p = b; return &p; }

/* CASE 242: [VLA] VLA of unsigned int address-of-local-pointer stored into global void** */
void escape_242(int sz) { unsigned int b[sz]; unsigned int *p = b; g_voidpp = (void **)&p; }

/* CASE 243: [alloca] alloca block of struct S returned directly */
struct S *escape_243(void) { struct S *p = alloca(4); return p; }

/* CASE 244: [alloca] alloca block of char stored into global pointer */
void escape_244(void) { char *p = alloca(7); g_char = p; }

/* CASE 245: [alloca] alloca block of int passed to helper function sink */
void escape_245(void) { int *p = alloca(10); sink_int(p); }

/* CASE 246: [alloca] alloca block of short stored into function-static pointer */
void escape_246(void) { short *p = alloca(13); static short *sp; sp = p; (void)sp; }

/* CASE 247: [alloca] alloca block of long aliased in nested scope then stored globally */
void escape_247(void) { long *p = alloca(16); { long *q = p; g_long = q; } }

/* CASE 248: [alloca] alloca block of float returned through pointer-to-pointer */
float **escape_248(void) { float *p = alloca(19); return &p; }

/* CASE 249: [alloca] alloca block of double pointer-to-pointer stored into global void** */
void escape_249(void) { double *p = alloca(22); g_voidpp = (void **)&p; }

/* CASE 250: [fixed] fixed unsigned char array returned directly */
unsigned char *escape_250(void) { unsigned char b[25]; return b; }

/* CASE 251: [fixed] fixed unsigned int array stored into global pointer */
void escape_251(void) { unsigned int b[28]; g_uint = b; }

/* CASE 252: [fixed] fixed struct S array passed to helper function sink */
void escape_252(void) { struct S b[4]; sink_struct(b); }

/* CASE 253: [fixed] fixed char array stored into function-static pointer */
void escape_253(void) { char b[7]; static char *sp; sp = b; (void)sp; }

/* CASE 254: [fixed] nested-scope fixed int array returned */
int *escape_254(void) { int b[10]; { int c[10]; return c; } }

/* CASE 255: [fixed] fixed short array address-of-local-pointer returned */
short **escape_255(void) { short b[13]; short *p = b; return &p; }

/* CASE 256: [fixed] address of local long scalar returned */
long *escape_256(void) { long x = (long)0; return &x; }

/* CASE 257: [fixed] address of local float scalar stored into global pointer */
void escape_257(void) { float x = (float)0; g_float = &x; }

/* CASE 258: [compound] address of double compound literal returned */
double *escape_258(void) { return &(double){0}; }

/* CASE 259: [compound] unsigned char compound-literal array stored into global pointer */
void escape_259(void) { g_uchar = (unsigned char[25]){0}; }

/* CASE 260: [compound] address of unsigned int compound literal passed to helper sink */
void escape_260(void) { sink_uint(&(unsigned int){0}); }

/* CASE 261: [VLA] VLA of struct S returned directly */
struct S *escape_261(int sz) { struct S b[sz]; return b; }

/* CASE 262: [VLA] VLA of char stored into global pointer */
void escape_262(int sz) { char b[sz]; g_char = b; }

/* CASE 263: [VLA] nested-scope VLA of int stored into global pointer */
void escape_263(int sz) { int b[sz]; { int c[sz]; g_int = c; } (void)b; }

/* CASE 264: [VLA] VLA of short returned through a local alias pointer */
short *escape_264(int sz) { short b[sz]; short *p = b; return p; }

/* CASE 265: [VLA] VLA of long passed to helper function sink */
void escape_265(int sz) { long b[sz]; sink_long(b); }

/* CASE 266: [VLA] VLA of float stored into function-static pointer */
void escape_266(int sz) { float b[sz]; static float *sp; sp = b; (void)sp; }

/* CASE 267: [VLA] VLA of double address-of-local-pointer returned (pointer-to-pointer) */
double **escape_267(int sz) { double b[sz]; double *p = b; return &p; }

/* CASE 268: [VLA] VLA of unsigned char address-of-local-pointer stored into global void** */
void escape_268(int sz) { unsigned char b[sz]; unsigned char *p = b; g_voidpp = (void **)&p; }

/* CASE 269: [alloca] alloca block of unsigned int returned directly */
unsigned int *escape_269(void) { unsigned int *p = alloca(28); return p; }

/* CASE 270: [alloca] alloca block of struct S stored into global pointer */
void escape_270(void) { struct S *p = alloca(4); g_struct = p; }

/* CASE 271: [alloca] alloca block of char passed to helper function sink */
void escape_271(void) { char *p = alloca(7); sink_char(p); }

/* CASE 272: [alloca] alloca block of int stored into function-static pointer */
void escape_272(void) { int *p = alloca(10); static int *sp; sp = p; (void)sp; }

/* CASE 273: [alloca] alloca block of short aliased in nested scope then stored globally */
void escape_273(void) { short *p = alloca(13); { short *q = p; g_short = q; } }

/* CASE 274: [alloca] alloca block of long returned through pointer-to-pointer */
long **escape_274(void) { long *p = alloca(16); return &p; }

/* CASE 275: [alloca] alloca block of float pointer-to-pointer stored into global void** */
void escape_275(void) { float *p = alloca(19); g_voidpp = (void **)&p; }

/* CASE 276: [fixed] fixed double array returned directly */
double *escape_276(void) { double b[22]; return b; }

/* CASE 277: [fixed] fixed unsigned char array stored into global pointer */
void escape_277(void) { unsigned char b[25]; g_uchar = b; }

/* CASE 278: [fixed] fixed unsigned int array passed to helper function sink */
void escape_278(void) { unsigned int b[28]; sink_uint(b); }

/* CASE 279: [fixed] fixed struct S array stored into function-static pointer */
void escape_279(void) { struct S b[4]; static struct S *sp; sp = b; (void)sp; }

/* CASE 280: [fixed] nested-scope fixed char array returned */
char *escape_280(void) { char b[7]; { char c[7]; return c; } }

/* CASE 281: [fixed] fixed int array address-of-local-pointer returned */
int **escape_281(void) { int b[10]; int *p = b; return &p; }

/* CASE 282: [fixed] address of local short scalar returned */
short *escape_282(void) { short x = (short)0; return &x; }

/* CASE 283: [fixed] address of local long scalar stored into global pointer */
void escape_283(void) { long x = (long)0; g_long = &x; }

/* CASE 284: [compound] address of float compound literal returned */
float *escape_284(void) { return &(float){0}; }

/* CASE 285: [compound] double compound-literal array stored into global pointer */
void escape_285(void) { g_double = (double[22]){0}; }

/* CASE 286: [compound] address of unsigned char compound literal passed to helper sink */
void escape_286(void) { sink_uchar(&(unsigned char){0}); }

/* CASE 287: [VLA] VLA of unsigned int returned directly */
unsigned int *escape_287(int sz) { unsigned int b[sz]; return b; }

/* CASE 288: [VLA] VLA of struct S stored into global pointer */
void escape_288(int sz) { struct S b[sz]; g_struct = b; }

/* CASE 289: [VLA] nested-scope VLA of char stored into global pointer */
void escape_289(int sz) { char b[sz]; { char c[sz]; g_char = c; } (void)b; }

/* CASE 290: [VLA] VLA of int returned through a local alias pointer */
int *escape_290(int sz) { int b[sz]; int *p = b; return p; }

/* CASE 291: [VLA] VLA of short passed to helper function sink */
void escape_291(int sz) { short b[sz]; sink_short(b); }

/* CASE 292: [VLA] VLA of long stored into function-static pointer */
void escape_292(int sz) { long b[sz]; static long *sp; sp = b; (void)sp; }

/* CASE 293: [VLA] VLA of float address-of-local-pointer returned (pointer-to-pointer) */
float **escape_293(int sz) { float b[sz]; float *p = b; return &p; }

/* CASE 294: [VLA] VLA of double address-of-local-pointer stored into global void** */
void escape_294(int sz) { double b[sz]; double *p = b; g_voidpp = (void **)&p; }

/* CASE 295: [alloca] alloca block of unsigned char returned directly */
unsigned char *escape_295(void) { unsigned char *p = alloca(25); return p; }

/* CASE 296: [alloca] alloca block of unsigned int stored into global pointer */
void escape_296(void) { unsigned int *p = alloca(28); g_uint = p; }

/* CASE 297: [alloca] alloca block of struct S passed to helper function sink */
void escape_297(void) { struct S *p = alloca(4); sink_struct(p); }

/* CASE 298: [alloca] alloca block of char stored into function-static pointer */
void escape_298(void) { char *p = alloca(7); static char *sp; sp = p; (void)sp; }

/* CASE 299: [alloca] alloca block of int aliased in nested scope then stored globally */
void escape_299(void) { int *p = alloca(10); { int *q = p; g_int = q; } }

/* CASE 300: [alloca] alloca block of short returned through pointer-to-pointer */
short **escape_300(void) { short *p = alloca(13); return &p; }

/* CASE 301: [alloca] alloca block of long pointer-to-pointer stored into global void** */
void escape_301(void) { long *p = alloca(16); g_voidpp = (void **)&p; }

/* CASE 302: [fixed] fixed float array returned directly */
float *escape_302(void) { float b[19]; return b; }

/* CASE 303: [fixed] fixed double array stored into global pointer */
void escape_303(void) { double b[22]; g_double = b; }

/* CASE 304: [fixed] fixed unsigned char array passed to helper function sink */
void escape_304(void) { unsigned char b[25]; sink_uchar(b); }

/* CASE 305: [fixed] fixed unsigned int array stored into function-static pointer */
void escape_305(void) { unsigned int b[28]; static unsigned int *sp; sp = b; (void)sp; }

/* CASE 306: [fixed] nested-scope fixed struct S array returned */
struct S *escape_306(void) { struct S b[4]; { struct S c[4]; return c; } }

/* CASE 307: [fixed] fixed char array address-of-local-pointer returned */
char **escape_307(void) { char b[7]; char *p = b; return &p; }

/* CASE 308: [fixed] address of local int scalar returned */
int *escape_308(void) { int x = (int)0; return &x; }

/* CASE 309: [fixed] address of local short scalar stored into global pointer */
void escape_309(void) { short x = (short)0; g_short = &x; }

/* CASE 310: [compound] address of long compound literal returned */
long *escape_310(void) { return &(long){0}; }

/* CASE 311: [compound] float compound-literal array stored into global pointer */
void escape_311(void) { g_float = (float[19]){0}; }

/* CASE 312: [compound] address of double compound literal passed to helper sink */
void escape_312(void) { sink_double(&(double){0}); }

/* CASE 313: [VLA] VLA of unsigned char returned directly */
unsigned char *escape_313(int sz) { unsigned char b[sz]; return b; }

/* CASE 314: [VLA] VLA of unsigned int stored into global pointer */
void escape_314(int sz) { unsigned int b[sz]; g_uint = b; }

/* CASE 315: [VLA] nested-scope VLA of struct S stored into global pointer */
void escape_315(int sz) { struct S b[sz]; { struct S c[sz]; g_struct = c; } (void)b; }

/* CASE 316: [VLA] VLA of char returned through a local alias pointer */
char *escape_316(int sz) { char b[sz]; char *p = b; return p; }

/* CASE 317: [VLA] VLA of int passed to helper function sink */
void escape_317(int sz) { int b[sz]; sink_int(b); }

/* CASE 318: [VLA] VLA of short stored into function-static pointer */
void escape_318(int sz) { short b[sz]; static short *sp; sp = b; (void)sp; }

/* CASE 319: [VLA] VLA of long address-of-local-pointer returned (pointer-to-pointer) */
long **escape_319(int sz) { long b[sz]; long *p = b; return &p; }

/* CASE 320: [VLA] VLA of float address-of-local-pointer stored into global void** */
void escape_320(int sz) { float b[sz]; float *p = b; g_voidpp = (void **)&p; }

/* CASE 321: [alloca] alloca block of double returned directly */
double *escape_321(void) { double *p = alloca(22); return p; }

/* CASE 322: [alloca] alloca block of unsigned char stored into global pointer */
void escape_322(void) { unsigned char *p = alloca(25); g_uchar = p; }

/* CASE 323: [alloca] alloca block of unsigned int passed to helper function sink */
void escape_323(void) { unsigned int *p = alloca(28); sink_uint(p); }

/* CASE 324: [alloca] alloca block of struct S stored into function-static pointer */
void escape_324(void) { struct S *p = alloca(4); static struct S *sp; sp = p; (void)sp; }

/* CASE 325: [alloca] alloca block of char aliased in nested scope then stored globally */
void escape_325(void) { char *p = alloca(7); { char *q = p; g_char = q; } }

/* CASE 326: [alloca] alloca block of int returned through pointer-to-pointer */
int **escape_326(void) { int *p = alloca(10); return &p; }

/* CASE 327: [alloca] alloca block of short pointer-to-pointer stored into global void** */
void escape_327(void) { short *p = alloca(13); g_voidpp = (void **)&p; }

/* CASE 328: [fixed] fixed long array returned directly */
long *escape_328(void) { long b[16]; return b; }

/* CASE 329: [fixed] fixed float array stored into global pointer */
void escape_329(void) { float b[19]; g_float = b; }

/* CASE 330: [fixed] fixed double array passed to helper function sink */
void escape_330(void) { double b[22]; sink_double(b); }

/* CASE 331: [fixed] fixed unsigned char array stored into function-static pointer */
void escape_331(void) { unsigned char b[25]; static unsigned char *sp; sp = b; (void)sp; }

/* CASE 332: [fixed] nested-scope fixed unsigned int array returned */
unsigned int *escape_332(void) { unsigned int b[28]; { unsigned int c[28]; return c; } }

/* CASE 333: [fixed] fixed struct S array address-of-local-pointer returned */
struct S **escape_333(void) { struct S b[4]; struct S *p = b; return &p; }

/* CASE 334: [fixed] address of local char scalar returned */
char *escape_334(void) { char x = (char)0; return &x; }

/* CASE 335: [fixed] address of local int scalar stored into global pointer */
void escape_335(void) { int x = (int)0; g_int = &x; }

/* CASE 336: [compound] address of short compound literal returned */
short *escape_336(void) { return &(short){0}; }

/* CASE 337: [compound] long compound-literal array stored into global pointer */
void escape_337(void) { g_long = (long[16]){0}; }

/* CASE 338: [compound] address of float compound literal passed to helper sink */
void escape_338(void) { sink_float(&(float){0}); }

/* CASE 339: [VLA] VLA of double returned directly */
double *escape_339(int sz) { double b[sz]; return b; }

/* CASE 340: [VLA] VLA of unsigned char stored into global pointer */
void escape_340(int sz) { unsigned char b[sz]; g_uchar = b; }

/* CASE 341: [VLA] nested-scope VLA of unsigned int stored into global pointer */
void escape_341(int sz) { unsigned int b[sz]; { unsigned int c[sz]; g_uint = c; } (void)b; }

/* CASE 342: [VLA] VLA of struct S returned through a local alias pointer */
struct S *escape_342(int sz) { struct S b[sz]; struct S *p = b; return p; }

/* CASE 343: [VLA] VLA of char passed to helper function sink */
void escape_343(int sz) { char b[sz]; sink_char(b); }

/* CASE 344: [VLA] VLA of int stored into function-static pointer */
void escape_344(int sz) { int b[sz]; static int *sp; sp = b; (void)sp; }

/* CASE 345: [VLA] VLA of short address-of-local-pointer returned (pointer-to-pointer) */
short **escape_345(int sz) { short b[sz]; short *p = b; return &p; }

/* CASE 346: [VLA] VLA of long address-of-local-pointer stored into global void** */
void escape_346(int sz) { long b[sz]; long *p = b; g_voidpp = (void **)&p; }

/* CASE 347: [alloca] alloca block of float returned directly */
float *escape_347(void) { float *p = alloca(19); return p; }

/* CASE 348: [alloca] alloca block of double stored into global pointer */
void escape_348(void) { double *p = alloca(22); g_double = p; }

/* CASE 349: [alloca] alloca block of unsigned char passed to helper function sink */
void escape_349(void) { unsigned char *p = alloca(25); sink_uchar(p); }

/* CASE 350: [alloca] alloca block of unsigned int stored into function-static pointer */
void escape_350(void) { unsigned int *p = alloca(28); static unsigned int *sp; sp = p; (void)sp; }

/* CASE 351: [alloca] alloca block of struct S aliased in nested scope then stored globally */
void escape_351(void) { struct S *p = alloca(4); { struct S *q = p; g_struct = q; } }

/* CASE 352: [alloca] alloca block of char returned through pointer-to-pointer */
char **escape_352(void) { char *p = alloca(7); return &p; }

/* CASE 353: [alloca] alloca block of int pointer-to-pointer stored into global void** */
void escape_353(void) { int *p = alloca(10); g_voidpp = (void **)&p; }

/* CASE 354: [fixed] fixed short array returned directly */
short *escape_354(void) { short b[13]; return b; }

/* CASE 355: [fixed] fixed long array stored into global pointer */
void escape_355(void) { long b[16]; g_long = b; }

/* CASE 356: [fixed] fixed float array passed to helper function sink */
void escape_356(void) { float b[19]; sink_float(b); }

/* CASE 357: [fixed] fixed double array stored into function-static pointer */
void escape_357(void) { double b[22]; static double *sp; sp = b; (void)sp; }

/* CASE 358: [fixed] nested-scope fixed unsigned char array returned */
unsigned char *escape_358(void) { unsigned char b[25]; { unsigned char c[25]; return c; } }

/* CASE 359: [fixed] fixed unsigned int array address-of-local-pointer returned */
unsigned int **escape_359(void) { unsigned int b[28]; unsigned int *p = b; return &p; }

/* CASE 360: [fixed] address of local struct S scalar returned */
struct S *escape_360(void) { struct S x = {0}; return &x; }

/* CASE 361: [fixed] address of local char scalar stored into global pointer */
void escape_361(void) { char x = (char)0; g_char = &x; }

/* CASE 362: [compound] address of int compound literal returned */
int *escape_362(void) { return &(int){0}; }

/* CASE 363: [compound] short compound-literal array stored into global pointer */
void escape_363(void) { g_short = (short[13]){0}; }

/* CASE 364: [compound] address of long compound literal passed to helper sink */
void escape_364(void) { sink_long(&(long){0}); }

/* CASE 365: [VLA] VLA of float returned directly */
float *escape_365(int sz) { float b[sz]; return b; }

/* CASE 366: [VLA] VLA of double stored into global pointer */
void escape_366(int sz) { double b[sz]; g_double = b; }

/* CASE 367: [VLA] nested-scope VLA of unsigned char stored into global pointer */
void escape_367(int sz) { unsigned char b[sz]; { unsigned char c[sz]; g_uchar = c; } (void)b; }

/* CASE 368: [VLA] VLA of unsigned int returned through a local alias pointer */
unsigned int *escape_368(int sz) { unsigned int b[sz]; unsigned int *p = b; return p; }

/* CASE 369: [VLA] VLA of struct S passed to helper function sink */
void escape_369(int sz) { struct S b[sz]; sink_struct(b); }

/* CASE 370: [VLA] VLA of char stored into function-static pointer */
void escape_370(int sz) { char b[sz]; static char *sp; sp = b; (void)sp; }

/* CASE 371: [VLA] VLA of int address-of-local-pointer returned (pointer-to-pointer) */
int **escape_371(int sz) { int b[sz]; int *p = b; return &p; }

/* CASE 372: [VLA] VLA of short address-of-local-pointer stored into global void** */
void escape_372(int sz) { short b[sz]; short *p = b; g_voidpp = (void **)&p; }

/* CASE 373: [alloca] alloca block of long returned directly */
long *escape_373(void) { long *p = alloca(16); return p; }

/* CASE 374: [alloca] alloca block of float stored into global pointer */
void escape_374(void) { float *p = alloca(19); g_float = p; }

/* CASE 375: [alloca] alloca block of double passed to helper function sink */
void escape_375(void) { double *p = alloca(22); sink_double(p); }

/* CASE 376: [alloca] alloca block of unsigned char stored into function-static pointer */
void escape_376(void) { unsigned char *p = alloca(25); static unsigned char *sp; sp = p; (void)sp; }

/* CASE 377: [alloca] alloca block of unsigned int aliased in nested scope then stored globally */
void escape_377(void) { unsigned int *p = alloca(28); { unsigned int *q = p; g_uint = q; } }

/* CASE 378: [alloca] alloca block of struct S returned through pointer-to-pointer */
struct S **escape_378(void) { struct S *p = alloca(4); return &p; }

/* CASE 379: [alloca] alloca block of char pointer-to-pointer stored into global void** */
void escape_379(void) { char *p = alloca(7); g_voidpp = (void **)&p; }

/* CASE 380: [fixed] fixed int array returned directly */
int *escape_380(void) { int b[10]; return b; }

/* CASE 381: [fixed] fixed short array stored into global pointer */
void escape_381(void) { short b[13]; g_short = b; }

/* CASE 382: [fixed] fixed long array passed to helper function sink */
void escape_382(void) { long b[16]; sink_long(b); }

/* CASE 383: [fixed] fixed float array stored into function-static pointer */
void escape_383(void) { float b[19]; static float *sp; sp = b; (void)sp; }

/* CASE 384: [fixed] nested-scope fixed double array returned */
double *escape_384(void) { double b[22]; { double c[22]; return c; } }

/* CASE 385: [fixed] fixed unsigned char array address-of-local-pointer returned */
unsigned char **escape_385(void) { unsigned char b[25]; unsigned char *p = b; return &p; }

/* CASE 386: [fixed] address of local unsigned int scalar returned */
unsigned int *escape_386(void) { unsigned int x = (unsigned int)0; return &x; }

/* CASE 387: [fixed] address of local struct S scalar stored into global pointer */
void escape_387(void) { struct S x = {0}; g_struct = &x; }

/* CASE 388: [compound] address of char compound literal returned */
char *escape_388(void) { return &(char){0}; }

/* CASE 389: [compound] int compound-literal array stored into global pointer */
void escape_389(void) { g_int = (int[10]){0}; }

/* CASE 390: [compound] address of short compound literal passed to helper sink */
void escape_390(void) { sink_short(&(short){0}); }

/* CASE 391: [VLA] VLA of long returned directly */
long *escape_391(int sz) { long b[sz]; return b; }

/* CASE 392: [VLA] VLA of float stored into global pointer */
void escape_392(int sz) { float b[sz]; g_float = b; }

/* CASE 393: [VLA] nested-scope VLA of double stored into global pointer */
void escape_393(int sz) { double b[sz]; { double c[sz]; g_double = c; } (void)b; }

/* CASE 394: [VLA] VLA of unsigned char returned through a local alias pointer */
unsigned char *escape_394(int sz) { unsigned char b[sz]; unsigned char *p = b; return p; }

/* CASE 395: [VLA] VLA of unsigned int passed to helper function sink */
void escape_395(int sz) { unsigned int b[sz]; sink_uint(b); }

/* CASE 396: [VLA] VLA of struct S stored into function-static pointer */
void escape_396(int sz) { struct S b[sz]; static struct S *sp; sp = b; (void)sp; }

/* CASE 397: [VLA] VLA of char address-of-local-pointer returned (pointer-to-pointer) */
char **escape_397(int sz) { char b[sz]; char *p = b; return &p; }

/* CASE 398: [VLA] VLA of int address-of-local-pointer stored into global void** */
void escape_398(int sz) { int b[sz]; int *p = b; g_voidpp = (void **)&p; }

/* CASE 399: [alloca] alloca block of short returned directly */
short *escape_399(void) { short *p = alloca(13); return p; }

/* CASE 400: [alloca] alloca block of long stored into global pointer */
void escape_400(void) { long *p = alloca(16); g_long = p; }

/* CASE 401: [alloca] alloca block of float passed to helper function sink */
void escape_401(void) { float *p = alloca(19); sink_float(p); }

/* CASE 402: [alloca] alloca block of double stored into function-static pointer */
void escape_402(void) { double *p = alloca(22); static double *sp; sp = p; (void)sp; }

/* CASE 403: [alloca] alloca block of unsigned char aliased in nested scope then stored globally */
void escape_403(void) { unsigned char *p = alloca(25); { unsigned char *q = p; g_uchar = q; } }

/* CASE 404: [alloca] alloca block of unsigned int returned through pointer-to-pointer */
unsigned int **escape_404(void) { unsigned int *p = alloca(28); return &p; }

/* CASE 405: [alloca] alloca block of struct S pointer-to-pointer stored into global void** */
void escape_405(void) { struct S *p = alloca(4); g_voidpp = (void **)&p; }

/* CASE 406: [fixed] fixed char array returned directly */
char *escape_406(void) { char b[7]; return b; }

/* CASE 407: [fixed] fixed int array stored into global pointer */
void escape_407(void) { int b[10]; g_int = b; }

/* CASE 408: [fixed] fixed short array passed to helper function sink */
void escape_408(void) { short b[13]; sink_short(b); }

/* CASE 409: [fixed] fixed long array stored into function-static pointer */
void escape_409(void) { long b[16]; static long *sp; sp = b; (void)sp; }

/* CASE 410: [fixed] nested-scope fixed float array returned */
float *escape_410(void) { float b[19]; { float c[19]; return c; } }

/* CASE 411: [fixed] fixed double array address-of-local-pointer returned */
double **escape_411(void) { double b[22]; double *p = b; return &p; }

/* CASE 412: [fixed] address of local unsigned char scalar returned */
unsigned char *escape_412(void) { unsigned char x = (unsigned char)0; return &x; }

/* CASE 413: [fixed] address of local unsigned int scalar stored into global pointer */
void escape_413(void) { unsigned int x = (unsigned int)0; g_uint = &x; }

/* CASE 414: [compound] address of struct S compound literal returned */
struct S *escape_414(void) { return &(struct S){0}; }

/* CASE 415: [compound] char compound-literal array stored into global pointer */
void escape_415(void) { g_char = (char[7]){0}; }

/* CASE 416: [compound] address of int compound literal passed to helper sink */
void escape_416(void) { sink_int(&(int){0}); }

/* CASE 417: [VLA] VLA of short returned directly */
short *escape_417(int sz) { short b[sz]; return b; }

/* CASE 418: [VLA] VLA of long stored into global pointer */
void escape_418(int sz) { long b[sz]; g_long = b; }

/* CASE 419: [VLA] nested-scope VLA of float stored into global pointer */
void escape_419(int sz) { float b[sz]; { float c[sz]; g_float = c; } (void)b; }

/* CASE 420: [VLA] VLA of double returned through a local alias pointer */
double *escape_420(int sz) { double b[sz]; double *p = b; return p; }

/* CASE 421: [VLA] VLA of unsigned char passed to helper function sink */
void escape_421(int sz) { unsigned char b[sz]; sink_uchar(b); }

/* CASE 422: [VLA] VLA of unsigned int stored into function-static pointer */
void escape_422(int sz) { unsigned int b[sz]; static unsigned int *sp; sp = b; (void)sp; }

/* CASE 423: [VLA] VLA of struct S address-of-local-pointer returned (pointer-to-pointer) */
struct S **escape_423(int sz) { struct S b[sz]; struct S *p = b; return &p; }

/* CASE 424: [VLA] VLA of char address-of-local-pointer stored into global void** */
void escape_424(int sz) { char b[sz]; char *p = b; g_voidpp = (void **)&p; }

/* CASE 425: [alloca] alloca block of int returned directly */
int *escape_425(void) { int *p = alloca(10); return p; }

/* CASE 426: [alloca] alloca block of short stored into global pointer */
void escape_426(void) { short *p = alloca(13); g_short = p; }

/* CASE 427: [alloca] alloca block of long passed to helper function sink */
void escape_427(void) { long *p = alloca(16); sink_long(p); }

/* CASE 428: [alloca] alloca block of float stored into function-static pointer */
void escape_428(void) { float *p = alloca(19); static float *sp; sp = p; (void)sp; }

/* CASE 429: [alloca] alloca block of double aliased in nested scope then stored globally */
void escape_429(void) { double *p = alloca(22); { double *q = p; g_double = q; } }

/* CASE 430: [alloca] alloca block of unsigned char returned through pointer-to-pointer */
unsigned char **escape_430(void) { unsigned char *p = alloca(25); return &p; }

/* CASE 431: [alloca] alloca block of unsigned int pointer-to-pointer stored into global void** */
void escape_431(void) { unsigned int *p = alloca(28); g_voidpp = (void **)&p; }

/* CASE 432: [fixed] fixed struct S array returned directly */
struct S *escape_432(void) { struct S b[4]; return b; }

/* CASE 433: [fixed] fixed char array stored into global pointer */
void escape_433(void) { char b[7]; g_char = b; }

/* CASE 434: [fixed] fixed int array passed to helper function sink */
void escape_434(void) { int b[10]; sink_int(b); }

/* CASE 435: [fixed] fixed short array stored into function-static pointer */
void escape_435(void) { short b[13]; static short *sp; sp = b; (void)sp; }

/* CASE 436: [fixed] nested-scope fixed long array returned */
long *escape_436(void) { long b[16]; { long c[16]; return c; } }

/* CASE 437: [fixed] fixed float array address-of-local-pointer returned */
float **escape_437(void) { float b[19]; float *p = b; return &p; }

/* CASE 438: [fixed] address of local double scalar returned */
double *escape_438(void) { double x = (double)0; return &x; }

/* CASE 439: [fixed] address of local unsigned char scalar stored into global pointer */
void escape_439(void) { unsigned char x = (unsigned char)0; g_uchar = &x; }

/* CASE 440: [compound] address of unsigned int compound literal returned */
unsigned int *escape_440(void) { return &(unsigned int){0}; }

/* CASE 441: [compound] struct S compound-literal array stored into global pointer */
void escape_441(void) { g_struct = (struct S[4]){0}; }

/* CASE 442: [compound] address of char compound literal passed to helper sink */
void escape_442(void) { sink_char(&(char){0}); }

/* CASE 443: [VLA] VLA of int returned directly */
int *escape_443(int sz) { int b[sz]; return b; }

/* CASE 444: [VLA] VLA of short stored into global pointer */
void escape_444(int sz) { short b[sz]; g_short = b; }

/* CASE 445: [VLA] nested-scope VLA of long stored into global pointer */
void escape_445(int sz) { long b[sz]; { long c[sz]; g_long = c; } (void)b; }

/* CASE 446: [VLA] VLA of float returned through a local alias pointer */
float *escape_446(int sz) { float b[sz]; float *p = b; return p; }

/* CASE 447: [VLA] VLA of double passed to helper function sink */
void escape_447(int sz) { double b[sz]; sink_double(b); }

/* CASE 448: [VLA] VLA of unsigned char stored into function-static pointer */
void escape_448(int sz) { unsigned char b[sz]; static unsigned char *sp; sp = b; (void)sp; }

/* CASE 449: [VLA] VLA of unsigned int address-of-local-pointer returned (pointer-to-pointer) */
unsigned int **escape_449(int sz) { unsigned int b[sz]; unsigned int *p = b; return &p; }

/* CASE 450: [VLA] VLA of struct S address-of-local-pointer stored into global void** */
void escape_450(int sz) { struct S b[sz]; struct S *p = b; g_voidpp = (void **)&p; }

/* CASE 451: [alloca] alloca block of char returned directly */
char *escape_451(void) { char *p = alloca(7); return p; }

/* CASE 452: [alloca] alloca block of int stored into global pointer */
void escape_452(void) { int *p = alloca(10); g_int = p; }

/* CASE 453: [alloca] alloca block of short passed to helper function sink */
void escape_453(void) { short *p = alloca(13); sink_short(p); }

/* CASE 454: [alloca] alloca block of long stored into function-static pointer */
void escape_454(void) { long *p = alloca(16); static long *sp; sp = p; (void)sp; }

/* CASE 455: [alloca] alloca block of float aliased in nested scope then stored globally */
void escape_455(void) { float *p = alloca(19); { float *q = p; g_float = q; } }

/* CASE 456: [alloca] alloca block of double returned through pointer-to-pointer */
double **escape_456(void) { double *p = alloca(22); return &p; }

/* CASE 457: [alloca] alloca block of unsigned char pointer-to-pointer stored into global void** */
void escape_457(void) { unsigned char *p = alloca(25); g_voidpp = (void **)&p; }

/* CASE 458: [fixed] fixed unsigned int array returned directly */
unsigned int *escape_458(void) { unsigned int b[28]; return b; }

/* CASE 459: [fixed] fixed struct S array stored into global pointer */
void escape_459(void) { struct S b[4]; g_struct = b; }

/* CASE 460: [fixed] fixed char array passed to helper function sink */
void escape_460(void) { char b[7]; sink_char(b); }

/* CASE 461: [fixed] fixed int array stored into function-static pointer */
void escape_461(void) { int b[10]; static int *sp; sp = b; (void)sp; }

/* CASE 462: [fixed] nested-scope fixed short array returned */
short *escape_462(void) { short b[13]; { short c[13]; return c; } }

/* CASE 463: [fixed] fixed long array address-of-local-pointer returned */
long **escape_463(void) { long b[16]; long *p = b; return &p; }

/* CASE 464: [fixed] address of local float scalar returned */
float *escape_464(void) { float x = (float)0; return &x; }

/* CASE 465: [fixed] address of local double scalar stored into global pointer */
void escape_465(void) { double x = (double)0; g_double = &x; }

/* CASE 466: [compound] address of unsigned char compound literal returned */
unsigned char *escape_466(void) { return &(unsigned char){0}; }

/* CASE 467: [compound] unsigned int compound-literal array stored into global pointer */
void escape_467(void) { g_uint = (unsigned int[28]){0}; }

/* CASE 468: [compound] address of struct S compound literal passed to helper sink */
void escape_468(void) { sink_struct(&(struct S){0}); }

/* CASE 469: [VLA] VLA of char returned directly */
char *escape_469(int sz) { char b[sz]; return b; }

/* CASE 470: [VLA] VLA of int stored into global pointer */
void escape_470(int sz) { int b[sz]; g_int = b; }

/* CASE 471: [VLA] nested-scope VLA of short stored into global pointer */
void escape_471(int sz) { short b[sz]; { short c[sz]; g_short = c; } (void)b; }

/* CASE 472: [VLA] VLA of long returned through a local alias pointer */
long *escape_472(int sz) { long b[sz]; long *p = b; return p; }

/* CASE 473: [VLA] VLA of float passed to helper function sink */
void escape_473(int sz) { float b[sz]; sink_float(b); }

/* CASE 474: [VLA] VLA of double stored into function-static pointer */
void escape_474(int sz) { double b[sz]; static double *sp; sp = b; (void)sp; }

/* CASE 475: [VLA] VLA of unsigned char address-of-local-pointer returned (pointer-to-pointer) */
unsigned char **escape_475(int sz) { unsigned char b[sz]; unsigned char *p = b; return &p; }

/* CASE 476: [VLA] VLA of unsigned int address-of-local-pointer stored into global void** */
void escape_476(int sz) { unsigned int b[sz]; unsigned int *p = b; g_voidpp = (void **)&p; }

/* CASE 477: [alloca] alloca block of struct S returned directly */
struct S *escape_477(void) { struct S *p = alloca(4); return p; }

/* CASE 478: [alloca] alloca block of char stored into global pointer */
void escape_478(void) { char *p = alloca(7); g_char = p; }

/* CASE 479: [alloca] alloca block of int passed to helper function sink */
void escape_479(void) { int *p = alloca(10); sink_int(p); }

/* CASE 480: [alloca] alloca block of short stored into function-static pointer */
void escape_480(void) { short *p = alloca(13); static short *sp; sp = p; (void)sp; }

/* CASE 481: [alloca] alloca block of long aliased in nested scope then stored globally */
void escape_481(void) { long *p = alloca(16); { long *q = p; g_long = q; } }

/* CASE 482: [alloca] alloca block of float returned through pointer-to-pointer */
float **escape_482(void) { float *p = alloca(19); return &p; }

/* CASE 483: [alloca] alloca block of double pointer-to-pointer stored into global void** */
void escape_483(void) { double *p = alloca(22); g_voidpp = (void **)&p; }

/* CASE 484: [fixed] fixed unsigned char array returned directly */
unsigned char *escape_484(void) { unsigned char b[25]; return b; }

/* CASE 485: [fixed] fixed unsigned int array stored into global pointer */
void escape_485(void) { unsigned int b[28]; g_uint = b; }

/* CASE 486: [fixed] fixed struct S array passed to helper function sink */
void escape_486(void) { struct S b[4]; sink_struct(b); }

/* CASE 487: [fixed] fixed char array stored into function-static pointer */
void escape_487(void) { char b[7]; static char *sp; sp = b; (void)sp; }

/* CASE 488: [fixed] nested-scope fixed int array returned */
int *escape_488(void) { int b[10]; { int c[10]; return c; } }

/* CASE 489: [fixed] fixed short array address-of-local-pointer returned */
short **escape_489(void) { short b[13]; short *p = b; return &p; }

/* CASE 490: [fixed] address of local long scalar returned */
long *escape_490(void) { long x = (long)0; return &x; }

/* CASE 491: [fixed] address of local float scalar stored into global pointer */
void escape_491(void) { float x = (float)0; g_float = &x; }

/* CASE 492: [compound] address of double compound literal returned */
double *escape_492(void) { return &(double){0}; }

/* CASE 493: [compound] unsigned char compound-literal array stored into global pointer */
void escape_493(void) { g_uchar = (unsigned char[25]){0}; }

/* CASE 494: [compound] address of unsigned int compound literal passed to helper sink */
void escape_494(void) { sink_uint(&(unsigned int){0}); }

/* CASE 495: [VLA] VLA of struct S returned directly */
struct S *escape_495(int sz) { struct S b[sz]; return b; }

/* CASE 496: [VLA] VLA of char stored into global pointer */
void escape_496(int sz) { char b[sz]; g_char = b; }

/* CASE 497: [VLA] nested-scope VLA of int stored into global pointer */
void escape_497(int sz) { int b[sz]; { int c[sz]; g_int = c; } (void)b; }

/* CASE 498: [VLA] VLA of short returned through a local alias pointer */
short *escape_498(int sz) { short b[sz]; short *p = b; return p; }

/* CASE 499: [VLA] VLA of long passed to helper function sink */
void escape_499(int sz) { long b[sz]; sink_long(b); }

/* CASE 500: [VLA] VLA of float stored into function-static pointer */
void escape_500(int sz) { float b[sz]; static float *sp; sp = b; (void)sp; }

/* CASE 501: [VLA] VLA of double address-of-local-pointer returned (pointer-to-pointer) */
double **escape_501(int sz) { double b[sz]; double *p = b; return &p; }

/* CASE 502: [VLA] VLA of unsigned char address-of-local-pointer stored into global void** */
void escape_502(int sz) { unsigned char b[sz]; unsigned char *p = b; g_voidpp = (void **)&p; }

/* CASE 503: [alloca] alloca block of unsigned int returned directly */
unsigned int *escape_503(void) { unsigned int *p = alloca(28); return p; }

/* CASE 504: [alloca] alloca block of struct S stored into global pointer */
void escape_504(void) { struct S *p = alloca(4); g_struct = p; }

/* CASE 505: [alloca] alloca block of char passed to helper function sink */
void escape_505(void) { char *p = alloca(7); sink_char(p); }

/* CASE 506: [alloca] alloca block of int stored into function-static pointer */
void escape_506(void) { int *p = alloca(10); static int *sp; sp = p; (void)sp; }

/* CASE 507: [alloca] alloca block of short aliased in nested scope then stored globally */
void escape_507(void) { short *p = alloca(13); { short *q = p; g_short = q; } }

/* CASE 508: [alloca] alloca block of long returned through pointer-to-pointer */
long **escape_508(void) { long *p = alloca(16); return &p; }

/* CASE 509: [alloca] alloca block of float pointer-to-pointer stored into global void** */
void escape_509(void) { float *p = alloca(19); g_voidpp = (void **)&p; }

/* CASE 510: [fixed] fixed double array returned directly */
double *escape_510(void) { double b[22]; return b; }

/* CASE 511: [fixed] fixed unsigned char array stored into global pointer */
void escape_511(void) { unsigned char b[25]; g_uchar = b; }

/* CASE 512: [fixed] fixed unsigned int array passed to helper function sink */
void escape_512(void) { unsigned int b[28]; sink_uint(b); }

/* CASE 513: [fixed] fixed struct S array stored into function-static pointer */
void escape_513(void) { struct S b[4]; static struct S *sp; sp = b; (void)sp; }

/* CASE 514: [fixed] nested-scope fixed char array returned */
char *escape_514(void) { char b[7]; { char c[7]; return c; } }

/* CASE 515: [fixed] fixed int array address-of-local-pointer returned */
int **escape_515(void) { int b[10]; int *p = b; return &p; }

/* CASE 516: [fixed] address of local short scalar returned */
short *escape_516(void) { short x = (short)0; return &x; }

/* CASE 517: [fixed] address of local long scalar stored into global pointer */
void escape_517(void) { long x = (long)0; g_long = &x; }

/* CASE 518: [compound] address of float compound literal returned */
float *escape_518(void) { return &(float){0}; }

/* CASE 519: [compound] double compound-literal array stored into global pointer */
void escape_519(void) { g_double = (double[22]){0}; }

/* CASE 520: [compound] address of unsigned char compound literal passed to helper sink */
void escape_520(void) { sink_uchar(&(unsigned char){0}); }

/* CASE 521: [VLA] VLA of unsigned int returned directly */
unsigned int *escape_521(int sz) { unsigned int b[sz]; return b; }

/* CASE 522: [VLA] VLA of struct S stored into global pointer */
void escape_522(int sz) { struct S b[sz]; g_struct = b; }

/* CASE 523: [VLA] nested-scope VLA of char stored into global pointer */
void escape_523(int sz) { char b[sz]; { char c[sz]; g_char = c; } (void)b; }

/* CASE 524: [VLA] VLA of int returned through a local alias pointer */
int *escape_524(int sz) { int b[sz]; int *p = b; return p; }

/* CASE 525: [VLA] VLA of short passed to helper function sink */
void escape_525(int sz) { short b[sz]; sink_short(b); }

/* CASE 526: [VLA] VLA of long stored into function-static pointer */
void escape_526(int sz) { long b[sz]; static long *sp; sp = b; (void)sp; }

/* CASE 527: [VLA] VLA of float address-of-local-pointer returned (pointer-to-pointer) */
float **escape_527(int sz) { float b[sz]; float *p = b; return &p; }

/* CASE 528: [VLA] VLA of double address-of-local-pointer stored into global void** */
void escape_528(int sz) { double b[sz]; double *p = b; g_voidpp = (void **)&p; }

/* CASE 529: [alloca] alloca block of unsigned char returned directly */
unsigned char *escape_529(void) { unsigned char *p = alloca(25); return p; }

/* CASE 530: [alloca] alloca block of unsigned int stored into global pointer */
void escape_530(void) { unsigned int *p = alloca(28); g_uint = p; }

/* CASE 531: [alloca] alloca block of struct S passed to helper function sink */
void escape_531(void) { struct S *p = alloca(4); sink_struct(p); }

/* CASE 532: [alloca] alloca block of char stored into function-static pointer */
void escape_532(void) { char *p = alloca(7); static char *sp; sp = p; (void)sp; }

/* CASE 533: [alloca] alloca block of int aliased in nested scope then stored globally */
void escape_533(void) { int *p = alloca(10); { int *q = p; g_int = q; } }

/* CASE 534: [alloca] alloca block of short returned through pointer-to-pointer */
short **escape_534(void) { short *p = alloca(13); return &p; }

/* CASE 535: [alloca] alloca block of long pointer-to-pointer stored into global void** */
void escape_535(void) { long *p = alloca(16); g_voidpp = (void **)&p; }

/* CASE 536: [fixed] fixed float array returned directly */
float *escape_536(void) { float b[19]; return b; }

/* CASE 537: [fixed] fixed double array stored into global pointer */
void escape_537(void) { double b[22]; g_double = b; }

/* CASE 538: [fixed] fixed unsigned char array passed to helper function sink */
void escape_538(void) { unsigned char b[25]; sink_uchar(b); }

/* CASE 539: [fixed] fixed unsigned int array stored into function-static pointer */
void escape_539(void) { unsigned int b[28]; static unsigned int *sp; sp = b; (void)sp; }

/* CASE 540: [fixed] nested-scope fixed struct S array returned */
struct S *escape_540(void) { struct S b[4]; { struct S c[4]; return c; } }

/* CASE 541: [fixed] fixed char array address-of-local-pointer returned */
char **escape_541(void) { char b[7]; char *p = b; return &p; }

/* CASE 542: [fixed] address of local int scalar returned */
int *escape_542(void) { int x = (int)0; return &x; }

/* CASE 543: [fixed] address of local short scalar stored into global pointer */
void escape_543(void) { short x = (short)0; g_short = &x; }

/* CASE 544: [compound] address of long compound literal returned */
long *escape_544(void) { return &(long){0}; }

/* CASE 545: [compound] float compound-literal array stored into global pointer */
void escape_545(void) { g_float = (float[19]){0}; }

/* CASE 546: [compound] address of double compound literal passed to helper sink */
void escape_546(void) { sink_double(&(double){0}); }

/* CASE 547: [VLA] VLA of unsigned char returned directly */
unsigned char *escape_547(int sz) { unsigned char b[sz]; return b; }

/* CASE 548: [VLA] VLA of unsigned int stored into global pointer */
void escape_548(int sz) { unsigned int b[sz]; g_uint = b; }

/* CASE 549: [VLA] nested-scope VLA of struct S stored into global pointer */
void escape_549(int sz) { struct S b[sz]; { struct S c[sz]; g_struct = c; } (void)b; }

/* CASE 550: [VLA] VLA of char returned through a local alias pointer */
char *escape_550(int sz) { char b[sz]; char *p = b; return p; }

/* CASE 551: [VLA] VLA of int passed to helper function sink */
void escape_551(int sz) { int b[sz]; sink_int(b); }

/* CASE 552: [VLA] VLA of short stored into function-static pointer */
void escape_552(int sz) { short b[sz]; static short *sp; sp = b; (void)sp; }

/* CASE 553: [VLA] VLA of long address-of-local-pointer returned (pointer-to-pointer) */
long **escape_553(int sz) { long b[sz]; long *p = b; return &p; }

/* CASE 554: [VLA] VLA of float address-of-local-pointer stored into global void** */
void escape_554(int sz) { float b[sz]; float *p = b; g_voidpp = (void **)&p; }

/* CASE 555: [alloca] alloca block of double returned directly */
double *escape_555(void) { double *p = alloca(22); return p; }

/* CASE 556: [alloca] alloca block of unsigned char stored into global pointer */
void escape_556(void) { unsigned char *p = alloca(25); g_uchar = p; }

/* CASE 557: [alloca] alloca block of unsigned int passed to helper function sink */
void escape_557(void) { unsigned int *p = alloca(28); sink_uint(p); }

/* CASE 558: [alloca] alloca block of struct S stored into function-static pointer */
void escape_558(void) { struct S *p = alloca(4); static struct S *sp; sp = p; (void)sp; }

/* CASE 559: [alloca] alloca block of char aliased in nested scope then stored globally */
void escape_559(void) { char *p = alloca(7); { char *q = p; g_char = q; } }

/* CASE 560: [alloca] alloca block of int returned through pointer-to-pointer */
int **escape_560(void) { int *p = alloca(10); return &p; }

/* CASE 561: [alloca] alloca block of short pointer-to-pointer stored into global void** */
void escape_561(void) { short *p = alloca(13); g_voidpp = (void **)&p; }

/* CASE 562: [fixed] fixed long array returned directly */
long *escape_562(void) { long b[16]; return b; }

/* CASE 563: [fixed] fixed float array stored into global pointer */
void escape_563(void) { float b[19]; g_float = b; }

/* CASE 564: [fixed] fixed double array passed to helper function sink */
void escape_564(void) { double b[22]; sink_double(b); }

/* CASE 565: [fixed] fixed unsigned char array stored into function-static pointer */
void escape_565(void) { unsigned char b[25]; static unsigned char *sp; sp = b; (void)sp; }

/* CASE 566: [fixed] nested-scope fixed unsigned int array returned */
unsigned int *escape_566(void) { unsigned int b[28]; { unsigned int c[28]; return c; } }

/* CASE 567: [fixed] fixed struct S array address-of-local-pointer returned */
struct S **escape_567(void) { struct S b[4]; struct S *p = b; return &p; }

/* CASE 568: [fixed] address of local char scalar returned */
char *escape_568(void) { char x = (char)0; return &x; }

/* CASE 569: [fixed] address of local int scalar stored into global pointer */
void escape_569(void) { int x = (int)0; g_int = &x; }

/* CASE 570: [compound] address of short compound literal returned */
short *escape_570(void) { return &(short){0}; }

/* CASE 571: [compound] long compound-literal array stored into global pointer */
void escape_571(void) { g_long = (long[16]){0}; }

/* CASE 572: [compound] address of float compound literal passed to helper sink */
void escape_572(void) { sink_float(&(float){0}); }

/* CASE 573: [VLA] VLA of double returned directly */
double *escape_573(int sz) { double b[sz]; return b; }

/* CASE 574: [VLA] VLA of unsigned char stored into global pointer */
void escape_574(int sz) { unsigned char b[sz]; g_uchar = b; }

/* CASE 575: [VLA] nested-scope VLA of unsigned int stored into global pointer */
void escape_575(int sz) { unsigned int b[sz]; { unsigned int c[sz]; g_uint = c; } (void)b; }

/* CASE 576: [VLA] VLA of struct S returned through a local alias pointer */
struct S *escape_576(int sz) { struct S b[sz]; struct S *p = b; return p; }

/* CASE 577: [VLA] VLA of char passed to helper function sink */
void escape_577(int sz) { char b[sz]; sink_char(b); }

/* CASE 578: [VLA] VLA of int stored into function-static pointer */
void escape_578(int sz) { int b[sz]; static int *sp; sp = b; (void)sp; }

/* CASE 579: [VLA] VLA of short address-of-local-pointer returned (pointer-to-pointer) */
short **escape_579(int sz) { short b[sz]; short *p = b; return &p; }

/* CASE 580: [VLA] VLA of long address-of-local-pointer stored into global void** */
void escape_580(int sz) { long b[sz]; long *p = b; g_voidpp = (void **)&p; }

/* CASE 581: [alloca] alloca block of float returned directly */
float *escape_581(void) { float *p = alloca(19); return p; }

/* CASE 582: [alloca] alloca block of double stored into global pointer */
void escape_582(void) { double *p = alloca(22); g_double = p; }

/* CASE 583: [alloca] alloca block of unsigned char passed to helper function sink */
void escape_583(void) { unsigned char *p = alloca(25); sink_uchar(p); }

/* CASE 584: [alloca] alloca block of unsigned int stored into function-static pointer */
void escape_584(void) { unsigned int *p = alloca(28); static unsigned int *sp; sp = p; (void)sp; }

/* CASE 585: [alloca] alloca block of struct S aliased in nested scope then stored globally */
void escape_585(void) { struct S *p = alloca(4); { struct S *q = p; g_struct = q; } }

/* CASE 586: [alloca] alloca block of char returned through pointer-to-pointer */
char **escape_586(void) { char *p = alloca(7); return &p; }

/* CASE 587: [alloca] alloca block of int pointer-to-pointer stored into global void** */
void escape_587(void) { int *p = alloca(10); g_voidpp = (void **)&p; }

/* CASE 588: [fixed] fixed short array returned directly */
short *escape_588(void) { short b[13]; return b; }

/* CASE 589: [fixed] fixed long array stored into global pointer */
void escape_589(void) { long b[16]; g_long = b; }

/* CASE 590: [fixed] fixed float array passed to helper function sink */
void escape_590(void) { float b[19]; sink_float(b); }

/* CASE 591: [fixed] fixed double array stored into function-static pointer */
void escape_591(void) { double b[22]; static double *sp; sp = b; (void)sp; }

/* CASE 592: [fixed] nested-scope fixed unsigned char array returned */
unsigned char *escape_592(void) { unsigned char b[25]; { unsigned char c[25]; return c; } }

/* CASE 593: [fixed] fixed unsigned int array address-of-local-pointer returned */
unsigned int **escape_593(void) { unsigned int b[28]; unsigned int *p = b; return &p; }

/* CASE 594: [fixed] address of local struct S scalar returned */
struct S *escape_594(void) { struct S x = {0}; return &x; }

/* CASE 595: [fixed] address of local char scalar stored into global pointer */
void escape_595(void) { char x = (char)0; g_char = &x; }

/* CASE 596: [compound] address of int compound literal returned */
int *escape_596(void) { return &(int){0}; }

/* CASE 597: [compound] short compound-literal array stored into global pointer */
void escape_597(void) { g_short = (short[13]){0}; }

/* CASE 598: [compound] address of long compound literal passed to helper sink */
void escape_598(void) { sink_long(&(long){0}); }

/* CASE 599: [VLA] VLA of float returned directly */
float *escape_599(int sz) { float b[sz]; return b; }

/* CASE 600: [VLA] VLA of double stored into global pointer */
void escape_600(int sz) { double b[sz]; g_double = b; }

/* CASE 601: [VLA] nested-scope VLA of unsigned char stored into global pointer */
void escape_601(int sz) { unsigned char b[sz]; { unsigned char c[sz]; g_uchar = c; } (void)b; }

/* CASE 602: [VLA] VLA of unsigned int returned through a local alias pointer */
unsigned int *escape_602(int sz) { unsigned int b[sz]; unsigned int *p = b; return p; }

/* CASE 603: [VLA] VLA of struct S passed to helper function sink */
void escape_603(int sz) { struct S b[sz]; sink_struct(b); }

/* CASE 604: [VLA] VLA of char stored into function-static pointer */
void escape_604(int sz) { char b[sz]; static char *sp; sp = b; (void)sp; }

/* CASE 605: [VLA] VLA of int address-of-local-pointer returned (pointer-to-pointer) */
int **escape_605(int sz) { int b[sz]; int *p = b; return &p; }

/* CASE 606: [VLA] VLA of short address-of-local-pointer stored into global void** */
void escape_606(int sz) { short b[sz]; short *p = b; g_voidpp = (void **)&p; }

/* CASE 607: [alloca] alloca block of long returned directly */
long *escape_607(void) { long *p = alloca(16); return p; }

/* CASE 608: [alloca] alloca block of float stored into global pointer */
void escape_608(void) { float *p = alloca(19); g_float = p; }

/* CASE 609: [alloca] alloca block of double passed to helper function sink */
void escape_609(void) { double *p = alloca(22); sink_double(p); }

/* CASE 610: [alloca] alloca block of unsigned char stored into function-static pointer */
void escape_610(void) { unsigned char *p = alloca(25); static unsigned char *sp; sp = p; (void)sp; }

/* CASE 611: [alloca] alloca block of unsigned int aliased in nested scope then stored globally */
void escape_611(void) { unsigned int *p = alloca(28); { unsigned int *q = p; g_uint = q; } }

/* CASE 612: [alloca] alloca block of struct S returned through pointer-to-pointer */
struct S **escape_612(void) { struct S *p = alloca(4); return &p; }

/* CASE 613: [alloca] alloca block of char pointer-to-pointer stored into global void** */
void escape_613(void) { char *p = alloca(7); g_voidpp = (void **)&p; }

/* CASE 614: [fixed] fixed int array returned directly */
int *escape_614(void) { int b[10]; return b; }

/* CASE 615: [fixed] fixed short array stored into global pointer */
void escape_615(void) { short b[13]; g_short = b; }

/* CASE 616: [fixed] fixed long array passed to helper function sink */
void escape_616(void) { long b[16]; sink_long(b); }

/* CASE 617: [fixed] fixed float array stored into function-static pointer */
void escape_617(void) { float b[19]; static float *sp; sp = b; (void)sp; }

/* CASE 618: [fixed] nested-scope fixed double array returned */
double *escape_618(void) { double b[22]; { double c[22]; return c; } }

/* CASE 619: [fixed] fixed unsigned char array address-of-local-pointer returned */
unsigned char **escape_619(void) { unsigned char b[25]; unsigned char *p = b; return &p; }

/* CASE 620: [fixed] address of local unsigned int scalar returned */
unsigned int *escape_620(void) { unsigned int x = (unsigned int)0; return &x; }

/* CASE 621: [fixed] address of local struct S scalar stored into global pointer */
void escape_621(void) { struct S x = {0}; g_struct = &x; }

/* CASE 622: [compound] address of char compound literal returned */
char *escape_622(void) { return &(char){0}; }

/* CASE 623: [compound] int compound-literal array stored into global pointer */
void escape_623(void) { g_int = (int[10]){0}; }

/* CASE 624: [compound] address of short compound literal passed to helper sink */
void escape_624(void) { sink_short(&(short){0}); }

/* CASE 625: [VLA] VLA of long returned directly */
long *escape_625(int sz) { long b[sz]; return b; }

/* CASE 626: [VLA] VLA of float stored into global pointer */
void escape_626(int sz) { float b[sz]; g_float = b; }

/* CASE 627: [VLA] nested-scope VLA of double stored into global pointer */
void escape_627(int sz) { double b[sz]; { double c[sz]; g_double = c; } (void)b; }

/* CASE 628: [VLA] VLA of unsigned char returned through a local alias pointer */
unsigned char *escape_628(int sz) { unsigned char b[sz]; unsigned char *p = b; return p; }

/* CASE 629: [VLA] VLA of unsigned int passed to helper function sink */
void escape_629(int sz) { unsigned int b[sz]; sink_uint(b); }

/* CASE 630: [VLA] VLA of struct S stored into function-static pointer */
void escape_630(int sz) { struct S b[sz]; static struct S *sp; sp = b; (void)sp; }

/* CASE 631: [VLA] VLA of char address-of-local-pointer returned (pointer-to-pointer) */
char **escape_631(int sz) { char b[sz]; char *p = b; return &p; }

/* CASE 632: [VLA] VLA of int address-of-local-pointer stored into global void** */
void escape_632(int sz) { int b[sz]; int *p = b; g_voidpp = (void **)&p; }

/* CASE 633: [alloca] alloca block of short returned directly */
short *escape_633(void) { short *p = alloca(13); return p; }

/* CASE 634: [alloca] alloca block of long stored into global pointer */
void escape_634(void) { long *p = alloca(16); g_long = p; }

/* CASE 635: [alloca] alloca block of float passed to helper function sink */
void escape_635(void) { float *p = alloca(19); sink_float(p); }

/* CASE 636: [alloca] alloca block of double stored into function-static pointer */
void escape_636(void) { double *p = alloca(22); static double *sp; sp = p; (void)sp; }

/* CASE 637: [alloca] alloca block of unsigned char aliased in nested scope then stored globally */
void escape_637(void) { unsigned char *p = alloca(25); { unsigned char *q = p; g_uchar = q; } }

/* CASE 638: [alloca] alloca block of unsigned int returned through pointer-to-pointer */
unsigned int **escape_638(void) { unsigned int *p = alloca(28); return &p; }

/* CASE 639: [alloca] alloca block of struct S pointer-to-pointer stored into global void** */
void escape_639(void) { struct S *p = alloca(4); g_voidpp = (void **)&p; }

/* CASE 640: [fixed] fixed char array returned directly */
char *escape_640(void) { char b[7]; return b; }

/* CASE 641: [fixed] fixed int array stored into global pointer */
void escape_641(void) { int b[10]; g_int = b; }

/* CASE 642: [fixed] fixed short array passed to helper function sink */
void escape_642(void) { short b[13]; sink_short(b); }

/* CASE 643: [fixed] fixed long array stored into function-static pointer */
void escape_643(void) { long b[16]; static long *sp; sp = b; (void)sp; }

/* CASE 644: [fixed] nested-scope fixed float array returned */
float *escape_644(void) { float b[19]; { float c[19]; return c; } }

/* CASE 645: [fixed] fixed double array address-of-local-pointer returned */
double **escape_645(void) { double b[22]; double *p = b; return &p; }

/* CASE 646: [fixed] address of local unsigned char scalar returned */
unsigned char *escape_646(void) { unsigned char x = (unsigned char)0; return &x; }

/* CASE 647: [fixed] address of local unsigned int scalar stored into global pointer */
void escape_647(void) { unsigned int x = (unsigned int)0; g_uint = &x; }

/* CASE 648: [compound] address of struct S compound literal returned */
struct S *escape_648(void) { return &(struct S){0}; }

/* CASE 649: [compound] char compound-literal array stored into global pointer */
void escape_649(void) { g_char = (char[7]){0}; }

/* CASE 650: [compound] address of int compound literal passed to helper sink */
void escape_650(void) { sink_int(&(int){0}); }

/* CASE 651: [VLA] VLA of short returned directly */
short *escape_651(int sz) { short b[sz]; return b; }

/* CASE 652: [VLA] VLA of long stored into global pointer */
void escape_652(int sz) { long b[sz]; g_long = b; }

/* CASE 653: [VLA] nested-scope VLA of float stored into global pointer */
void escape_653(int sz) { float b[sz]; { float c[sz]; g_float = c; } (void)b; }

/* CASE 654: [VLA] VLA of double returned through a local alias pointer */
double *escape_654(int sz) { double b[sz]; double *p = b; return p; }

/* CASE 655: [VLA] VLA of unsigned char passed to helper function sink */
void escape_655(int sz) { unsigned char b[sz]; sink_uchar(b); }

/* CASE 656: [VLA] VLA of unsigned int stored into function-static pointer */
void escape_656(int sz) { unsigned int b[sz]; static unsigned int *sp; sp = b; (void)sp; }

/* CASE 657: [VLA] VLA of struct S address-of-local-pointer returned (pointer-to-pointer) */
struct S **escape_657(int sz) { struct S b[sz]; struct S *p = b; return &p; }

/* CASE 658: [VLA] VLA of char address-of-local-pointer stored into global void** */
void escape_658(int sz) { char b[sz]; char *p = b; g_voidpp = (void **)&p; }

/* CASE 659: [alloca] alloca block of int returned directly */
int *escape_659(void) { int *p = alloca(10); return p; }

/* CASE 660: [alloca] alloca block of short stored into global pointer */
void escape_660(void) { short *p = alloca(13); g_short = p; }

/* CASE 661: [alloca] alloca block of long passed to helper function sink */
void escape_661(void) { long *p = alloca(16); sink_long(p); }

/* CASE 662: [alloca] alloca block of float stored into function-static pointer */
void escape_662(void) { float *p = alloca(19); static float *sp; sp = p; (void)sp; }

/* CASE 663: [alloca] alloca block of double aliased in nested scope then stored globally */
void escape_663(void) { double *p = alloca(22); { double *q = p; g_double = q; } }

/* CASE 664: [alloca] alloca block of unsigned char returned through pointer-to-pointer */
unsigned char **escape_664(void) { unsigned char *p = alloca(25); return &p; }

/* CASE 665: [alloca] alloca block of unsigned int pointer-to-pointer stored into global void** */
void escape_665(void) { unsigned int *p = alloca(28); g_voidpp = (void **)&p; }

/* CASE 666: [fixed] fixed struct S array returned directly */
struct S *escape_666(void) { struct S b[4]; return b; }

/* CASE 667: [fixed] fixed char array stored into global pointer */
void escape_667(void) { char b[7]; g_char = b; }

/* CASE 668: [fixed] fixed int array passed to helper function sink */
void escape_668(void) { int b[10]; sink_int(b); }

/* CASE 669: [fixed] fixed short array stored into function-static pointer */
void escape_669(void) { short b[13]; static short *sp; sp = b; (void)sp; }

/* CASE 670: [fixed] nested-scope fixed long array returned */
long *escape_670(void) { long b[16]; { long c[16]; return c; } }

/* CASE 671: [fixed] fixed float array address-of-local-pointer returned */
float **escape_671(void) { float b[19]; float *p = b; return &p; }

/* CASE 672: [fixed] address of local double scalar returned */
double *escape_672(void) { double x = (double)0; return &x; }

/* CASE 673: [fixed] address of local unsigned char scalar stored into global pointer */
void escape_673(void) { unsigned char x = (unsigned char)0; g_uchar = &x; }

/* CASE 674: [compound] address of unsigned int compound literal returned */
unsigned int *escape_674(void) { return &(unsigned int){0}; }

/* CASE 675: [compound] struct S compound-literal array stored into global pointer */
void escape_675(void) { g_struct = (struct S[4]){0}; }

/* CASE 676: [compound] address of char compound literal passed to helper sink */
void escape_676(void) { sink_char(&(char){0}); }

/* CASE 677: [VLA] VLA of int returned directly */
int *escape_677(int sz) { int b[sz]; return b; }

/* CASE 678: [VLA] VLA of short stored into global pointer */
void escape_678(int sz) { short b[sz]; g_short = b; }

/* CASE 679: [VLA] nested-scope VLA of long stored into global pointer */
void escape_679(int sz) { long b[sz]; { long c[sz]; g_long = c; } (void)b; }

/* CASE 680: [VLA] VLA of float returned through a local alias pointer */
float *escape_680(int sz) { float b[sz]; float *p = b; return p; }

/* CASE 681: [VLA] VLA of double passed to helper function sink */
void escape_681(int sz) { double b[sz]; sink_double(b); }

/* CASE 682: [VLA] VLA of unsigned char stored into function-static pointer */
void escape_682(int sz) { unsigned char b[sz]; static unsigned char *sp; sp = b; (void)sp; }

/* CASE 683: [VLA] VLA of unsigned int address-of-local-pointer returned (pointer-to-pointer) */
unsigned int **escape_683(int sz) { unsigned int b[sz]; unsigned int *p = b; return &p; }

/* CASE 684: [VLA] VLA of struct S address-of-local-pointer stored into global void** */
void escape_684(int sz) { struct S b[sz]; struct S *p = b; g_voidpp = (void **)&p; }

/* CASE 685: [alloca] alloca block of char returned directly */
char *escape_685(void) { char *p = alloca(7); return p; }

/* CASE 686: [alloca] alloca block of int stored into global pointer */
void escape_686(void) { int *p = alloca(10); g_int = p; }

/* CASE 687: [alloca] alloca block of short passed to helper function sink */
void escape_687(void) { short *p = alloca(13); sink_short(p); }

/* CASE 688: [alloca] alloca block of long stored into function-static pointer */
void escape_688(void) { long *p = alloca(16); static long *sp; sp = p; (void)sp; }

/* CASE 689: [alloca] alloca block of float aliased in nested scope then stored globally */
void escape_689(void) { float *p = alloca(19); { float *q = p; g_float = q; } }

/* CASE 690: [alloca] alloca block of double returned through pointer-to-pointer */
double **escape_690(void) { double *p = alloca(22); return &p; }

/* CASE 691: [alloca] alloca block of unsigned char pointer-to-pointer stored into global void** */
void escape_691(void) { unsigned char *p = alloca(25); g_voidpp = (void **)&p; }

/* CASE 692: [fixed] fixed unsigned int array returned directly */
unsigned int *escape_692(void) { unsigned int b[28]; return b; }

/* CASE 693: [fixed] fixed struct S array stored into global pointer */
void escape_693(void) { struct S b[4]; g_struct = b; }

/* CASE 694: [fixed] fixed char array passed to helper function sink */
void escape_694(void) { char b[7]; sink_char(b); }

/* CASE 695: [fixed] fixed int array stored into function-static pointer */
void escape_695(void) { int b[10]; static int *sp; sp = b; (void)sp; }

/* CASE 696: [fixed] nested-scope fixed short array returned */
short *escape_696(void) { short b[13]; { short c[13]; return c; } }

/* CASE 697: [fixed] fixed long array address-of-local-pointer returned */
long **escape_697(void) { long b[16]; long *p = b; return &p; }

/* CASE 698: [fixed] address of local float scalar returned */
float *escape_698(void) { float x = (float)0; return &x; }

/* CASE 699: [fixed] address of local double scalar stored into global pointer */
void escape_699(void) { double x = (double)0; g_double = &x; }

/* CASE 700: [compound] address of unsigned char compound literal returned */
unsigned char *escape_700(void) { return &(unsigned char){0}; }

/* CASE 701: [compound] unsigned int compound-literal array stored into global pointer */
void escape_701(void) { g_uint = (unsigned int[28]){0}; }

/* CASE 702: [compound] address of struct S compound literal passed to helper sink */
void escape_702(void) { sink_struct(&(struct S){0}); }

/* CASE 703: [VLA] VLA of char returned directly */
char *escape_703(int sz) { char b[sz]; return b; }

/* CASE 704: [VLA] VLA of int stored into global pointer */
void escape_704(int sz) { int b[sz]; g_int = b; }

/* CASE 705: [VLA] nested-scope VLA of short stored into global pointer */
void escape_705(int sz) { short b[sz]; { short c[sz]; g_short = c; } (void)b; }

/* CASE 706: [VLA] VLA of long returned through a local alias pointer */
long *escape_706(int sz) { long b[sz]; long *p = b; return p; }

/* CASE 707: [VLA] VLA of float passed to helper function sink */
void escape_707(int sz) { float b[sz]; sink_float(b); }

/* CASE 708: [VLA] VLA of double stored into function-static pointer */
void escape_708(int sz) { double b[sz]; static double *sp; sp = b; (void)sp; }

/* CASE 709: [VLA] VLA of unsigned char address-of-local-pointer returned (pointer-to-pointer) */
unsigned char **escape_709(int sz) { unsigned char b[sz]; unsigned char *p = b; return &p; }

/* CASE 710: [VLA] VLA of unsigned int address-of-local-pointer stored into global void** */
void escape_710(int sz) { unsigned int b[sz]; unsigned int *p = b; g_voidpp = (void **)&p; }

/* CASE 711: [alloca] alloca block of struct S returned directly */
struct S *escape_711(void) { struct S *p = alloca(4); return p; }

/* CASE 712: [alloca] alloca block of char stored into global pointer */
void escape_712(void) { char *p = alloca(7); g_char = p; }

/* CASE 713: [alloca] alloca block of int passed to helper function sink */
void escape_713(void) { int *p = alloca(10); sink_int(p); }

/* CASE 714: [alloca] alloca block of short stored into function-static pointer */
void escape_714(void) { short *p = alloca(13); static short *sp; sp = p; (void)sp; }

/* CASE 715: [alloca] alloca block of long aliased in nested scope then stored globally */
void escape_715(void) { long *p = alloca(16); { long *q = p; g_long = q; } }

/* CASE 716: [alloca] alloca block of float returned through pointer-to-pointer */
float **escape_716(void) { float *p = alloca(19); return &p; }

/* CASE 717: [alloca] alloca block of double pointer-to-pointer stored into global void** */
void escape_717(void) { double *p = alloca(22); g_voidpp = (void **)&p; }

/* CASE 718: [fixed] fixed unsigned char array returned directly */
unsigned char *escape_718(void) { unsigned char b[25]; return b; }

/* CASE 719: [fixed] fixed unsigned int array stored into global pointer */
void escape_719(void) { unsigned int b[28]; g_uint = b; }

/* CASE 720: [fixed] fixed struct S array passed to helper function sink */
void escape_720(void) { struct S b[4]; sink_struct(b); }

/* CASE 721: [fixed] fixed char array stored into function-static pointer */
void escape_721(void) { char b[7]; static char *sp; sp = b; (void)sp; }

/* CASE 722: [fixed] nested-scope fixed int array returned */
int *escape_722(void) { int b[10]; { int c[10]; return c; } }

/* CASE 723: [fixed] fixed short array address-of-local-pointer returned */
short **escape_723(void) { short b[13]; short *p = b; return &p; }

/* CASE 724: [fixed] address of local long scalar returned */
long *escape_724(void) { long x = (long)0; return &x; }

/* CASE 725: [fixed] address of local float scalar stored into global pointer */
void escape_725(void) { float x = (float)0; g_float = &x; }

/* CASE 726: [compound] address of double compound literal returned */
double *escape_726(void) { return &(double){0}; }

/* CASE 727: [compound] unsigned char compound-literal array stored into global pointer */
void escape_727(void) { g_uchar = (unsigned char[25]){0}; }

/* CASE 728: [compound] address of unsigned int compound literal passed to helper sink */
void escape_728(void) { sink_uint(&(unsigned int){0}); }

/* CASE 729: [VLA] VLA of struct S returned directly */
struct S *escape_729(int sz) { struct S b[sz]; return b; }

/* CASE 730: [VLA] VLA of char stored into global pointer */
void escape_730(int sz) { char b[sz]; g_char = b; }

/* CASE 731: [VLA] nested-scope VLA of int stored into global pointer */
void escape_731(int sz) { int b[sz]; { int c[sz]; g_int = c; } (void)b; }

/* CASE 732: [VLA] VLA of short returned through a local alias pointer */
short *escape_732(int sz) { short b[sz]; short *p = b; return p; }

/* CASE 733: [VLA] VLA of long passed to helper function sink */
void escape_733(int sz) { long b[sz]; sink_long(b); }

/* CASE 734: [VLA] VLA of float stored into function-static pointer */
void escape_734(int sz) { float b[sz]; static float *sp; sp = b; (void)sp; }

/* CASE 735: [VLA] VLA of double address-of-local-pointer returned (pointer-to-pointer) */
double **escape_735(int sz) { double b[sz]; double *p = b; return &p; }

/* CASE 736: [VLA] VLA of unsigned char address-of-local-pointer stored into global void** */
void escape_736(int sz) { unsigned char b[sz]; unsigned char *p = b; g_voidpp = (void **)&p; }

/* CASE 737: [alloca] alloca block of unsigned int returned directly */
unsigned int *escape_737(void) { unsigned int *p = alloca(28); return p; }

/* CASE 738: [alloca] alloca block of struct S stored into global pointer */
void escape_738(void) { struct S *p = alloca(4); g_struct = p; }

/* CASE 739: [alloca] alloca block of char passed to helper function sink */
void escape_739(void) { char *p = alloca(7); sink_char(p); }

/* CASE 740: [alloca] alloca block of int stored into function-static pointer */
void escape_740(void) { int *p = alloca(10); static int *sp; sp = p; (void)sp; }

/* CASE 741: [alloca] alloca block of short aliased in nested scope then stored globally */
void escape_741(void) { short *p = alloca(13); { short *q = p; g_short = q; } }

/* CASE 742: [alloca] alloca block of long returned through pointer-to-pointer */
long **escape_742(void) { long *p = alloca(16); return &p; }

/* CASE 743: [alloca] alloca block of float pointer-to-pointer stored into global void** */
void escape_743(void) { float *p = alloca(19); g_voidpp = (void **)&p; }

/* CASE 744: [fixed] fixed double array returned directly */
double *escape_744(void) { double b[22]; return b; }

/* CASE 745: [fixed] fixed unsigned char array stored into global pointer */
void escape_745(void) { unsigned char b[25]; g_uchar = b; }

/* CASE 746: [fixed] fixed unsigned int array passed to helper function sink */
void escape_746(void) { unsigned int b[28]; sink_uint(b); }

/* CASE 747: [fixed] fixed struct S array stored into function-static pointer */
void escape_747(void) { struct S b[4]; static struct S *sp; sp = b; (void)sp; }

/* CASE 748: [fixed] nested-scope fixed char array returned */
char *escape_748(void) { char b[7]; { char c[7]; return c; } }

/* CASE 749: [fixed] fixed int array address-of-local-pointer returned */
int **escape_749(void) { int b[10]; int *p = b; return &p; }

/* CASE 750: [fixed] address of local short scalar returned */
short *escape_750(void) { short x = (short)0; return &x; }

/* CASE 751: [fixed] address of local long scalar stored into global pointer */
void escape_751(void) { long x = (long)0; g_long = &x; }

/* CASE 752: [compound] address of float compound literal returned */
float *escape_752(void) { return &(float){0}; }

/* CASE 753: [compound] double compound-literal array stored into global pointer */
void escape_753(void) { g_double = (double[22]){0}; }

/* CASE 754: [compound] address of unsigned char compound literal passed to helper sink */
void escape_754(void) { sink_uchar(&(unsigned char){0}); }

/* CASE 755: [VLA] VLA of unsigned int returned directly */
unsigned int *escape_755(int sz) { unsigned int b[sz]; return b; }

/* CASE 756: [VLA] VLA of struct S stored into global pointer */
void escape_756(int sz) { struct S b[sz]; g_struct = b; }

/* CASE 757: [VLA] nested-scope VLA of char stored into global pointer */
void escape_757(int sz) { char b[sz]; { char c[sz]; g_char = c; } (void)b; }

/* CASE 758: [VLA] VLA of int returned through a local alias pointer */
int *escape_758(int sz) { int b[sz]; int *p = b; return p; }

/* CASE 759: [VLA] VLA of short passed to helper function sink */
void escape_759(int sz) { short b[sz]; sink_short(b); }

/* CASE 760: [VLA] VLA of long stored into function-static pointer */
void escape_760(int sz) { long b[sz]; static long *sp; sp = b; (void)sp; }

/* CASE 761: [VLA] VLA of float address-of-local-pointer returned (pointer-to-pointer) */
float **escape_761(int sz) { float b[sz]; float *p = b; return &p; }

/* CASE 762: [VLA] VLA of double address-of-local-pointer stored into global void** */
void escape_762(int sz) { double b[sz]; double *p = b; g_voidpp = (void **)&p; }

/* CASE 763: [alloca] alloca block of unsigned char returned directly */
unsigned char *escape_763(void) { unsigned char *p = alloca(25); return p; }

/* CASE 764: [alloca] alloca block of unsigned int stored into global pointer */
void escape_764(void) { unsigned int *p = alloca(28); g_uint = p; }

/* CASE 765: [alloca] alloca block of struct S passed to helper function sink */
void escape_765(void) { struct S *p = alloca(4); sink_struct(p); }

/* CASE 766: [alloca] alloca block of char stored into function-static pointer */
void escape_766(void) { char *p = alloca(7); static char *sp; sp = p; (void)sp; }

/* CASE 767: [alloca] alloca block of int aliased in nested scope then stored globally */
void escape_767(void) { int *p = alloca(10); { int *q = p; g_int = q; } }

/* CASE 768: [alloca] alloca block of short returned through pointer-to-pointer */
short **escape_768(void) { short *p = alloca(13); return &p; }

/* CASE 769: [alloca] alloca block of long pointer-to-pointer stored into global void** */
void escape_769(void) { long *p = alloca(16); g_voidpp = (void **)&p; }

/* CASE 770: [fixed] fixed float array returned directly */
float *escape_770(void) { float b[19]; return b; }

/* CASE 771: [fixed] fixed double array stored into global pointer */
void escape_771(void) { double b[22]; g_double = b; }

/* CASE 772: [fixed] fixed unsigned char array passed to helper function sink */
void escape_772(void) { unsigned char b[25]; sink_uchar(b); }

/* CASE 773: [fixed] fixed unsigned int array stored into function-static pointer */
void escape_773(void) { unsigned int b[28]; static unsigned int *sp; sp = b; (void)sp; }

/* CASE 774: [fixed] nested-scope fixed struct S array returned */
struct S *escape_774(void) { struct S b[4]; { struct S c[4]; return c; } }

/* CASE 775: [fixed] fixed char array address-of-local-pointer returned */
char **escape_775(void) { char b[7]; char *p = b; return &p; }

/* CASE 776: [fixed] address of local int scalar returned */
int *escape_776(void) { int x = (int)0; return &x; }

/* CASE 777: [fixed] address of local short scalar stored into global pointer */
void escape_777(void) { short x = (short)0; g_short = &x; }

/* CASE 778: [compound] address of long compound literal returned */
long *escape_778(void) { return &(long){0}; }

/* CASE 779: [compound] float compound-literal array stored into global pointer */
void escape_779(void) { g_float = (float[19]){0}; }

/* CASE 780: [compound] address of double compound literal passed to helper sink */
void escape_780(void) { sink_double(&(double){0}); }

/* CASE 781: [VLA] VLA of unsigned char returned directly */
unsigned char *escape_781(int sz) { unsigned char b[sz]; return b; }

/* CASE 782: [VLA] VLA of unsigned int stored into global pointer */
void escape_782(int sz) { unsigned int b[sz]; g_uint = b; }

/* CASE 783: [VLA] nested-scope VLA of struct S stored into global pointer */
void escape_783(int sz) { struct S b[sz]; { struct S c[sz]; g_struct = c; } (void)b; }

/* CASE 784: [VLA] VLA of char returned through a local alias pointer */
char *escape_784(int sz) { char b[sz]; char *p = b; return p; }

/* CASE 785: [VLA] VLA of int passed to helper function sink */
void escape_785(int sz) { int b[sz]; sink_int(b); }

/* CASE 786: [VLA] VLA of short stored into function-static pointer */
void escape_786(int sz) { short b[sz]; static short *sp; sp = b; (void)sp; }

/* CASE 787: [VLA] VLA of long address-of-local-pointer returned (pointer-to-pointer) */
long **escape_787(int sz) { long b[sz]; long *p = b; return &p; }

/* CASE 788: [VLA] VLA of float address-of-local-pointer stored into global void** */
void escape_788(int sz) { float b[sz]; float *p = b; g_voidpp = (void **)&p; }

/* CASE 789: [alloca] alloca block of double returned directly */
double *escape_789(void) { double *p = alloca(22); return p; }

/* CASE 790: [alloca] alloca block of unsigned char stored into global pointer */
void escape_790(void) { unsigned char *p = alloca(25); g_uchar = p; }

/* CASE 791: [alloca] alloca block of unsigned int passed to helper function sink */
void escape_791(void) { unsigned int *p = alloca(28); sink_uint(p); }

/* CASE 792: [alloca] alloca block of struct S stored into function-static pointer */
void escape_792(void) { struct S *p = alloca(4); static struct S *sp; sp = p; (void)sp; }

/* CASE 793: [alloca] alloca block of char aliased in nested scope then stored globally */
void escape_793(void) { char *p = alloca(7); { char *q = p; g_char = q; } }

/* CASE 794: [alloca] alloca block of int returned through pointer-to-pointer */
int **escape_794(void) { int *p = alloca(10); return &p; }

/* CASE 795: [alloca] alloca block of short pointer-to-pointer stored into global void** */
void escape_795(void) { short *p = alloca(13); g_voidpp = (void **)&p; }

/* CASE 796: [fixed] fixed long array returned directly */
long *escape_796(void) { long b[16]; return b; }

/* CASE 797: [fixed] fixed float array stored into global pointer */
void escape_797(void) { float b[19]; g_float = b; }

/* CASE 798: [fixed] fixed double array passed to helper function sink */
void escape_798(void) { double b[22]; sink_double(b); }

/* CASE 799: [fixed] fixed unsigned char array stored into function-static pointer */
void escape_799(void) { unsigned char b[25]; static unsigned char *sp; sp = b; (void)sp; }

/* CASE 800: [fixed] nested-scope fixed unsigned int array returned */
unsigned int *escape_800(void) { unsigned int b[28]; { unsigned int c[28]; return c; } }

/* CASE 801: [fixed] fixed struct S array address-of-local-pointer returned */
struct S **escape_801(void) { struct S b[4]; struct S *p = b; return &p; }

/* CASE 802: [fixed] address of local char scalar returned */
char *escape_802(void) { char x = (char)0; return &x; }

/* CASE 803: [fixed] address of local int scalar stored into global pointer */
void escape_803(void) { int x = (int)0; g_int = &x; }

/* CASE 804: [compound] address of short compound literal returned */
short *escape_804(void) { return &(short){0}; }

/* CASE 805: [compound] long compound-literal array stored into global pointer */
void escape_805(void) { g_long = (long[16]){0}; }

/* CASE 806: [compound] address of float compound literal passed to helper sink */
void escape_806(void) { sink_float(&(float){0}); }

/* CASE 807: [VLA] VLA of double returned directly */
double *escape_807(int sz) { double b[sz]; return b; }

/* CASE 808: [VLA] VLA of unsigned char stored into global pointer */
void escape_808(int sz) { unsigned char b[sz]; g_uchar = b; }

/* CASE 809: [VLA] nested-scope VLA of unsigned int stored into global pointer */
void escape_809(int sz) { unsigned int b[sz]; { unsigned int c[sz]; g_uint = c; } (void)b; }

/* CASE 810: [VLA] VLA of struct S returned through a local alias pointer */
struct S *escape_810(int sz) { struct S b[sz]; struct S *p = b; return p; }

/* CASE 811: [VLA] VLA of char passed to helper function sink */
void escape_811(int sz) { char b[sz]; sink_char(b); }

/* CASE 812: [VLA] VLA of int stored into function-static pointer */
void escape_812(int sz) { int b[sz]; static int *sp; sp = b; (void)sp; }

/* CASE 813: [VLA] VLA of short address-of-local-pointer returned (pointer-to-pointer) */
short **escape_813(int sz) { short b[sz]; short *p = b; return &p; }

/* CASE 814: [VLA] VLA of long address-of-local-pointer stored into global void** */
void escape_814(int sz) { long b[sz]; long *p = b; g_voidpp = (void **)&p; }

/* CASE 815: [alloca] alloca block of float returned directly */
float *escape_815(void) { float *p = alloca(19); return p; }

/* CASE 816: [alloca] alloca block of double stored into global pointer */
void escape_816(void) { double *p = alloca(22); g_double = p; }

/* CASE 817: [alloca] alloca block of unsigned char passed to helper function sink */
void escape_817(void) { unsigned char *p = alloca(25); sink_uchar(p); }

/* CASE 818: [alloca] alloca block of unsigned int stored into function-static pointer */
void escape_818(void) { unsigned int *p = alloca(28); static unsigned int *sp; sp = p; (void)sp; }

/* CASE 819: [alloca] alloca block of struct S aliased in nested scope then stored globally */
void escape_819(void) { struct S *p = alloca(4); { struct S *q = p; g_struct = q; } }

/* CASE 820: [alloca] alloca block of char returned through pointer-to-pointer */
char **escape_820(void) { char *p = alloca(7); return &p; }

/* CASE 821: [alloca] alloca block of int pointer-to-pointer stored into global void** */
void escape_821(void) { int *p = alloca(10); g_voidpp = (void **)&p; }

/* CASE 822: [fixed] fixed short array returned directly */
short *escape_822(void) { short b[13]; return b; }

/* CASE 823: [fixed] fixed long array stored into global pointer */
void escape_823(void) { long b[16]; g_long = b; }

/* CASE 824: [fixed] fixed float array passed to helper function sink */
void escape_824(void) { float b[19]; sink_float(b); }

/* CASE 825: [fixed] fixed double array stored into function-static pointer */
void escape_825(void) { double b[22]; static double *sp; sp = b; (void)sp; }

/* CASE 826: [fixed] nested-scope fixed unsigned char array returned */
unsigned char *escape_826(void) { unsigned char b[25]; { unsigned char c[25]; return c; } }

/* CASE 827: [fixed] fixed unsigned int array address-of-local-pointer returned */
unsigned int **escape_827(void) { unsigned int b[28]; unsigned int *p = b; return &p; }

/* CASE 828: [fixed] address of local struct S scalar returned */
struct S *escape_828(void) { struct S x = {0}; return &x; }

/* CASE 829: [fixed] address of local char scalar stored into global pointer */
void escape_829(void) { char x = (char)0; g_char = &x; }

/* CASE 830: [compound] address of int compound literal returned */
int *escape_830(void) { return &(int){0}; }

/* CASE 831: [compound] short compound-literal array stored into global pointer */
void escape_831(void) { g_short = (short[13]){0}; }

/* CASE 832: [compound] address of long compound literal passed to helper sink */
void escape_832(void) { sink_long(&(long){0}); }

/* CASE 833: [VLA] VLA of float returned directly */
float *escape_833(int sz) { float b[sz]; return b; }

/* CASE 834: [VLA] VLA of double stored into global pointer */
void escape_834(int sz) { double b[sz]; g_double = b; }

/* CASE 835: [VLA] nested-scope VLA of unsigned char stored into global pointer */
void escape_835(int sz) { unsigned char b[sz]; { unsigned char c[sz]; g_uchar = c; } (void)b; }

/* CASE 836: [VLA] VLA of unsigned int returned through a local alias pointer */
unsigned int *escape_836(int sz) { unsigned int b[sz]; unsigned int *p = b; return p; }

/* CASE 837: [VLA] VLA of struct S passed to helper function sink */
void escape_837(int sz) { struct S b[sz]; sink_struct(b); }

/* CASE 838: [VLA] VLA of char stored into function-static pointer */
void escape_838(int sz) { char b[sz]; static char *sp; sp = b; (void)sp; }

/* CASE 839: [VLA] VLA of int address-of-local-pointer returned (pointer-to-pointer) */
int **escape_839(int sz) { int b[sz]; int *p = b; return &p; }

/* CASE 840: [VLA] VLA of short address-of-local-pointer stored into global void** */
void escape_840(int sz) { short b[sz]; short *p = b; g_voidpp = (void **)&p; }

/* CASE 841: [alloca] alloca block of long returned directly */
long *escape_841(void) { long *p = alloca(16); return p; }

/* CASE 842: [alloca] alloca block of float stored into global pointer */
void escape_842(void) { float *p = alloca(19); g_float = p; }

/* CASE 843: [alloca] alloca block of double passed to helper function sink */
void escape_843(void) { double *p = alloca(22); sink_double(p); }

/* CASE 844: [alloca] alloca block of unsigned char stored into function-static pointer */
void escape_844(void) { unsigned char *p = alloca(25); static unsigned char *sp; sp = p; (void)sp; }

/* CASE 845: [alloca] alloca block of unsigned int aliased in nested scope then stored globally */
void escape_845(void) { unsigned int *p = alloca(28); { unsigned int *q = p; g_uint = q; } }

/* CASE 846: [alloca] alloca block of struct S returned through pointer-to-pointer */
struct S **escape_846(void) { struct S *p = alloca(4); return &p; }

/* CASE 847: [alloca] alloca block of char pointer-to-pointer stored into global void** */
void escape_847(void) { char *p = alloca(7); g_voidpp = (void **)&p; }

/* CASE 848: [fixed] fixed int array returned directly */
int *escape_848(void) { int b[10]; return b; }

/* CASE 849: [fixed] fixed short array stored into global pointer */
void escape_849(void) { short b[13]; g_short = b; }

/* CASE 850: [fixed] fixed long array passed to helper function sink */
void escape_850(void) { long b[16]; sink_long(b); }

/* CASE 851: [fixed] fixed float array stored into function-static pointer */
void escape_851(void) { float b[19]; static float *sp; sp = b; (void)sp; }

/* CASE 852: [fixed] nested-scope fixed double array returned */
double *escape_852(void) { double b[22]; { double c[22]; return c; } }

/* CASE 853: [fixed] fixed unsigned char array address-of-local-pointer returned */
unsigned char **escape_853(void) { unsigned char b[25]; unsigned char *p = b; return &p; }

/* CASE 854: [fixed] address of local unsigned int scalar returned */
unsigned int *escape_854(void) { unsigned int x = (unsigned int)0; return &x; }

/* CASE 855: [fixed] address of local struct S scalar stored into global pointer */
void escape_855(void) { struct S x = {0}; g_struct = &x; }

/* CASE 856: [compound] address of char compound literal returned */
char *escape_856(void) { return &(char){0}; }

/* CASE 857: [compound] int compound-literal array stored into global pointer */
void escape_857(void) { g_int = (int[10]){0}; }

/* CASE 858: [compound] address of short compound literal passed to helper sink */
void escape_858(void) { sink_short(&(short){0}); }

/* CASE 859: [VLA] VLA of long returned directly */
long *escape_859(int sz) { long b[sz]; return b; }

/* CASE 860: [VLA] VLA of float stored into global pointer */
void escape_860(int sz) { float b[sz]; g_float = b; }

/* CASE 861: [VLA] nested-scope VLA of double stored into global pointer */
void escape_861(int sz) { double b[sz]; { double c[sz]; g_double = c; } (void)b; }

/* CASE 862: [VLA] VLA of unsigned char returned through a local alias pointer */
unsigned char *escape_862(int sz) { unsigned char b[sz]; unsigned char *p = b; return p; }

/* CASE 863: [VLA] VLA of unsigned int passed to helper function sink */
void escape_863(int sz) { unsigned int b[sz]; sink_uint(b); }

/* CASE 864: [VLA] VLA of struct S stored into function-static pointer */
void escape_864(int sz) { struct S b[sz]; static struct S *sp; sp = b; (void)sp; }

/* CASE 865: [VLA] VLA of char address-of-local-pointer returned (pointer-to-pointer) */
char **escape_865(int sz) { char b[sz]; char *p = b; return &p; }

/* CASE 866: [VLA] VLA of int address-of-local-pointer stored into global void** */
void escape_866(int sz) { int b[sz]; int *p = b; g_voidpp = (void **)&p; }

/* CASE 867: [alloca] alloca block of short returned directly */
short *escape_867(void) { short *p = alloca(13); return p; }

/* CASE 868: [alloca] alloca block of long stored into global pointer */
void escape_868(void) { long *p = alloca(16); g_long = p; }

/* CASE 869: [alloca] alloca block of float passed to helper function sink */
void escape_869(void) { float *p = alloca(19); sink_float(p); }

/* CASE 870: [alloca] alloca block of double stored into function-static pointer */
void escape_870(void) { double *p = alloca(22); static double *sp; sp = p; (void)sp; }

/* CASE 871: [alloca] alloca block of unsigned char aliased in nested scope then stored globally */
void escape_871(void) { unsigned char *p = alloca(25); { unsigned char *q = p; g_uchar = q; } }

/* CASE 872: [alloca] alloca block of unsigned int returned through pointer-to-pointer */
unsigned int **escape_872(void) { unsigned int *p = alloca(28); return &p; }

/* CASE 873: [alloca] alloca block of struct S pointer-to-pointer stored into global void** */
void escape_873(void) { struct S *p = alloca(4); g_voidpp = (void **)&p; }

/* CASE 874: [fixed] fixed char array returned directly */
char *escape_874(void) { char b[7]; return b; }

/* CASE 875: [fixed] fixed int array stored into global pointer */
void escape_875(void) { int b[10]; g_int = b; }

/* CASE 876: [fixed] fixed short array passed to helper function sink */
void escape_876(void) { short b[13]; sink_short(b); }

/* CASE 877: [fixed] fixed long array stored into function-static pointer */
void escape_877(void) { long b[16]; static long *sp; sp = b; (void)sp; }

/* CASE 878: [fixed] nested-scope fixed float array returned */
float *escape_878(void) { float b[19]; { float c[19]; return c; } }

/* CASE 879: [fixed] fixed double array address-of-local-pointer returned */
double **escape_879(void) { double b[22]; double *p = b; return &p; }

/* CASE 880: [fixed] address of local unsigned char scalar returned */
unsigned char *escape_880(void) { unsigned char x = (unsigned char)0; return &x; }

/* CASE 881: [fixed] address of local unsigned int scalar stored into global pointer */
void escape_881(void) { unsigned int x = (unsigned int)0; g_uint = &x; }

/* CASE 882: [compound] address of struct S compound literal returned */
struct S *escape_882(void) { return &(struct S){0}; }

/* CASE 883: [compound] char compound-literal array stored into global pointer */
void escape_883(void) { g_char = (char[7]){0}; }

/* CASE 884: [compound] address of int compound literal passed to helper sink */
void escape_884(void) { sink_int(&(int){0}); }

/* CASE 885: [VLA] VLA of short returned directly */
short *escape_885(int sz) { short b[sz]; return b; }

/* CASE 886: [VLA] VLA of long stored into global pointer */
void escape_886(int sz) { long b[sz]; g_long = b; }

/* CASE 887: [VLA] nested-scope VLA of float stored into global pointer */
void escape_887(int sz) { float b[sz]; { float c[sz]; g_float = c; } (void)b; }

/* CASE 888: [VLA] VLA of double returned through a local alias pointer */
double *escape_888(int sz) { double b[sz]; double *p = b; return p; }

/* CASE 889: [VLA] VLA of unsigned char passed to helper function sink */
void escape_889(int sz) { unsigned char b[sz]; sink_uchar(b); }

/* CASE 890: [VLA] VLA of unsigned int stored into function-static pointer */
void escape_890(int sz) { unsigned int b[sz]; static unsigned int *sp; sp = b; (void)sp; }

/* CASE 891: [VLA] VLA of struct S address-of-local-pointer returned (pointer-to-pointer) */
struct S **escape_891(int sz) { struct S b[sz]; struct S *p = b; return &p; }

/* CASE 892: [VLA] VLA of char address-of-local-pointer stored into global void** */
void escape_892(int sz) { char b[sz]; char *p = b; g_voidpp = (void **)&p; }

/* CASE 893: [alloca] alloca block of int returned directly */
int *escape_893(void) { int *p = alloca(10); return p; }

/* CASE 894: [alloca] alloca block of short stored into global pointer */
void escape_894(void) { short *p = alloca(13); g_short = p; }

/* CASE 895: [alloca] alloca block of long passed to helper function sink */
void escape_895(void) { long *p = alloca(16); sink_long(p); }

/* CASE 896: [alloca] alloca block of float stored into function-static pointer */
void escape_896(void) { float *p = alloca(19); static float *sp; sp = p; (void)sp; }

/* CASE 897: [alloca] alloca block of double aliased in nested scope then stored globally */
void escape_897(void) { double *p = alloca(22); { double *q = p; g_double = q; } }

/* CASE 898: [alloca] alloca block of unsigned char returned through pointer-to-pointer */
unsigned char **escape_898(void) { unsigned char *p = alloca(25); return &p; }

/* CASE 899: [alloca] alloca block of unsigned int pointer-to-pointer stored into global void** */
void escape_899(void) { unsigned int *p = alloca(28); g_voidpp = (void **)&p; }

/* CASE 900: [fixed] fixed struct S array returned directly */
struct S *escape_900(void) { struct S b[4]; return b; }

/* CASE 901: [fixed] fixed char array stored into global pointer */
void escape_901(void) { char b[7]; g_char = b; }

/* CASE 902: [fixed] fixed int array passed to helper function sink */
void escape_902(void) { int b[10]; sink_int(b); }

/* CASE 903: [fixed] fixed short array stored into function-static pointer */
void escape_903(void) { short b[13]; static short *sp; sp = b; (void)sp; }

/* CASE 904: [fixed] nested-scope fixed long array returned */
long *escape_904(void) { long b[16]; { long c[16]; return c; } }

/* CASE 905: [fixed] fixed float array address-of-local-pointer returned */
float **escape_905(void) { float b[19]; float *p = b; return &p; }

/* CASE 906: [fixed] address of local double scalar returned */
double *escape_906(void) { double x = (double)0; return &x; }

/* CASE 907: [fixed] address of local unsigned char scalar stored into global pointer */
void escape_907(void) { unsigned char x = (unsigned char)0; g_uchar = &x; }

/* CASE 908: [compound] address of unsigned int compound literal returned */
unsigned int *escape_908(void) { return &(unsigned int){0}; }

/* CASE 909: [compound] struct S compound-literal array stored into global pointer */
void escape_909(void) { g_struct = (struct S[4]){0}; }

/* CASE 910: [compound] address of char compound literal passed to helper sink */
void escape_910(void) { sink_char(&(char){0}); }

/* CASE 911: [VLA] VLA of int returned directly */
int *escape_911(int sz) { int b[sz]; return b; }

/* CASE 912: [VLA] VLA of short stored into global pointer */
void escape_912(int sz) { short b[sz]; g_short = b; }

/* CASE 913: [VLA] nested-scope VLA of long stored into global pointer */
void escape_913(int sz) { long b[sz]; { long c[sz]; g_long = c; } (void)b; }

/* CASE 914: [VLA] VLA of float returned through a local alias pointer */
float *escape_914(int sz) { float b[sz]; float *p = b; return p; }

/* CASE 915: [VLA] VLA of double passed to helper function sink */
void escape_915(int sz) { double b[sz]; sink_double(b); }

/* CASE 916: [VLA] VLA of unsigned char stored into function-static pointer */
void escape_916(int sz) { unsigned char b[sz]; static unsigned char *sp; sp = b; (void)sp; }

/* CASE 917: [VLA] VLA of unsigned int address-of-local-pointer returned (pointer-to-pointer) */
unsigned int **escape_917(int sz) { unsigned int b[sz]; unsigned int *p = b; return &p; }

/* CASE 918: [VLA] VLA of struct S address-of-local-pointer stored into global void** */
void escape_918(int sz) { struct S b[sz]; struct S *p = b; g_voidpp = (void **)&p; }

/* CASE 919: [alloca] alloca block of char returned directly */
char *escape_919(void) { char *p = alloca(7); return p; }

/* CASE 920: [alloca] alloca block of int stored into global pointer */
void escape_920(void) { int *p = alloca(10); g_int = p; }

/* CASE 921: [alloca] alloca block of short passed to helper function sink */
void escape_921(void) { short *p = alloca(13); sink_short(p); }

/* CASE 922: [alloca] alloca block of long stored into function-static pointer */
void escape_922(void) { long *p = alloca(16); static long *sp; sp = p; (void)sp; }

/* CASE 923: [alloca] alloca block of float aliased in nested scope then stored globally */
void escape_923(void) { float *p = alloca(19); { float *q = p; g_float = q; } }

/* CASE 924: [alloca] alloca block of double returned through pointer-to-pointer */
double **escape_924(void) { double *p = alloca(22); return &p; }

/* CASE 925: [alloca] alloca block of unsigned char pointer-to-pointer stored into global void** */
void escape_925(void) { unsigned char *p = alloca(25); g_voidpp = (void **)&p; }

/* CASE 926: [fixed] fixed unsigned int array returned directly */
unsigned int *escape_926(void) { unsigned int b[28]; return b; }

/* CASE 927: [fixed] fixed struct S array stored into global pointer */
void escape_927(void) { struct S b[4]; g_struct = b; }

/* CASE 928: [fixed] fixed char array passed to helper function sink */
void escape_928(void) { char b[7]; sink_char(b); }

/* CASE 929: [fixed] fixed int array stored into function-static pointer */
void escape_929(void) { int b[10]; static int *sp; sp = b; (void)sp; }

/* CASE 930: [fixed] nested-scope fixed short array returned */
short *escape_930(void) { short b[13]; { short c[13]; return c; } }

/* CASE 931: [fixed] fixed long array address-of-local-pointer returned */
long **escape_931(void) { long b[16]; long *p = b; return &p; }

/* CASE 932: [fixed] address of local float scalar returned */
float *escape_932(void) { float x = (float)0; return &x; }

/* CASE 933: [fixed] address of local double scalar stored into global pointer */
void escape_933(void) { double x = (double)0; g_double = &x; }

/* CASE 934: [compound] address of unsigned char compound literal returned */
unsigned char *escape_934(void) { return &(unsigned char){0}; }

/* CASE 935: [compound] unsigned int compound-literal array stored into global pointer */
void escape_935(void) { g_uint = (unsigned int[28]){0}; }

/* CASE 936: [compound] address of struct S compound literal passed to helper sink */
void escape_936(void) { sink_struct(&(struct S){0}); }

/* CASE 937: [VLA] VLA of char returned directly */
char *escape_937(int sz) { char b[sz]; return b; }

/* CASE 938: [VLA] VLA of int stored into global pointer */
void escape_938(int sz) { int b[sz]; g_int = b; }

/* CASE 939: [VLA] nested-scope VLA of short stored into global pointer */
void escape_939(int sz) { short b[sz]; { short c[sz]; g_short = c; } (void)b; }

/* CASE 940: [VLA] VLA of long returned through a local alias pointer */
long *escape_940(int sz) { long b[sz]; long *p = b; return p; }

/* CASE 941: [VLA] VLA of float passed to helper function sink */
void escape_941(int sz) { float b[sz]; sink_float(b); }

/* CASE 942: [VLA] VLA of double stored into function-static pointer */
void escape_942(int sz) { double b[sz]; static double *sp; sp = b; (void)sp; }

/* CASE 943: [VLA] VLA of unsigned char address-of-local-pointer returned (pointer-to-pointer) */
unsigned char **escape_943(int sz) { unsigned char b[sz]; unsigned char *p = b; return &p; }

/* CASE 944: [VLA] VLA of unsigned int address-of-local-pointer stored into global void** */
void escape_944(int sz) { unsigned int b[sz]; unsigned int *p = b; g_voidpp = (void **)&p; }

/* CASE 945: [alloca] alloca block of struct S returned directly */
struct S *escape_945(void) { struct S *p = alloca(4); return p; }

/* CASE 946: [alloca] alloca block of char stored into global pointer */
void escape_946(void) { char *p = alloca(7); g_char = p; }

/* CASE 947: [alloca] alloca block of int passed to helper function sink */
void escape_947(void) { int *p = alloca(10); sink_int(p); }

/* CASE 948: [alloca] alloca block of short stored into function-static pointer */
void escape_948(void) { short *p = alloca(13); static short *sp; sp = p; (void)sp; }

/* CASE 949: [alloca] alloca block of long aliased in nested scope then stored globally */
void escape_949(void) { long *p = alloca(16); { long *q = p; g_long = q; } }

/* CASE 950: [alloca] alloca block of float returned through pointer-to-pointer */
float **escape_950(void) { float *p = alloca(19); return &p; }

/* CASE 951: [alloca] alloca block of double pointer-to-pointer stored into global void** */
void escape_951(void) { double *p = alloca(22); g_voidpp = (void **)&p; }

/* CASE 952: [fixed] fixed unsigned char array returned directly */
unsigned char *escape_952(void) { unsigned char b[25]; return b; }

/* CASE 953: [fixed] fixed unsigned int array stored into global pointer */
void escape_953(void) { unsigned int b[28]; g_uint = b; }

/* CASE 954: [fixed] fixed struct S array passed to helper function sink */
void escape_954(void) { struct S b[4]; sink_struct(b); }

/* CASE 955: [fixed] fixed char array stored into function-static pointer */
void escape_955(void) { char b[7]; static char *sp; sp = b; (void)sp; }

/* CASE 956: [fixed] nested-scope fixed int array returned */
int *escape_956(void) { int b[10]; { int c[10]; return c; } }

/* CASE 957: [fixed] fixed short array address-of-local-pointer returned */
short **escape_957(void) { short b[13]; short *p = b; return &p; }

/* CASE 958: [fixed] address of local long scalar returned */
long *escape_958(void) { long x = (long)0; return &x; }

/* CASE 959: [fixed] address of local float scalar stored into global pointer */
void escape_959(void) { float x = (float)0; g_float = &x; }

/* CASE 960: [compound] address of double compound literal returned */
double *escape_960(void) { return &(double){0}; }

/* CASE 961: [compound] unsigned char compound-literal array stored into global pointer */
void escape_961(void) { g_uchar = (unsigned char[25]){0}; }

/* CASE 962: [compound] address of unsigned int compound literal passed to helper sink */
void escape_962(void) { sink_uint(&(unsigned int){0}); }

/* CASE 963: [VLA] VLA of struct S returned directly */
struct S *escape_963(int sz) { struct S b[sz]; return b; }

/* CASE 964: [VLA] VLA of char stored into global pointer */
void escape_964(int sz) { char b[sz]; g_char = b; }

/* CASE 965: [VLA] nested-scope VLA of int stored into global pointer */
void escape_965(int sz) { int b[sz]; { int c[sz]; g_int = c; } (void)b; }

/* CASE 966: [VLA] VLA of short returned through a local alias pointer */
short *escape_966(int sz) { short b[sz]; short *p = b; return p; }

/* CASE 967: [VLA] VLA of long passed to helper function sink */
void escape_967(int sz) { long b[sz]; sink_long(b); }

/* CASE 968: [VLA] VLA of float stored into function-static pointer */
void escape_968(int sz) { float b[sz]; static float *sp; sp = b; (void)sp; }

/* CASE 969: [VLA] VLA of double address-of-local-pointer returned (pointer-to-pointer) */
double **escape_969(int sz) { double b[sz]; double *p = b; return &p; }

/* CASE 970: [VLA] VLA of unsigned char address-of-local-pointer stored into global void** */
void escape_970(int sz) { unsigned char b[sz]; unsigned char *p = b; g_voidpp = (void **)&p; }

/* CASE 971: [alloca] alloca block of unsigned int returned directly */
unsigned int *escape_971(void) { unsigned int *p = alloca(28); return p; }

/* CASE 972: [alloca] alloca block of struct S stored into global pointer */
void escape_972(void) { struct S *p = alloca(4); g_struct = p; }

/* CASE 973: [alloca] alloca block of char passed to helper function sink */
void escape_973(void) { char *p = alloca(7); sink_char(p); }

/* CASE 974: [alloca] alloca block of int stored into function-static pointer */
void escape_974(void) { int *p = alloca(10); static int *sp; sp = p; (void)sp; }

/* CASE 975: [alloca] alloca block of short aliased in nested scope then stored globally */
void escape_975(void) { short *p = alloca(13); { short *q = p; g_short = q; } }

/* CASE 976: [alloca] alloca block of long returned through pointer-to-pointer */
long **escape_976(void) { long *p = alloca(16); return &p; }

/* CASE 977: [alloca] alloca block of float pointer-to-pointer stored into global void** */
void escape_977(void) { float *p = alloca(19); g_voidpp = (void **)&p; }

/* CASE 978: [fixed] fixed double array returned directly */
double *escape_978(void) { double b[22]; return b; }

/* CASE 979: [fixed] fixed unsigned char array stored into global pointer */
void escape_979(void) { unsigned char b[25]; g_uchar = b; }

/* CASE 980: [fixed] fixed unsigned int array passed to helper function sink */
void escape_980(void) { unsigned int b[28]; sink_uint(b); }

/* CASE 981: [fixed] fixed struct S array stored into function-static pointer */
void escape_981(void) { struct S b[4]; static struct S *sp; sp = b; (void)sp; }

/* CASE 982: [fixed] nested-scope fixed char array returned */
char *escape_982(void) { char b[7]; { char c[7]; return c; } }

/* CASE 983: [fixed] fixed int array address-of-local-pointer returned */
int **escape_983(void) { int b[10]; int *p = b; return &p; }

/* CASE 984: [fixed] address of local short scalar returned */
short *escape_984(void) { short x = (short)0; return &x; }

/* CASE 985: [fixed] address of local long scalar stored into global pointer */
void escape_985(void) { long x = (long)0; g_long = &x; }

/* CASE 986: [compound] address of float compound literal returned */
float *escape_986(void) { return &(float){0}; }

/* CASE 987: [compound] double compound-literal array stored into global pointer */
void escape_987(void) { g_double = (double[22]){0}; }

/* CASE 988: [compound] address of unsigned char compound literal passed to helper sink */
void escape_988(void) { sink_uchar(&(unsigned char){0}); }

/* CASE 989: [VLA] VLA of unsigned int returned directly */
unsigned int *escape_989(int sz) { unsigned int b[sz]; return b; }

/* CASE 990: [VLA] VLA of struct S stored into global pointer */
void escape_990(int sz) { struct S b[sz]; g_struct = b; }

/* CASE 991: [VLA] nested-scope VLA of char stored into global pointer */
void escape_991(int sz) { char b[sz]; { char c[sz]; g_char = c; } (void)b; }

/* CASE 992: [VLA] VLA of int returned through a local alias pointer */
int *escape_992(int sz) { int b[sz]; int *p = b; return p; }

/* CASE 993: [VLA] VLA of short passed to helper function sink */
void escape_993(int sz) { short b[sz]; sink_short(b); }

/* CASE 994: [VLA] VLA of long stored into function-static pointer */
void escape_994(int sz) { long b[sz]; static long *sp; sp = b; (void)sp; }

/* CASE 995: [VLA] VLA of float address-of-local-pointer returned (pointer-to-pointer) */
float **escape_995(int sz) { float b[sz]; float *p = b; return &p; }

/* CASE 996: [VLA] VLA of double address-of-local-pointer stored into global void** */
void escape_996(int sz) { double b[sz]; double *p = b; g_voidpp = (void **)&p; }

/* CASE 997: [alloca] alloca block of unsigned char returned directly */
unsigned char *escape_997(void) { unsigned char *p = alloca(25); return p; }

/* CASE 998: [alloca] alloca block of unsigned int stored into global pointer */
void escape_998(void) { unsigned int *p = alloca(28); g_uint = p; }

/* CASE 999: [alloca] alloca block of struct S passed to helper function sink */
void escape_999(void) { struct S *p = alloca(4); sink_struct(p); }

/* CASE 1000: [alloca] alloca block of char stored into function-static pointer */
void escape_1000(void) { char *p = alloca(7); static char *sp; sp = p; (void)sp; }

/* CASE 1001: [alloca] alloca block of int aliased in nested scope then stored globally */
void escape_1001(void) { int *p = alloca(10); { int *q = p; g_int = q; } }

/* CASE 1002: [alloca] alloca block of short returned through pointer-to-pointer */
short **escape_1002(void) { short *p = alloca(13); return &p; }

/* CASE 1003: [alloca] alloca block of long pointer-to-pointer stored into global void** */
void escape_1003(void) { long *p = alloca(16); g_voidpp = (void **)&p; }

/* CASE 1004: [fixed] fixed float array returned directly */
float *escape_1004(void) { float b[19]; return b; }

/* CASE 1005: [fixed] fixed double array stored into global pointer */
void escape_1005(void) { double b[22]; g_double = b; }

/* CASE 1006: [fixed] fixed unsigned char array passed to helper function sink */
void escape_1006(void) { unsigned char b[25]; sink_uchar(b); }

/* CASE 1007: [fixed] fixed unsigned int array stored into function-static pointer */
void escape_1007(void) { unsigned int b[28]; static unsigned int *sp; sp = b; (void)sp; }

/* CASE 1008: [fixed] nested-scope fixed struct S array returned */
struct S *escape_1008(void) { struct S b[4]; { struct S c[4]; return c; } }

/* CASE 1009: [fixed] fixed char array address-of-local-pointer returned */
char **escape_1009(void) { char b[7]; char *p = b; return &p; }

/* CASE 1010: [fixed] address of local int scalar returned */
int *escape_1010(void) { int x = (int)0; return &x; }

/* CASE 1011: [fixed] address of local short scalar stored into global pointer */
void escape_1011(void) { short x = (short)0; g_short = &x; }

/* CASE 1012: [compound] address of long compound literal returned */
long *escape_1012(void) { return &(long){0}; }

/* CASE 1013: [compound] float compound-literal array stored into global pointer */
void escape_1013(void) { g_float = (float[19]){0}; }

/* CASE 1014: [compound] address of double compound literal passed to helper sink */
void escape_1014(void) { sink_double(&(double){0}); }

/* CASE 1015: [VLA] VLA of unsigned char returned directly */
unsigned char *escape_1015(int sz) { unsigned char b[sz]; return b; }

/* CASE 1016: [VLA] VLA of unsigned int stored into global pointer */
void escape_1016(int sz) { unsigned int b[sz]; g_uint = b; }

/* CASE 1017: [VLA] nested-scope VLA of struct S stored into global pointer */
void escape_1017(int sz) { struct S b[sz]; { struct S c[sz]; g_struct = c; } (void)b; }

/* CASE 1018: [VLA] VLA of char returned through a local alias pointer */
char *escape_1018(int sz) { char b[sz]; char *p = b; return p; }

/* CASE 1019: [VLA] VLA of int passed to helper function sink */
void escape_1019(int sz) { int b[sz]; sink_int(b); }

/* CASE 1020: [VLA] VLA of short stored into function-static pointer */
void escape_1020(int sz) { short b[sz]; static short *sp; sp = b; (void)sp; }

/* CASE 1021: [VLA] VLA of long address-of-local-pointer returned (pointer-to-pointer) */
long **escape_1021(int sz) { long b[sz]; long *p = b; return &p; }

/* CASE 1022: [VLA] VLA of float address-of-local-pointer stored into global void** */
void escape_1022(int sz) { float b[sz]; float *p = b; g_voidpp = (void **)&p; }

/* CASE 1023: [alloca] alloca block of double returned directly */
double *escape_1023(void) { double *p = alloca(22); return p; }

/* CASE 1024: [alloca] alloca block of unsigned char stored into global pointer */
void escape_1024(void) { unsigned char *p = alloca(25); g_uchar = p; }

/* CASE 1025: [alloca] alloca block of unsigned int passed to helper function sink */
void escape_1025(void) { unsigned int *p = alloca(28); sink_uint(p); }

/* CASE 1026: [alloca] alloca block of struct S stored into function-static pointer */
void escape_1026(void) { struct S *p = alloca(4); static struct S *sp; sp = p; (void)sp; }

/* CASE 1027: [alloca] alloca block of char aliased in nested scope then stored globally */
void escape_1027(void) { char *p = alloca(7); { char *q = p; g_char = q; } }

/* CASE 1028: [alloca] alloca block of int returned through pointer-to-pointer */
int **escape_1028(void) { int *p = alloca(10); return &p; }

/* CASE 1029: [alloca] alloca block of short pointer-to-pointer stored into global void** */
void escape_1029(void) { short *p = alloca(13); g_voidpp = (void **)&p; }

/* CASE 1030: [fixed] fixed long array returned directly */
long *escape_1030(void) { long b[16]; return b; }

/* CASE 1031: [fixed] fixed float array stored into global pointer */
void escape_1031(void) { float b[19]; g_float = b; }

/* CASE 1032: [fixed] fixed double array passed to helper function sink */
void escape_1032(void) { double b[22]; sink_double(b); }

/* CASE 1033: [fixed] fixed unsigned char array stored into function-static pointer */
void escape_1033(void) { unsigned char b[25]; static unsigned char *sp; sp = b; (void)sp; }

/* CASE 1034: [fixed] nested-scope fixed unsigned int array returned */
unsigned int *escape_1034(void) { unsigned int b[28]; { unsigned int c[28]; return c; } }

/* CASE 1035: [fixed] fixed struct S array address-of-local-pointer returned */
struct S **escape_1035(void) { struct S b[4]; struct S *p = b; return &p; }

/* CASE 1036: [fixed] address of local char scalar returned */
char *escape_1036(void) { char x = (char)0; return &x; }

/* CASE 1037: [fixed] address of local int scalar stored into global pointer */
void escape_1037(void) { int x = (int)0; g_int = &x; }

/* CASE 1038: [compound] address of short compound literal returned */
short *escape_1038(void) { return &(short){0}; }

/* CASE 1039: [compound] long compound-literal array stored into global pointer */
void escape_1039(void) { g_long = (long[16]){0}; }

/* CASE 1040: [compound] address of float compound literal passed to helper sink */
void escape_1040(void) { sink_float(&(float){0}); }

/* CASE 1041: [VLA] VLA of double returned directly */
double *escape_1041(int sz) { double b[sz]; return b; }

/* CASE 1042: [VLA] VLA of unsigned char stored into global pointer */
void escape_1042(int sz) { unsigned char b[sz]; g_uchar = b; }

/* CASE 1043: [VLA] nested-scope VLA of unsigned int stored into global pointer */
void escape_1043(int sz) { unsigned int b[sz]; { unsigned int c[sz]; g_uint = c; } (void)b; }

/* CASE 1044: [VLA] VLA of struct S returned through a local alias pointer */
struct S *escape_1044(int sz) { struct S b[sz]; struct S *p = b; return p; }

/* CASE 1045: [VLA] VLA of char passed to helper function sink */
void escape_1045(int sz) { char b[sz]; sink_char(b); }

/* CASE 1046: [VLA] VLA of int stored into function-static pointer */
void escape_1046(int sz) { int b[sz]; static int *sp; sp = b; (void)sp; }

/* CASE 1047: [VLA] VLA of short address-of-local-pointer returned (pointer-to-pointer) */
short **escape_1047(int sz) { short b[sz]; short *p = b; return &p; }

/* CASE 1048: [VLA] VLA of long address-of-local-pointer stored into global void** */
void escape_1048(int sz) { long b[sz]; long *p = b; g_voidpp = (void **)&p; }

/* CASE 1049: [alloca] alloca block of float returned directly */
float *escape_1049(void) { float *p = alloca(19); return p; }

/* CASE 1050: [alloca] alloca block of double stored into global pointer */
void escape_1050(void) { double *p = alloca(22); g_double = p; }

/* CASE 1051: [alloca] alloca block of unsigned char passed to helper function sink */
void escape_1051(void) { unsigned char *p = alloca(25); sink_uchar(p); }

/* CASE 1052: [alloca] alloca block of unsigned int stored into function-static pointer */
void escape_1052(void) { unsigned int *p = alloca(28); static unsigned int *sp; sp = p; (void)sp; }

/* CASE 1053: [alloca] alloca block of struct S aliased in nested scope then stored globally */
void escape_1053(void) { struct S *p = alloca(4); { struct S *q = p; g_struct = q; } }

/* CASE 1054: [alloca] alloca block of char returned through pointer-to-pointer */
char **escape_1054(void) { char *p = alloca(7); return &p; }

/* CASE 1055: [alloca] alloca block of int pointer-to-pointer stored into global void** */
void escape_1055(void) { int *p = alloca(10); g_voidpp = (void **)&p; }

/* CASE 1056: [fixed] fixed short array returned directly */
short *escape_1056(void) { short b[13]; return b; }

/* CASE 1057: [fixed] fixed long array stored into global pointer */
void escape_1057(void) { long b[16]; g_long = b; }

/* CASE 1058: [fixed] fixed float array passed to helper function sink */
void escape_1058(void) { float b[19]; sink_float(b); }

/* CASE 1059: [fixed] fixed double array stored into function-static pointer */
void escape_1059(void) { double b[22]; static double *sp; sp = b; (void)sp; }

/* CASE 1060: [fixed] nested-scope fixed unsigned char array returned */
unsigned char *escape_1060(void) { unsigned char b[25]; { unsigned char c[25]; return c; } }

/* CASE 1061: [fixed] fixed unsigned int array address-of-local-pointer returned */
unsigned int **escape_1061(void) { unsigned int b[28]; unsigned int *p = b; return &p; }

/* CASE 1062: [fixed] address of local struct S scalar returned */
struct S *escape_1062(void) { struct S x = {0}; return &x; }

/* CASE 1063: [fixed] address of local char scalar stored into global pointer */
void escape_1063(void) { char x = (char)0; g_char = &x; }

/* CASE 1064: [compound] address of int compound literal returned */
int *escape_1064(void) { return &(int){0}; }

/* CASE 1065: [compound] short compound-literal array stored into global pointer */
void escape_1065(void) { g_short = (short[13]){0}; }

/* CASE 1066: [compound] address of long compound literal passed to helper sink */
void escape_1066(void) { sink_long(&(long){0}); }

/* CASE 1067: [VLA] VLA of float returned directly */
float *escape_1067(int sz) { float b[sz]; return b; }

/* CASE 1068: [VLA] VLA of double stored into global pointer */
void escape_1068(int sz) { double b[sz]; g_double = b; }

/* CASE 1069: [VLA] nested-scope VLA of unsigned char stored into global pointer */
void escape_1069(int sz) { unsigned char b[sz]; { unsigned char c[sz]; g_uchar = c; } (void)b; }

/* CASE 1070: [VLA] VLA of unsigned int returned through a local alias pointer */
unsigned int *escape_1070(int sz) { unsigned int b[sz]; unsigned int *p = b; return p; }

/* CASE 1071: [VLA] VLA of struct S passed to helper function sink */
void escape_1071(int sz) { struct S b[sz]; sink_struct(b); }

/* CASE 1072: [VLA] VLA of char stored into function-static pointer */
void escape_1072(int sz) { char b[sz]; static char *sp; sp = b; (void)sp; }

/* CASE 1073: [VLA] VLA of int address-of-local-pointer returned (pointer-to-pointer) */
int **escape_1073(int sz) { int b[sz]; int *p = b; return &p; }

/* CASE 1074: [VLA] VLA of short address-of-local-pointer stored into global void** */
void escape_1074(int sz) { short b[sz]; short *p = b; g_voidpp = (void **)&p; }

/* CASE 1075: [alloca] alloca block of long returned directly */
long *escape_1075(void) { long *p = alloca(16); return p; }

/* CASE 1076: [alloca] alloca block of float stored into global pointer */
void escape_1076(void) { float *p = alloca(19); g_float = p; }

/* CASE 1077: [alloca] alloca block of double passed to helper function sink */
void escape_1077(void) { double *p = alloca(22); sink_double(p); }

/* CASE 1078: [alloca] alloca block of unsigned char stored into function-static pointer */
void escape_1078(void) { unsigned char *p = alloca(25); static unsigned char *sp; sp = p; (void)sp; }

/* CASE 1079: [alloca] alloca block of unsigned int aliased in nested scope then stored globally */
void escape_1079(void) { unsigned int *p = alloca(28); { unsigned int *q = p; g_uint = q; } }

/* CASE 1080: [alloca] alloca block of struct S returned through pointer-to-pointer */
struct S **escape_1080(void) { struct S *p = alloca(4); return &p; }

/* CASE 1081: [alloca] alloca block of char pointer-to-pointer stored into global void** */
void escape_1081(void) { char *p = alloca(7); g_voidpp = (void **)&p; }

/* CASE 1082: [fixed] fixed int array returned directly */
int *escape_1082(void) { int b[10]; return b; }

/* CASE 1083: [fixed] fixed short array stored into global pointer */
void escape_1083(void) { short b[13]; g_short = b; }

/* CASE 1084: [fixed] fixed long array passed to helper function sink */
void escape_1084(void) { long b[16]; sink_long(b); }

/* CASE 1085: [fixed] fixed float array stored into function-static pointer */
void escape_1085(void) { float b[19]; static float *sp; sp = b; (void)sp; }

/* CASE 1086: [fixed] nested-scope fixed double array returned */
double *escape_1086(void) { double b[22]; { double c[22]; return c; } }

/* CASE 1087: [fixed] fixed unsigned char array address-of-local-pointer returned */
unsigned char **escape_1087(void) { unsigned char b[25]; unsigned char *p = b; return &p; }

/* CASE 1088: [fixed] address of local unsigned int scalar returned */
unsigned int *escape_1088(void) { unsigned int x = (unsigned int)0; return &x; }

/* CASE 1089: [fixed] address of local struct S scalar stored into global pointer */
void escape_1089(void) { struct S x = {0}; g_struct = &x; }

/* CASE 1090: [compound] address of char compound literal returned */
char *escape_1090(void) { return &(char){0}; }

/* CASE 1091: [compound] int compound-literal array stored into global pointer */
void escape_1091(void) { g_int = (int[10]){0}; }

/* CASE 1092: [compound] address of short compound literal passed to helper sink */
void escape_1092(void) { sink_short(&(short){0}); }

/* CASE 1093: [VLA] VLA of long returned directly */
long *escape_1093(int sz) { long b[sz]; return b; }

/* CASE 1094: [VLA] VLA of float stored into global pointer */
void escape_1094(int sz) { float b[sz]; g_float = b; }

/* CASE 1095: [VLA] nested-scope VLA of double stored into global pointer */
void escape_1095(int sz) { double b[sz]; { double c[sz]; g_double = c; } (void)b; }

/* CASE 1096: [VLA] VLA of unsigned char returned through a local alias pointer */
unsigned char *escape_1096(int sz) { unsigned char b[sz]; unsigned char *p = b; return p; }

/* CASE 1097: [VLA] VLA of unsigned int passed to helper function sink */
void escape_1097(int sz) { unsigned int b[sz]; sink_uint(b); }

/* CASE 1098: [VLA] VLA of struct S stored into function-static pointer */
void escape_1098(int sz) { struct S b[sz]; static struct S *sp; sp = b; (void)sp; }

/* CASE 1099: [VLA] VLA of char address-of-local-pointer returned (pointer-to-pointer) */
char **escape_1099(int sz) { char b[sz]; char *p = b; return &p; }

/* CASE 1100: [VLA] VLA of int address-of-local-pointer stored into global void** */
void escape_1100(int sz) { int b[sz]; int *p = b; g_voidpp = (void **)&p; }

/* CASE 1101: [alloca] alloca block of short returned directly */
short *escape_1101(void) { short *p = alloca(13); return p; }

/* CASE 1102: [alloca] alloca block of long stored into global pointer */
void escape_1102(void) { long *p = alloca(16); g_long = p; }

/* CASE 1103: [alloca] alloca block of float passed to helper function sink */
void escape_1103(void) { float *p = alloca(19); sink_float(p); }

/* CASE 1104: [alloca] alloca block of double stored into function-static pointer */
void escape_1104(void) { double *p = alloca(22); static double *sp; sp = p; (void)sp; }

/* CASE 1105: [alloca] alloca block of unsigned char aliased in nested scope then stored globally */
void escape_1105(void) { unsigned char *p = alloca(25); { unsigned char *q = p; g_uchar = q; } }

/* CASE 1106: [alloca] alloca block of unsigned int returned through pointer-to-pointer */
unsigned int **escape_1106(void) { unsigned int *p = alloca(28); return &p; }

/* CASE 1107: [alloca] alloca block of struct S pointer-to-pointer stored into global void** */
void escape_1107(void) { struct S *p = alloca(4); g_voidpp = (void **)&p; }

/* CASE 1108: [fixed] fixed char array returned directly */
char *escape_1108(void) { char b[7]; return b; }

/* CASE 1109: [fixed] fixed int array stored into global pointer */
void escape_1109(void) { int b[10]; g_int = b; }

/* CASE 1110: [fixed] fixed short array passed to helper function sink */
void escape_1110(void) { short b[13]; sink_short(b); }

/* CASE 1111: [fixed] fixed long array stored into function-static pointer */
void escape_1111(void) { long b[16]; static long *sp; sp = b; (void)sp; }

/* CASE 1112: [fixed] nested-scope fixed float array returned */
float *escape_1112(void) { float b[19]; { float c[19]; return c; } }

/* CASE 1113: [fixed] fixed double array address-of-local-pointer returned */
double **escape_1113(void) { double b[22]; double *p = b; return &p; }

/* CASE 1114: [fixed] address of local unsigned char scalar returned */
unsigned char *escape_1114(void) { unsigned char x = (unsigned char)0; return &x; }

/* CASE 1115: [fixed] address of local unsigned int scalar stored into global pointer */
void escape_1115(void) { unsigned int x = (unsigned int)0; g_uint = &x; }

/* CASE 1116: [compound] address of struct S compound literal returned */
struct S *escape_1116(void) { return &(struct S){0}; }

/* CASE 1117: [compound] char compound-literal array stored into global pointer */
void escape_1117(void) { g_char = (char[7]){0}; }

/* CASE 1118: [compound] address of int compound literal passed to helper sink */
void escape_1118(void) { sink_int(&(int){0}); }

/* CASE 1119: [VLA] VLA of short returned directly */
short *escape_1119(int sz) { short b[sz]; return b; }

/* CASE 1120: [VLA] VLA of long stored into global pointer */
void escape_1120(int sz) { long b[sz]; g_long = b; }

/* CASE 1121: [VLA] nested-scope VLA of float stored into global pointer */
void escape_1121(int sz) { float b[sz]; { float c[sz]; g_float = c; } (void)b; }

/* CASE 1122: [VLA] VLA of double returned through a local alias pointer */
double *escape_1122(int sz) { double b[sz]; double *p = b; return p; }

/* CASE 1123: [VLA] VLA of unsigned char passed to helper function sink */
void escape_1123(int sz) { unsigned char b[sz]; sink_uchar(b); }

/* CASE 1124: [VLA] VLA of unsigned int stored into function-static pointer */
void escape_1124(int sz) { unsigned int b[sz]; static unsigned int *sp; sp = b; (void)sp; }

/* CASE 1125: [VLA] VLA of struct S address-of-local-pointer returned (pointer-to-pointer) */
struct S **escape_1125(int sz) { struct S b[sz]; struct S *p = b; return &p; }

/* CASE 1126: [VLA] VLA of char address-of-local-pointer stored into global void** */
void escape_1126(int sz) { char b[sz]; char *p = b; g_voidpp = (void **)&p; }

/* CASE 1127: [alloca] alloca block of int returned directly */
int *escape_1127(void) { int *p = alloca(10); return p; }

/* CASE 1128: [alloca] alloca block of short stored into global pointer */
void escape_1128(void) { short *p = alloca(13); g_short = p; }

/* CASE 1129: [alloca] alloca block of long passed to helper function sink */
void escape_1129(void) { long *p = alloca(16); sink_long(p); }

/* CASE 1130: [alloca] alloca block of float stored into function-static pointer */
void escape_1130(void) { float *p = alloca(19); static float *sp; sp = p; (void)sp; }

/* CASE 1131: [alloca] alloca block of double aliased in nested scope then stored globally */
void escape_1131(void) { double *p = alloca(22); { double *q = p; g_double = q; } }

/* CASE 1132: [alloca] alloca block of unsigned char returned through pointer-to-pointer */
unsigned char **escape_1132(void) { unsigned char *p = alloca(25); return &p; }

/* CASE 1133: [alloca] alloca block of unsigned int pointer-to-pointer stored into global void** */
void escape_1133(void) { unsigned int *p = alloca(28); g_voidpp = (void **)&p; }

/* CASE 1134: [fixed] fixed struct S array returned directly */
struct S *escape_1134(void) { struct S b[4]; return b; }

/* CASE 1135: [fixed] fixed char array stored into global pointer */
void escape_1135(void) { char b[7]; g_char = b; }

/* CASE 1136: [fixed] fixed int array passed to helper function sink */
void escape_1136(void) { int b[10]; sink_int(b); }

/* CASE 1137: [fixed] fixed short array stored into function-static pointer */
void escape_1137(void) { short b[13]; static short *sp; sp = b; (void)sp; }

/* CASE 1138: [fixed] nested-scope fixed long array returned */
long *escape_1138(void) { long b[16]; { long c[16]; return c; } }

/* CASE 1139: [fixed] fixed float array address-of-local-pointer returned */
float **escape_1139(void) { float b[19]; float *p = b; return &p; }

/* CASE 1140: [fixed] address of local double scalar returned */
double *escape_1140(void) { double x = (double)0; return &x; }

/* CASE 1141: [fixed] address of local unsigned char scalar stored into global pointer */
void escape_1141(void) { unsigned char x = (unsigned char)0; g_uchar = &x; }

/* CASE 1142: [compound] address of unsigned int compound literal returned */
unsigned int *escape_1142(void) { return &(unsigned int){0}; }

/* CASE 1143: [compound] struct S compound-literal array stored into global pointer */
void escape_1143(void) { g_struct = (struct S[4]){0}; }

/* CASE 1144: [compound] address of char compound literal passed to helper sink */
void escape_1144(void) { sink_char(&(char){0}); }

/* CASE 1145: [VLA] VLA of int returned directly */
int *escape_1145(int sz) { int b[sz]; return b; }

/* CASE 1146: [VLA] VLA of short stored into global pointer */
void escape_1146(int sz) { short b[sz]; g_short = b; }

/* CASE 1147: [VLA] nested-scope VLA of long stored into global pointer */
void escape_1147(int sz) { long b[sz]; { long c[sz]; g_long = c; } (void)b; }

/* CASE 1148: [VLA] VLA of float returned through a local alias pointer */
float *escape_1148(int sz) { float b[sz]; float *p = b; return p; }

/* CASE 1149: [VLA] VLA of double passed to helper function sink */
void escape_1149(int sz) { double b[sz]; sink_double(b); }

/* CASE 1150: [VLA] VLA of unsigned char stored into function-static pointer */
void escape_1150(int sz) { unsigned char b[sz]; static unsigned char *sp; sp = b; (void)sp; }

/* CASE 1151: [VLA] VLA of unsigned int address-of-local-pointer returned (pointer-to-pointer) */
unsigned int **escape_1151(int sz) { unsigned int b[sz]; unsigned int *p = b; return &p; }

/* CASE 1152: [VLA] VLA of struct S address-of-local-pointer stored into global void** */
void escape_1152(int sz) { struct S b[sz]; struct S *p = b; g_voidpp = (void **)&p; }

/* CASE 1153: [alloca] alloca block of char returned directly */
char *escape_1153(void) { char *p = alloca(7); return p; }

/* CASE 1154: [alloca] alloca block of int stored into global pointer */
void escape_1154(void) { int *p = alloca(10); g_int = p; }

/* CASE 1155: [alloca] alloca block of short passed to helper function sink */
void escape_1155(void) { short *p = alloca(13); sink_short(p); }

/* CASE 1156: [alloca] alloca block of long stored into function-static pointer */
void escape_1156(void) { long *p = alloca(16); static long *sp; sp = p; (void)sp; }

/* CASE 1157: [alloca] alloca block of float aliased in nested scope then stored globally */
void escape_1157(void) { float *p = alloca(19); { float *q = p; g_float = q; } }

/* CASE 1158: [alloca] alloca block of double returned through pointer-to-pointer */
double **escape_1158(void) { double *p = alloca(22); return &p; }

/* CASE 1159: [alloca] alloca block of unsigned char pointer-to-pointer stored into global void** */
void escape_1159(void) { unsigned char *p = alloca(25); g_voidpp = (void **)&p; }

/* CASE 1160: [fixed] fixed unsigned int array returned directly */
unsigned int *escape_1160(void) { unsigned int b[28]; return b; }

/* CASE 1161: [fixed] fixed struct S array stored into global pointer */
void escape_1161(void) { struct S b[4]; g_struct = b; }

/* CASE 1162: [fixed] fixed char array passed to helper function sink */
void escape_1162(void) { char b[7]; sink_char(b); }

/* CASE 1163: [fixed] fixed int array stored into function-static pointer */
void escape_1163(void) { int b[10]; static int *sp; sp = b; (void)sp; }

/* CASE 1164: [fixed] nested-scope fixed short array returned */
short *escape_1164(void) { short b[13]; { short c[13]; return c; } }

/* CASE 1165: [fixed] fixed long array address-of-local-pointer returned */
long **escape_1165(void) { long b[16]; long *p = b; return &p; }

/* CASE 1166: [fixed] address of local float scalar returned */
float *escape_1166(void) { float x = (float)0; return &x; }

/* CASE 1167: [fixed] address of local double scalar stored into global pointer */
void escape_1167(void) { double x = (double)0; g_double = &x; }

/* CASE 1168: [compound] address of unsigned char compound literal returned */
unsigned char *escape_1168(void) { return &(unsigned char){0}; }

/* CASE 1169: [compound] unsigned int compound-literal array stored into global pointer */
void escape_1169(void) { g_uint = (unsigned int[28]){0}; }

/* CASE 1170: [compound] address of struct S compound literal passed to helper sink */
void escape_1170(void) { sink_struct(&(struct S){0}); }

/* CASE 1171: [VLA] VLA of char returned directly */
char *escape_1171(int sz) { char b[sz]; return b; }

/* CASE 1172: [VLA] VLA of int stored into global pointer */
void escape_1172(int sz) { int b[sz]; g_int = b; }

/* CASE 1173: [VLA] nested-scope VLA of short stored into global pointer */
void escape_1173(int sz) { short b[sz]; { short c[sz]; g_short = c; } (void)b; }

/* CASE 1174: [VLA] VLA of long returned through a local alias pointer */
long *escape_1174(int sz) { long b[sz]; long *p = b; return p; }

/* CASE 1175: [VLA] VLA of float passed to helper function sink */
void escape_1175(int sz) { float b[sz]; sink_float(b); }

/* CASE 1176: [VLA] VLA of double stored into function-static pointer */
void escape_1176(int sz) { double b[sz]; static double *sp; sp = b; (void)sp; }

/* CASE 1177: [VLA] VLA of unsigned char address-of-local-pointer returned (pointer-to-pointer) */
unsigned char **escape_1177(int sz) { unsigned char b[sz]; unsigned char *p = b; return &p; }

/* CASE 1178: [VLA] VLA of unsigned int address-of-local-pointer stored into global void** */
void escape_1178(int sz) { unsigned int b[sz]; unsigned int *p = b; g_voidpp = (void **)&p; }

/* CASE 1179: [alloca] alloca block of struct S returned directly */
struct S *escape_1179(void) { struct S *p = alloca(4); return p; }

/* CASE 1180: [alloca] alloca block of char stored into global pointer */
void escape_1180(void) { char *p = alloca(7); g_char = p; }

/* CASE 1181: [alloca] alloca block of int passed to helper function sink */
void escape_1181(void) { int *p = alloca(10); sink_int(p); }

/* CASE 1182: [alloca] alloca block of short stored into function-static pointer */
void escape_1182(void) { short *p = alloca(13); static short *sp; sp = p; (void)sp; }

/* CASE 1183: [alloca] alloca block of long aliased in nested scope then stored globally */
void escape_1183(void) { long *p = alloca(16); { long *q = p; g_long = q; } }

/* CASE 1184: [alloca] alloca block of float returned through pointer-to-pointer */
float **escape_1184(void) { float *p = alloca(19); return &p; }

/* CASE 1185: [alloca] alloca block of double pointer-to-pointer stored into global void** */
void escape_1185(void) { double *p = alloca(22); g_voidpp = (void **)&p; }

/* CASE 1186: [fixed] fixed unsigned char array returned directly */
unsigned char *escape_1186(void) { unsigned char b[25]; return b; }

/* CASE 1187: [fixed] fixed unsigned int array stored into global pointer */
void escape_1187(void) { unsigned int b[28]; g_uint = b; }

/* CASE 1188: [fixed] fixed struct S array passed to helper function sink */
void escape_1188(void) { struct S b[4]; sink_struct(b); }

/* CASE 1189: [fixed] fixed char array stored into function-static pointer */
void escape_1189(void) { char b[7]; static char *sp; sp = b; (void)sp; }

/* CASE 1190: [fixed] nested-scope fixed int array returned */
int *escape_1190(void) { int b[10]; { int c[10]; return c; } }

/* CASE 1191: [fixed] fixed short array address-of-local-pointer returned */
short **escape_1191(void) { short b[13]; short *p = b; return &p; }

/* CASE 1192: [fixed] address of local long scalar returned */
long *escape_1192(void) { long x = (long)0; return &x; }

/* CASE 1193: [fixed] address of local float scalar stored into global pointer */
void escape_1193(void) { float x = (float)0; g_float = &x; }

/* CASE 1194: [compound] address of double compound literal returned */
double *escape_1194(void) { return &(double){0}; }

/* CASE 1195: [compound] unsigned char compound-literal array stored into global pointer */
void escape_1195(void) { g_uchar = (unsigned char[25]){0}; }

/* CASE 1196: [compound] address of unsigned int compound literal passed to helper sink */
void escape_1196(void) { sink_uint(&(unsigned int){0}); }

/* CASE 1197: [VLA] VLA of struct S returned directly */
struct S *escape_1197(int sz) { struct S b[sz]; return b; }

/* CASE 1198: [VLA] VLA of char stored into global pointer */
void escape_1198(int sz) { char b[sz]; g_char = b; }

/* CASE 1199: [VLA] nested-scope VLA of int stored into global pointer */
void escape_1199(int sz) { int b[sz]; { int c[sz]; g_int = c; } (void)b; }

/* CASE 1200: [VLA] VLA of short returned through a local alias pointer */
short *escape_1200(int sz) { short b[sz]; short *p = b; return p; }

/* main is present only so the fixture is a translation unit.  Never run. */
int main(void) { return 0; }
