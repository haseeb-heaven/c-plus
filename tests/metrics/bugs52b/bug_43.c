#include <stdlib.h>
struct Tree { struct Tree *left,*right; };
void destroy_tree(struct Tree*root){ if(!root)return; destroy_tree(root->left); destroy_tree(root->right); free(root); }
int main(void){ struct Tree*root=NULL; for(int i=0;i<500000;i++){ struct Tree*n=(struct Tree*)calloc(1,sizeof(struct Tree)); n->left=root; root=n;} destroy_tree(root); return 0; }
