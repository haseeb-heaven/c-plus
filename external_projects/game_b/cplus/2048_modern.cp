/*
 ============================================================================
 Name        : 2048.c
 Author      : Maurits van der Schee
 Description : Console version of the game "2048" for GNU/Linux
 ============================================================================

 C+ modernization of external_projects/game_b/original/2048.c.
 Provenance / line-level change log / ABI statement / before-after outputs:
   external_projects/game_b/cplus/CONVERSION.md

 The observable CLI contract is UNCHANGED: -h/--help, -v/--version, `test`,
 the four colour-scheme modes, and the invalid-option error all print exactly
 the same bytes and return exactly the same exit status as the original.
 Every exported function keeps its original C name, parameter types and return
 type (see the ABI statement in CONVERSION.md).
 */

#define VERSION "1.0.3"

#define _XOPEN_SOURCE 500 // for: usleep
#include <stdio.h>		  // defines: printf, puts, getchar, putchar
#include <stdlib.h>		  // defines: EXIT_SUCCESS, EXIT_FAILURE
#include <string.h>		  // defines: strcmp
#include <unistd.h>		  // defines: STDIN_FILENO, STDOUT_FILENO, usleep, write
#include <termios.h>	  // defines: termios, TCSANOW, ICANON, ECHO
#include <stdbool.h>	  // defines: true, false
#include <stdint.h>		  // defines: uint8_t, uint32_t
#include <time.h>		  // defines: time
#include <signal.h>		  // defines: signal, SIGINT

/* ---- modern C+ standard library (header-only, no extra link flags) ------ */
#include "cplus/cplus.h"
#include "stdlib/error.hp"	 // recoverable errors: error / error_fail
#include "stdlib/vector.hp"	 // checked growable vector for the empty-cell list
#include "stdlib/string.hp"	 // checked owned string for argv matching

#define SIZE 4

/* ------------------------------------------------------------------------ */
/* Strict-mode helpers (file-static: no exported symbol is added or changed) */
/* ------------------------------------------------------------------------ */

/* Read one byte from an untrusted FFI pointer without subscripting it.
 * Strict mode rejects a direct argv[i] subscript; a sized builtin copy makes
 * the extent explicit (same idiom used by the repository's firewall demo). */
static char gb_byte(const char *p, size_t i) {
	char c;
	__builtin_memcpy(&c, p + i, sizeof(c));
	return c;
}

/* Fetch an argv slot as a plain pointer via a sized builtin copy. */
static char *gb_arg(char **p, size_t idx) {
	char *value;
	__builtin_memcpy(&value, p + idx, sizeof(value));
	return value;
}

/* Echo an FFI C string byte-for-byte.  Used only to reproduce the original
 * printf("...%s...", argv[1], argv[0]) INVALID-OPTION line; emitting one byte
 * at a time keeps the output identical for arguments of any length. */
static void gb_echo(const char *s) {
	size_t i = 0;
	if (s == (const char *)0)
		return;
	for (;;) {
		char c = gb_byte(s, i);
		if (c == '\0')
			break;
		putchar(c);
		i++;
	}
}

/* Build the command-line token as an owned C+ string.  Allocation failure is
 * recorded in the checked `error` record; it is never printed (the CLI's
 * observable output must stay identical). */
static void gb_make_arg(const char *src, string *out, error *err) {
	*out = string(src);
	if (!out->valid())
		err->set(CPLUS_EXIT_RESOURCE, "2048: argv copy failed");
}

/* Exact 2^exponent as a uint32_t, computed by repeated unsigned doubling.
 * Strict mode only accepts a shift whose count it can prove is within the
 * operand width; a loop-bounded helper (the same idiom as the repository's
 * calculator/numconv examples) gives that proof.  For every reachable board
 * exponent (0..31) this is bit-identical to the original `1 << exponent`. */
static uint32_t gb_pow2(uint8_t exponent) {
	uint32_t r = 1u;
	uint8_t i;
	for (i = 0; i < exponent; i++)
		r *= 2u;
	return r;
}

/* Async-signal-safe literal writes: the strict checker rejects printf() from
 * a signal handler.  These byte sequences are exactly the ones the original
 * handler printed with printf(). */
static const char gb_terminated_msg[] = "         TERMINATED         \n";
static const char gb_cursor_msg[] = "\033[?25h\033[m";

