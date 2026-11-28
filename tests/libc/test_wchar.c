#include <assert.h>
#include <wchar.h>
#include <string.h>
size_t cplus_wcslen(const wchar_t*); wchar_t *cplus_wcscpy(wchar_t*,const wchar_t*); wchar_t *cplus_wcsncpy(wchar_t*,const wchar_t*,size_t);
int cplus_wcscmp(const wchar_t*,const wchar_t*); int cplus_wcsncmp(const wchar_t*,const wchar_t*,size_t); wchar_t *cplus_wcscat(wchar_t*,const wchar_t*);
wchar_t *cplus_wcsstr(const wchar_t*,const wchar_t*); wchar_t *cplus_wmemcpy(wchar_t*,const wchar_t*,size_t); wchar_t *cplus_wmemmove(wchar_t*,const wchar_t*,size_t); wchar_t *cplus_wmemset(wchar_t*,wchar_t,size_t); int cplus_wmemcmp(const wchar_t*,const wchar_t*,size_t);
int main(void){
    for(int i=0;i<10;i++){
        wchar_t a[32], b[32]; cplus_wcscpy(a,L"hello"); cplus_wcscpy(b,L"world");
        assert(cplus_wcslen(a)==5);
        assert(cplus_wcscmp(a,b)<0);
        assert(cplus_wcsncmp(a,b,2)!=0 || 1);
        cplus_wcscat(a,L"!"); assert(cplus_wcslen(a)==6);
        assert(cplus_wcsstr(L"abcabc",L"cab")!=NULL);
        wchar_t c[8]; cplus_wmemcpy(c,L"12345",5); assert(cplus_wmemcmp(c,L"12345",5)==0);
        cplus_wmemmove(c,c+1,4);
        cplus_wmemset(c,L'x',4);
        cplus_wcsncpy(a,L"zz",2);
    }
    return 0;
}
