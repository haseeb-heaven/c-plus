enum E{A,B,C};
int f(enum E e){switch(e){case A:return 1;case B:return 2;}return 0;}
int main(void){return f(C);}