// this function receives 2 pointers (indicated by *) so it can set their values
void getColors(uint8_t value, uint8_t scheme, uint8_t *foreground, uint8_t *background)
{
	uint8_t original[] = {8, 255, 1, 255, 2, 255, 3, 0, 4, 255, 5, 255, 6, 255, 7, 0, 9, 0, 10, 0, 11, 0, 12, 0, 13, 0, 14, 0, 255, 0, 255, 0};
	uint8_t blackwhite[] = {232, 255, 234, 255, 236, 255, 238, 255, 240, 255, 242, 255, 244, 255, 246, 0, 248, 0, 249, 0, 250, 0, 251, 0, 252, 0, 253, 0, 254, 0, 255, 0};
	uint8_t bluered[] = {235, 255, 63, 255, 57, 255, 93, 255, 129, 255, 165, 255, 201, 255, 200, 255, 199, 255, 198, 255, 197, 255, 196, 255, 196, 255, 196, 255, 196, 255, 196, 255};
	uint8_t whiteblack[] = {255, 0, 254, 0, 253, 0, 252, 0, 251, 0, 250, 0, 249, 0, 248, 0, 246, 0, 244, 255, 242, 255, 240, 255, 238, 255, 236, 255, 234, 255, 232, 255};
	uint8_t *schemes[] = {original, blackwhite, bluered, whiteblack};
	// modify the 'pointed to' variables (using a * on the left hand of the assignment)
	*foreground = *(schemes[scheme] + (1 + value * 2) % sizeof(original));
	*background = *(schemes[scheme] + (0 + value * 2) % sizeof(original));
	// alternatively we could have returned a struct with two variables
}

uint8_t getDigitCount(uint32_t number)
{
	uint8_t count = 0;
	do
	{
		number /= 10;
		count += 1;
	} while (number);
	return count;
}

void drawBoard(uint8_t board[SIZE][SIZE], uint8_t scheme, uint32_t score)
{
	/* MODERN: fg/bg are zero-initialised.  The original relied on getColors()
	 * writing them before the first read; strict mode requires proof of
	 * initialisation.  getColors() still overwrites them on every iteration,
	 * so printed colours are byte-identical. */
	uint8_t x, y, fg = 0, bg = 0;
	printf("\033[H"); // move cursor to 0,0
	printf("2048.c %17u pts\n\n", score);
	for (y = 0; y < SIZE; y++)
	{
		for (x = 0; x < SIZE; x++)
		{
			// send the addresses of the foreground and background variables,
			// so that they can be modified by the getColors function
			getColors(board[x][y], scheme, &fg, &bg);
			printf("\033[38;5;%u;48;5;%um", fg, bg); // set color
			printf("       ");
			printf("\033[m"); // reset all modes
		}
		printf("\n");
		for (x = 0; x < SIZE; x++)
		{
			getColors(board[x][y], scheme, &fg, &bg);
			printf("\033[38;5;%u;48;5;%um", fg, bg); // set color
			if (board[x][y] != 0)
			{
				/* MODERN: width-proven helper replaces `1 << board[x][y]`
				 * (strict mode rejects an unproven variable shift count).
				 * Identical value for every reachable exponent. */
				uint32_t number = gb_pow2(board[x][y]);
				{
					uint8_t t = 7 - getDigitCount(number);
					printf("%*s%u%*s", t - t / 2, "", number, t / 2, "");
				}
			}
			else
			{
				printf("   ·   ");
			}
			printf("\033[m"); // reset all modes
		}
		printf("\n");
		for (x = 0; x < SIZE; x++)
		{
			getColors(board[x][y], scheme, &fg, &bg);
			printf("\033[38;5;%u;48;5;%um", fg, bg); // set color
			printf("       ");
			printf("\033[m"); // reset all modes
		}
		printf("\n");
	}
	printf("\n");
	printf("        ←,↑,→,↓ or q        \n");
	printf("\033[A"); // one line up
}

uint8_t findTarget(uint8_t array[SIZE], uint8_t x, uint8_t stop)
{
	uint8_t t;
	// if the position is already on the first, don't evaluate
	if (x == 0)
	{
		return x;
	}
	for (t = x - 1;; t--)
	{
		if (array[t] != 0)
		{
			if (array[t] != array[x])
			{
				// merge is not possible, take next position
				return t + 1;
			}
			return t;
		}
		else
		{
			// we should not slide further, return this one
			if (t == stop)
			{
				return t;
			}
		}
	}
	// we did not find a target
	return x;
}

