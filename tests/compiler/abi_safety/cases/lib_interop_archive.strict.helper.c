int abi_lib_mul(int a,int b){ int t=a*b; return t; }
double abi_lib_scale(double x){ double t=x*2.0; return t; }
struct LibPair { long a; long b; };
struct LibPair abi_lib_pair(long a,long b){
  struct LibPair p; p.a=a; p.b=b; return p;
}
