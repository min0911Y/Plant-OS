#include"stdio.h"
#include"string.h"
#include"stdlib.h"

static char mem[32*1024] = {0};
static char* ptr = mem;
static char script[16*1024] = {0};
int script_len = 0;

extern int bf_runscript(int origin)
{
	int i = origin;
	while(1) {
		if(i==script_len) {
			return i;
		}
		if(script[i]==']') {
			return i;
		}
		if(script[i]=='+') {
			++*ptr;
		}
		if(script[i]=='-') {
			--*ptr;
		}
		if(script[i]==';') {
			char rst;
			scanf("%c",&rst);
			*ptr = rst;
		}
		if(script[i]=='.') {
			printf("%c", *ptr);
		}
		if(script[i]=='<') {
			ptr--;
		}
		if(script[i]=='>') {
			ptr++;
		}
		if(script[i]=='[') {
			int jmp_addr;
			while(*ptr) {
				jmp_addr = bf_runscript(i+1);
			}
			i = jmp_addr;
		}
		i++;
	}
}

int bf_run()
{
	script_len = strlen(script);
	script[script_len] = ']';
	script_len++;
	bf_runscript(0);
}

char* csubstring(char* src, int st, int ed) {
	char* dst = calloc(1, ed-st+1);
	int i;
	for(i=0;i<ed-st;i++) {
		dst[i] = src[st+i];
	}
	return dst;
}

char* bfpp_compilestatement(char* statement) {
	int i;
	if(!strcmp(csubstring(statement, 0, 3),"ADD")) {
		int tmp = atoi(csubstring(statement, 4, strlen(statement)));
		char* rst = calloc(1, tmp);
		for(i=0;i<tmp;i++) {
			rst[i] = '+';
		}
		return rst;
	}
	if(!strcmp(csubstring(statement, 0, 3),"SUB")) {
		int tmp = atoi(csubstring(statement, 4, strlen(statement)));
		char* rst = calloc(1, tmp);
		for(i=0;i<tmp;i++) {
			rst[i] = '-';
		}
		return rst;
	}
	if(!strcmp(csubstring(statement, 0, 3),"LST")) {
		int tmp = atoi(csubstring(statement, 4, strlen(statement)));
		char* rst = calloc(1, tmp);
		for(i=0;i<tmp;i++) {
			rst[i] = '<';
		}
		return rst;
	}
	if(!strcmp(csubstring(statement, 0, 3),"RST")) {
		int tmp = atoi(csubstring(statement, 4, strlen(statement)));
		char* rst = calloc(1, tmp);
		for(i=0;i<tmp;i++) {
			rst[i] = '>';
		}
		return rst;
	}
	if(!strcmp(csubstring(statement, 0, 2),"IN")) {
		char* rst = calloc(1, 2);
		rst[0] = ';';
		return rst;
	}
	if(!strcmp(csubstring(statement, 0, 3),"OUT")) {
		char* rst = calloc(1, 2);
		rst[0] = '.';
		return rst;
	}
	if(!strcmp(csubstring(statement, 0, 4),"LOOP")) {
		char* rst = calloc(1, 2);
		rst[0] = '[';
		return rst;
	}
	if(!strcmp(csubstring(statement, 0, 3),"END")) {
		char* rst = calloc(1, 2);
		rst[0] = ']';
		return rst;
	}
}

int main() {
	char s[128];
	int flag = 1;
	while(flag) {
		printf("> ");
		int i;
		for(i=0;i<128;i++)	s[i] = '\0';
		gets(s);
		if(s[0]=='!') {
			bf_run();
			flag = 0;
		}
		if(s[0]=='#'||s[0]=='\0') {
			continue;
		}
		int st = script_len;
		char* tmp = bfpp_compilestatement(s);
		for(i=0;i<strlen(tmp);i++) {
			script[st+i] = tmp[i];
		}
		script_len+=strlen(tmp);
	}
	return 0;
}
