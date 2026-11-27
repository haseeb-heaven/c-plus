extern void *cplus_check_backdoor(void *p);
int main(void) {
    int x = 1;
    void *p = cplus_check_backdoor(&x);
    return p == 0;
}
