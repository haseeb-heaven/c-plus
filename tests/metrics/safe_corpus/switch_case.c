static int days(int m){ switch(m){ case 2: return 28; case 4: case 6: return 30; default: return 31; } }
int main(void){ return days(2)==28 && days(4)==30 ? 0 : 1; }