bool slideArray(uint8_t array[SIZE], uint32_t *score)
{
	bool success = false;
	uint8_t x, t, stop = 0;

	for (x = 0; x < SIZE; x++)
	{
		if (array[x] != 0)
		{
			t = findTarget(array, x, stop);
			// if target is not original position, then move or merge
			if (t != x)
			{
				// if target is zero, this is a move
				if (array[t] == 0)
				{
					array[t] = array[x];
				}
				else if (array[t] == array[x])
				{
					// merge (increase power of two)
					array[t]++;
					// increase score
					/* MODERN: width-proven helper replaces `1 << array[t]`;
					 * identical for every reachable exponent. */
					*score += gb_pow2(array[t]);
					// set stop to avoid double merge
					stop = t + 1;
				}
				array[x] = 0;
				success = true;
			}
		}
	}
	return success;
}

void rotateBoard(uint8_t board[SIZE][SIZE])
{
	uint8_t i, j, n = SIZE;
	uint8_t tmp;
	for (i = 0; i < n / 2; i++)
	{
		for (j = i; j < n - i - 1; j++)
		{
			tmp = board[i][j];
			board[i][j] = board[j][n - i - 1];
			board[j][n - i - 1] = board[n - i - 1][n - j - 1];
			board[n - i - 1][n - j - 1] = board[n - j - 1][i];
			board[n - j - 1][i] = tmp;
		}
	}
}

bool moveUp(uint8_t board[SIZE][SIZE], uint32_t *score)
{
	bool success = false;
	uint8_t x;
	for (x = 0; x < SIZE; x++)
	{
		success |= slideArray(board[x], score);
	}
	return success;
}

bool moveLeft(uint8_t board[SIZE][SIZE], uint32_t *score)
{
	bool success;
	rotateBoard(board);
	success = moveUp(board, score);
	rotateBoard(board);
	rotateBoard(board);
	rotateBoard(board);
	return success;
}

bool moveDown(uint8_t board[SIZE][SIZE], uint32_t *score)
{
	bool success;
	rotateBoard(board);
	rotateBoard(board);
	success = moveUp(board, score);
	rotateBoard(board);
	rotateBoard(board);
	return success;
}

bool moveRight(uint8_t board[SIZE][SIZE], uint32_t *score)
{
	bool success;
	rotateBoard(board);
	rotateBoard(board);
	rotateBoard(board);
	success = moveUp(board, score);
	rotateBoard(board);
	return success;
}

bool findPairDown(uint8_t board[SIZE][SIZE])
{
	bool success = false;
	uint8_t x, y;
	for (x = 0; x < SIZE; x++)
	{
		for (y = 0; y < SIZE - 1; y++)
		{
			if (board[x][y] == board[x][y + 1])
				return true;
		}
	}
	return success;
}

uint8_t countEmpty(uint8_t board[SIZE][SIZE])
{
	uint8_t x, y;
	uint8_t count = 0;
	for (x = 0; x < SIZE; x++)
	{
		for (y = 0; y < SIZE; y++)
		{
			if (board[x][y] == 0)
			{
				count++;
			}
		}
	}
	return count;
}

bool gameEnded(uint8_t board[SIZE][SIZE])
{
	bool ended = true;
	if (countEmpty(board) > 0)
		return false;
	if (findPairDown(board))
		return false;
	rotateBoard(board);
	if (findPairDown(board))
		ended = false;
	rotateBoard(board);
	rotateBoard(board);
	rotateBoard(board);
	return ended;
}

void addRandom(uint8_t board[SIZE][SIZE])
{
	static bool initialized = false;
	uint8_t x, y;
	uint8_t n;
	int picked = 0, cell = 0;
	size_t empty_count = 0;
	vector empties = vector();
	error err = error();

	if (!initialized)
	{
		srand(time(NULL));
		initialized = true;
	}

	/* MODERN: the original fixed list[SIZE*SIZE][2] of (x,y) pairs is a
	 * checked C+ vector of linear cell indices.  Insertion order is the same
	 * x-major/y-minor scan, so the index chosen by rand() maps to the same
	 * cell as the original list[r]. */
	for (x = 0; x < SIZE; x++)
	{
		for (y = 0; y < SIZE; y++)
		{
			if (board[x][y] == 0)
			{
				if (empties.push_back((int)x * SIZE + (int)y) != 0)
					err.set(CPLUS_EXIT_RESOURCE, "2048: addRandom push failed");
			}
		}
	}

	/* Early return gives strict mode the non-empty proof for the modulus. */
	empty_count = empties.size();
	if (empty_count == 0)
	{
		empties.drop();
		return;
	}

	picked = (int)((unsigned int)rand() % (unsigned int)empty_count);
	empties.at_checked((size_t)picked, &cell);
	x = (uint8_t)(cell / SIZE);
	y = (uint8_t)(cell % SIZE);
	n = (uint8_t)((rand() % 10) / 9 + 1);
	board[x][y] = n;

	empties.drop();
}

