// 应用程序和批处理文件的处理函数
#include <ELF.h>
#include <cmd.h>
#include <dos.h>
int run_bat(char* cmdline) {
  // 运行批处理文件
  char* file;
  int i, j = 0;
  char* name = page_malloc(300);
  char* file1 = page_malloc(1024);
  clean(name, 300);
  for (i = 0; i < strlen(cmdline); i++) {
    if (cmdline[i] <= ' ') {
      break;
    }
    name[i] = cmdline[i];
  }
  name[i] = 0;
  int fsize = vfs_filesize(name);
  if (fsize == -1) {
    if (Path_Find_File(name, (char*)Path_Addr)) {
      Path_Find_FileName(name, name, (char*)Path_Addr);
      fsize = vfs_filesize(name);
    }
  }
  if (fsize == -1)  //没找到这个文件
  {
    //加上后缀再试一遍
    name = strcat(name, ".bat");
    fsize = vfs_filesize(name);
  }
  if (fsize == -1) {
    if (Path_Find_File(name, (char*)Path_Addr)) {
      Path_Find_FileName(name, name, (char*)Path_Addr);
      fsize = vfs_filesize(name);
    }
  }
  if (fsize != -1) {
    if (stricmp(".bat", &name[strlen(name) - 4]) != 0) {
      page_free(file1, 1024);
      page_free(name, 300);
      return 0;
    }
    FILE* fp = fopen(name, "r");
    file = (char*)fp->buffer;
    //读取每行的内容，然后调用命令解析函数（command_run）
    for (i = 0; i != fsize; i++) {
      if (file[i] == 0x0a || file[i] == 0x0d) {
        if (file[i] == '\r') {
          i++;
        }
        command_run(file1);
        j = 0;
        clean(file1, 1024);
        continue;
      }
      file1[j] = file[i];
      j++;
    }
    command_run(file1);
    fclose(fp);
    page_free(file1, 1024);
    page_free(name, 300);
    return 1;
  } else {
    page_free(file1, 1024);
    page_free(name, 300);
    return 0;
  }
}
