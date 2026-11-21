struct item { int id; int qty; };
int main(void){ struct item items[3]={{1,2},{2,5},{3,1}}; int total=0;
  for(int i=0;i<3;i++) total+=items[i].qty; return total==8 ? 0 : 1; }
