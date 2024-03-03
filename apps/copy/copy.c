#include <syscall.h>
#include <ctypes.h>
#include <arg.h>
#include <string.h>
int main(int argc,char **argv)
{
    char *buf = malloc(1000);
    GetCmdline(buf);
    char *arg = malloc(1000);
    char *arg2 = malloc(1000);
    if(get_argc(buf) < 3)
    {
        print("Usage: copy <source> <destination>\n");
        return 0;
    }
    get_arg(arg,buf,1);
    get_arg(arg2,buf,2);
    print("Copy: ");
    print(arg);
    print(" -> ");
    print(arg2);
    print("\n");
    Copy(arg,arg2);
    api_free(buf,1000);
    api_free(arg,1000);
    api_free(arg2,1000);
	return 0;
}