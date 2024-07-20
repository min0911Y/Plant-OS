#ifndef _CMD_H
#define _CMD_H
#include <kernel.h>
// command.c
int  command_run(char *cmdline);
void show_heap(memory *mem);
void pci_list();
void cmd_dir(char **args);
void type_deal(char *cmdline);
void pcinfo();
void mem();
void cmd_tl();
void cmd_vbetest();
// execbatch.c
int  run_bat(char *cmdline);
int  cmd_app(char *cmdline);
// chat.c
void chat_cmd();
void chat_gui();
// netgobang.c
void netgobang();
#endif