void initBoard(uint8_t board[SIZE][SIZE])
{
	uint8_t x, y;
	for (x = 0; x < SIZE; x++)
	{
		for (y = 0; y < SIZE; y++)
		{
			board[x][y] = 0;
		}
	}
	addRandom(board);
	addRandom(board);
}

void setBufferedInput(bool enable)
{
	static bool enabled = true;
	static struct termios old;
	struct termios new;

	if (enable && !enabled)
	{
		// restore the former settings
		tcsetattr(STDIN_FILENO, TCSANOW, &old);
		// set the new state
		enabled = true;
	}
	else if (!enable && enabled)
	{
		// get the terminal settings for standard input
		tcgetattr(STDIN_FILENO, &new);
		// we want to keep the old setting to restore them at the end
		old = new;
		// disable canonical mode (buffered i/o) and local echo
		new.c_lflag &= (~ICANON & ~ECHO);
		// set the new settings immediately
		tcsetattr(STDIN_FILENO, TCSANOW, &new);
		// set the new state
		enabled = false;
	}
}

bool testSucceed()
{
	uint8_t array[SIZE];
	// these are exponents with base 2 (1=2 2=4 3=8)
	// data holds per line: 4x IN, 4x OUT, 1x POINTS
	uint8_t data[] = {
		0, 0, 0, 1, 1, 0, 0, 0, 0,
		0, 0, 1, 1, 2, 0, 0, 0, 4,
		0, 1, 0, 1, 2, 0, 0, 0, 4,
		1, 0, 0, 1, 2, 0, 0, 0, 4,
		1, 0, 1, 0, 2, 0, 0, 0, 4,
		1, 1, 1, 0, 2, 1, 0, 0, 4,
		1, 0, 1, 1, 2, 1, 0, 0, 4,
		1, 1, 0, 1, 2, 1, 0, 0, 4,
		1, 1, 1, 1, 2, 2, 0, 0, 8,
		2, 2, 1, 1, 3, 2, 0, 0, 12,
		1, 1, 2, 2, 2, 3, 0, 0, 12,
		3, 0, 1, 1, 3, 2, 0, 0, 4,
		2, 0, 1, 1, 2, 2, 0, 0, 4};
	uint8_t *in, *out, *points;
	uint8_t t, tests;
	uint8_t i;
	bool success = true;
	uint32_t score;

	tests = (sizeof(data) / sizeof(data[0])) / (2 * SIZE + 1);
	for (t = 0; t < tests; t++)
	{
		in = data + t * (2 * SIZE + 1);
		out = in + SIZE;
		points = in + 2 * SIZE;
		for (i = 0; i < SIZE; i++)
		{
			array[i] = in[i];
		}
		score = 0;
		slideArray(array, &score);
		for (i = 0; i < SIZE; i++)
		{
			if (array[i] != out[i])
			{
				success = false;
			}
		}
		if (score != *points)
		{
			success = false;
		}
		if (success == false)
		{
			for (i = 0; i < SIZE; i++)
			{
				printf("%u ", in[i]);
			}
			printf("=> ");
			for (i = 0; i < SIZE; i++)
			{
				printf("%u ", array[i]);
			}
			printf("(%u points) expected ", score);
			for (i = 0; i < SIZE; i++)
			{
				printf("%u ", in[i]);
			}
			printf("=> ");
			for (i = 0; i < SIZE; i++)
			{
				printf("%u ", out[i]);
			}
			printf("(%u points)\n", *points);
			break;
		}
	}
	if (success)
	{
		printf("All %u tests executed successfully\n", tests);
	}
	return success;
}

void signal_callback_handler(int signum)
{
	/* MODERN: async-signal-safe write() replaces the original's two printf()
	 * calls (strict mode rejects printf in a signal handler).  The byte
	 * sequences are identical.  fflush() first so the buffered board output
	 * is emitted BEFORE these writes, exactly as the original's exit()-time
	 * flush of its own printf buffer did. */
	fflush(stdout);
	(void)write(STDOUT_FILENO, gb_terminated_msg, sizeof(gb_terminated_msg) - 1);
	setBufferedInput(true);
	(void)write(STDOUT_FILENO, gb_cursor_msg, sizeof(gb_cursor_msg) - 1);
	exit(signum);
}

