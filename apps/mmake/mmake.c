#include <stdio.h>
#include <errno.h>





int main() {
  FILE *fp;
  fp = fopen("MakeFile","r");
  if(fp == NULL) {
    printf("mmake: *** can't open MakeFile : %s stop.\n",strerror(errno));
  }
  fclose(fp);

  return 0;
}