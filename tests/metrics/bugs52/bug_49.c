#include <stdlib.h>
int main(void) { int** grid = (int**)malloc(3 * sizeof(int*)); for (int i = 0; i < 3; i++) grid[i] = (int*)malloc(3 * sizeof(int)); free(grid); for (int i = 0; i < 3; i++) { free(grid[i]); } return 0; }