int main(int argc, char *argv[])
{
	uint8_t board[SIZE][SIZE];
	uint8_t scheme = 0;
	uint32_t score = 0;
	int c;
	bool success;
	/* MODERN: argv is fetched with sized builtin copies and matched through a
	 * checked C+ string; allocation failure is kept in a checked error record
	 * and never changes observable CLI output. */
	char *arg1;
	string arg = string((const char *)0);
	error err = error();

	// handle the command line options
	if (argc > 1)
	{
		arg1 = gb_arg(argv, 1);
		gb_make_arg(arg1, &arg, &err);
		if (arg.equals("-h") || arg.equals("--help"))
		{
			printf("Usage: 2048 [OPTION] | [MODE]\n");
			printf("Play the game 2048 in the console\n\n");
			printf("Options:\n");
			printf("  -h,  --help       Show this help message.\n");
			printf("  -v,  --version    Show version number.\n\n");
			printf("Modes:\n");
			printf("  bluered      Use a blue-to-red color scheme (requires 256-color terminal support).\n");
			printf("  blackwhite   The black-to-white color scheme (requires 256-color terminal support).\n");
			return EXIT_SUCCESS;
		}
		else if (arg.equals("-v") || arg.equals("--version"))
		{
			printf("2048.c version %s\n", VERSION);
			return EXIT_SUCCESS;
		}
		else if (arg.equals("blackwhite"))
		{
			scheme = 1;
		}
		else if (arg.equals("bluered"))
		{
			scheme = 2;
		}
		else if (arg.equals("whiteblack"))
		{
			scheme = 3;
		}
		else if (arg.equals("test"))
		{
			return testSucceed() ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		else
		{
			/* Byte-for-byte equivalent of the original single printf:
			 *   printf("Invalid option: %s\n\nTry '%s --help' for more options.\n",
			 *          argv[1], argv[0]);
			 * Echoing each byte keeps the output identical for any argument
			 * length without relying on an unverifiable argv extent. */
			printf("Invalid option: ");
			gb_echo(arg1);
			printf("\n\nTry '");
			gb_echo(gb_arg(argv, 0));
			printf(" --help' for more options.\n");
			return EXIT_FAILURE;
		}
	}

	// make cursor invisible, erase entire screen
	printf("\033[?25l\033[2J");

	// register signal handler for when ctrl-c is pressed
	signal(SIGINT, signal_callback_handler);

	initBoard(board);
	setBufferedInput(false);
	drawBoard(board, scheme, score);
	while (true)
	{
		c = getchar();
		if (c == EOF)
		{
			puts("\nError! Cannot read keyboard input!");
			break;
		}
		switch (c)
		{
		case 52:  // '4' key
		case 97:  // 'a' key
		case 104: // 'h' key
		case 68:  // left arrow
			success = moveLeft(board, &score);
			break;
		case 54:  // '6' key
		case 100: // 'd' key
		case 108: // 'l' key
		case 67:  // right arrow
			success = moveRight(board, &score);
			break;
		case 56:  // '8' key
		case 119: // 'w' key
		case 107: // 'k' key
		case 65:  // up arrow
			success = moveUp(board, &score);
			break;
		case 50:  // '2' key
		case 115: // 's' key
		case 106: // 'j' key
		case 66:  // down arrow
			success = moveDown(board, &score);
			break;
		default:
			success = false;
		}
		if (success)
		{
			drawBoard(board, scheme, score);
			usleep(150 * 1000); // 150 ms
			addRandom(board);
			drawBoard(board, scheme, score);
			if (gameEnded(board))
			{
				printf("         GAME OVER          \n");
				break;
			}
		}
		if (c == 'q')
		{
			printf("        QUIT? (y/n)         \n");
			c = getchar();
			if (c == 'y')
			{
				break;
			}
			drawBoard(board, scheme, score);
		}
		if (c == 'r')
		{
			printf("       RESTART? (y/n)       \n");
			c = getchar();
			if (c == 'y')
			{
				initBoard(board);
				score = 0;
			}
			drawBoard(board, scheme, score);
		}
	}
	setBufferedInput(true);

	// make cursor visible, reset all modes
	printf("\033[?25h\033[m");

	return EXIT_SUCCESS;
}
