/* Original user-supplied crash scenario 5 (minimally adjusted to compile). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
struct Matrix { uint32_t rows, cols; double *cells; };
int main(void){ struct Matrix m={0}; printf("rows cols: "); if(scanf("%u %u",&m.rows,&m.cols)!=2) return 1;
 uint32_t total=m.rows*m.cols; m.cells=malloc((size_t)total*sizeof(double)); if(!m.cells) return 1;
 uint64_t last=(uint64_t)(m.rows-1)*m.cols+(m.cols-1); m.cells[last]=3.14159; free(m.cells); return 0; }
