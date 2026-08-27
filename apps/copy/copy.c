#include <syscall.h>
#include <ctypes.h>
#include <string.h>
int main(int argc,char **argv)
{
    if(argc != 3)
    {
        print("Usage: copy <source> <destination>\n");
        return 1;
    }
    print("Copy: ");
    print(argv[1]);
    print(" -> ");
    print(argv[2]);
    print("\n");
    return Copy(argv[1],argv[2]) == 0 ? 0 : 1;
}
