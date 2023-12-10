#include <stdio.h>
#include <syscall.h>
void copy_drive(char d, char *p1, char *p2) {
  char *buf = malloc(strlen(p2) + 4);
  if (p2[0] == '/') {
    sprintf(buf, "%c:%s", d, p2);
  } else {
    sprintf(buf, "%c:\\%s",d, p2);
  }
  Copy(p1, buf);
  free(buf);
}
void change_disk(char d) {
  char *buf = malloc(4);
  sprintf(buf, "rdrv %c", d);
  system(buf);
  free(buf);
}
int main() {
  char d, c;
  c = api_current_drive();
R:
  printf("Which drive do you want to copy to? [A/C/D/E/F]\n");
  d = getch();
  d = toupper(d);
  printf("the file doom.zip will copy to Drive %c [y/n]\n", d);
  if (getch() == 'n') {
    goto R;
  }
  printf("Reading 001doom.bin file...");
  int fsz1 = filesize("001doom.bin");
  char *buf1 = malloc(fsz1);
  api_ReadFile("001doom.bin", buf1);
  printf("done.\n");
  printf("Please insert doom2 disk and press enter to continue...");
R2:
  while (1) if (getch() == '\n') break;
  printf("\n");
  change_disk(c);
  int fsz2 = filesize("002doom.bin");
  if (fsz2 == -1) {
	printf("Insert a wrong disk, retry...");
	goto R2;
  }
  printf("Reading 002doom.bin file...");
  char *whole_file = malloc(fsz1 + fsz2);
  api_ReadFile("002doom.bin", whole_file + fsz1);
  memcpy((void *)whole_file, (void *)buf1, fsz1);
  free((void *)buf1);
  printf("done.\n");
  printf("Merging and copying nasm.bin...");
  change_disk(d);
  Edit_File("doom.zip", whole_file, fsz1 + fsz2, 0);
  free((void *)whole_file);
  printf("done.\n");
  printf("and then, you can extract it by miniunz.bin.\n  Have Fun! ^-^");
  return;
}