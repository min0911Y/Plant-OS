#ifndef _SET_JMP_H
#define _SET_JMP_H
#define _NSETJMP    10

typedef long jmp_buf[_NSETJMP];

int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#endif