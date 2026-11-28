#include <pthread.h>
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int critical_section(int fail) { pthread_mutex_lock(&lock); if (fail) { return -1; } pthread_mutex_unlock(&lock); return 0; }
int main(void) { return critical_section(1); }
