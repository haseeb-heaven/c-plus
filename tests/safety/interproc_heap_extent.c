#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "cplus/cplus.h"

int* get_data(int*ptr, size_t size){
	if(size < 0){
		return NULL;
	}

	for(int x = 0; x < 10; x++){
		ptr[x] = x + 10; // RUNTIME error or exception here see ptr is 1 size only
	}
	return ptr;
}

int main(){
	int *ptr = malloc(1);
	size_t size = 5;
	int *data = get_data(ptr, size); // even here.
	print("ptr is ", *ptr);
}
