#include <syscall.h>
#include <string.h>
#include <arg.h>
#include <stdio.h>
int main(int argc,char **argv)
{
    char *s = (char *)malloc(sizeof(char) * 30000);
    char *code = (char *)malloc(sizeof(char) * 5000);
    int len = 0;
    int stack[100];
    int stack_len = 0;
    char c;
    int j, k, x = 0;
    char *p = s + 10000;

    if (argc == 1)
    {
        scan(code, 5000);
        len = strlen(code);
    }else if(argc==2)
    {
        char *filename = malloc(sizeof(char) * 100);
        strcpy(filename,argv[1]);
        FILE *stream = fopen(filename, "rb");
        if(stream == NULL)
        {
            print("file not found\n");
            return 0;
        }
        else{
            len = fread(code, 1, 4999, stream);
            fclose(stream);
            code[len] = '\0';
            len = strlen(code);
        }
    }
    for (int i = 0; i < len; i++)
    {
        if (code[i] == '+')
        {
            (*p)++;
        }
        else if (code[i] == '-')
        {
            (*p)--;
        }
        else if (code[i] == '>')
        {
            p++;
        }
        else if (code[i] == '<')
        {
            p--;
        }
        else if (code[i] == '.')
        {
            putch((int)(*p));
        }
        else if (code[i] == ',')
        {
            *p = getch();
        }
        else if (code[i] == '[')
        {
            if (*p)
            {
                stack[stack_len++] = i;
            }
            else
            {
                for (k = i, j = 0; k < len; k++)
                {
                    code[k] == '[' && j++;
                    code[k] == ']' && j--;
                    if (j == 0)
                        break;
                }
                if (j == 0)
                    i = k;
                else
                {
                    // fprintf(stderr,"%s:%dn",__FILE__,__LINE__);
                    return 1;
                }
            }
        }
        else if (code[i] == ']')
        {
            i = stack[stack_len-- - 1] - 1;
        }
    }
    print("\n");
    return 0;
}
