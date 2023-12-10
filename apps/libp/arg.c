#include <arg.h>
#include <string.h>
#include <math.h>
int get_argc(char *s) {  
    int count = 0;  
    int in_quote = 0;  
    while (*s) {  
        if (*s == '"') {  
            in_quote = !in_quote;  
        } else if (*s == ' ' && !in_quote) {  
            count++;  
        }  
        s++;  
    }  
    return count+1;  
}
int get_arg(char *arg, char *s, int count) {
    int i = 0, j = 0;
    int in_quote = 0;
    while (*s != '\0') {
        if (j == count) {
		int f = 0;
		if(*s == '\"') {
		       	*s++;
			f = 1;
		}
		while(*s && (*s != ' ' || f) && *s != '\"') arg[i++] = *s++;
		goto end;
	}
	if (*s == '"') {
            in_quote = !in_quote;
        } else if (!in_quote && *s == ' ') {
            j++;
        }
        s++;
    }
end:
    arg[i] = '\0';
    return j == count - 1 ? 0 : -1;
}


