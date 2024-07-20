#include <dos.h>
#include <io.h>

char              **Input_Stack; // 1024个字符串
struct input_stack *STACK = NULL;

void Input_Stack_Init() {
  int i;
  STACK       = (struct Input_StacK *)page_malloc(sizeof(Input_Stack));
  Input_Stack = (char *)page_malloc(sizeof(char *) * 1024);
  for (i = 0; i < 1024; i++) {
    Input_Stack[i] = (char *)page_malloc(sizeof(char) * 4096);
  }

  for (i = 0; i < 1024; i++) {
    for (int j = 0; j < 1024; j++) {
      Input_Stack[i][j] = 0;
    }
  }
  STACK->stack      = Input_Stack;
  STACK->stack_size = 1024;
  STACK->free       = 1023;
  STACK->now        = 1023;
  STACK->times      = 0;
}

void input_stack_put(char *str) {
  int i;

  if (STACK->now == 0) {
    for (i = 0; i < 1024; i++) {
      STACK->stack[i] = NULL;
    }
    STACK->free  = 1023;
    STACK->now   = 1023;
    STACK->times = 0;
    input_stack_put(str);
  } else {
    strcpy(STACK->stack[STACK->now], str);
    STACK->free--;
    STACK->now--;
    STACK->times++;
  }
}

int input_stack_get_now() {
  return STACK->now;
}

int Get_times() {
  return STACK->times;
}

char *input_stack_pop() {
  return STACK->stack[STACK->now + 1];
}

void input_stack_set_now(int now) {
  STACK->now = now;
}

int get_free() {
  return STACK->free;
}
