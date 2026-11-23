int garr[4]; int *gp=garr;
static void wr(void){ gp[7]=1; }
int main(void){ wr(); return garr[0]; }
