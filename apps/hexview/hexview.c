// Author: foolish-shabby <2276316223@qq.com>
// License: WTFPL
// This software is a part of free toolpack 'myfattools', which is aimed to operate FAT images in CLI.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#define max(a, b) ((a) > (b) ? (a) : (b))

char *filename = NULL;
int row_byte_cnt = 16; // 一行默认16个字节
char placeholder = '.'; // 不可打印字符默认用.填充
unsigned int start = 0; // 默认从头开始
int end = -1; // 默认一直到结尾，负数作为特殊值表示到结尾

void print_hex_byte(unsigned char num) // 手动打印一个字节
{ // printf的%x会因未知原因发电，所以自己写了一个
    int hi = num / 16, lo = num % 16; // 一个字节两位，拆成高低位
    if (hi < 10) putchar(hi + '0'); // 在0～9之间，由于'0'到'9'连续，直接加'0'即可
    else putchar(hi - 10 + 'A'); // 在10～15之间，先变成0～5，再变成连续的A～F
    if (lo < 10) putchar(lo + '0'); // 低位同理不解释
    else putchar(lo - 10 + 'A');
}
void sig() {
    exit(0);
}
int hexview() // hexview主要部分，main主要在处理参数
{
    FILE *fp = fopen(filename, "rb"); // 先读取文件，由于是二进制查看器应该用rb
    if (!fp) {
        printf("Error: failed opening file '%s'\n", filename);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long int fsize = ftell(fp); // 经典方法获取文件大小，绕过stat，先将指针设到末尾再读指针，缺点是只能支持到long int上限的文件
    if (fsize < 0) {
        printf("Error: only support files less than 2GiB\n");
        return 1;
    }
    fseek(fp, start, SEEK_SET); // 指针放回指定的开头位置节省内存
    if (end < 0) end = fsize; // 判定负数特殊值
    unsigned char *content = malloc(end - start + 5); // 只需要end - start就够了，加5保险
    if (!content) {
        printf("Error: no memory for file content\n");
        return 1;
    }
    fread(content, end - start, 1, fp); // 这里只用读这么多
    // 开始画表头
    printf("         | "); // 后续开头有9个字符表示位置，因此先9个空格，一个分界再一个空格
    for (int i = 0; i < row_byte_cnt; i++) { // 第一行，每行有多少字节就画到多少
        print_hex_byte(i);
        putchar(' ');
    }
    printf("| ASCII\n"); // 加一个分界线表示该列结束，后面是ascii
    printf("---------+"); // 第二行，9个-（前面9个字符xxxx:xxxx）加一个+，显示效果更美观
    for (int i = 0; i < row_byte_cnt; i++) {
        printf("---"); // 每一行对应三个-（数前空格一个，数本身两个）
    }
    printf("-+"); // 还有一个空格用-，+表示下一列
    for (int i = 0; i <= max(row_byte_cnt, 6); i++) {
        printf("-"); // ascii共5个字符，需要完全包裹住；同时，每行多少字节就有多少ascii，所以两者取max
    }
    printf("\n"); // 表头完毕，正文开始
    for (unsigned int i = start; i < end; i++) { // 这样迭代可以正确显示字节
        if ((i - start) % row_byte_cnt == 0) { // 判断表头是否需要展示
            // 这里显示的是当前i，高两字节:低两字节
            print_hex_byte(i >> 24);
            print_hex_byte((i >> 16) & 0xFF);
            putchar(':');
            print_hex_byte((i >> 8) & 0xFF);
            print_hex_byte(i & 0xFF);
            printf("| "); // 加一列，开始显示数据
        }
        i -= start; // 这里是读到了content[0..end-start]，因此i需要减去start
        print_hex_byte(content[i]); // 显示十六进制
        putchar(' ');
        if (i % row_byte_cnt == row_byte_cnt - 1) { // 到达本行最后一个数据
            printf("|"); // 本列结束
            for (int j = i / row_byte_cnt * row_byte_cnt /*小于i的最大的每行字节数的倍数*/; j <= i; j++) {
                // 每一行必然是从每行字节数的倍数（因为0是第一个）开始
                if (content[j] >= 0x20 && content[j] <= 0x7e) putchar(content[j]); // 可打印的就打印
                else putchar(placeholder); // 否则打印占位符
            }
            printf("\n"); // 本行结束
        } else if (i == end - start - 1) { // 已到数据末尾但没到行末
            int next_rbc_mult = (i + row_byte_cnt) / row_byte_cnt * row_byte_cnt; // 大于i的最小的每行字节数的倍数
            int remain = next_rbc_mult - 1 - i; // 本行最后一个是next_rbc_mult - 1，再减i就知道还有多少个数没有填充
            for (int j = 0; j < remain; j++) {
                printf("   "); // 每一个数对应三个空格
            }
            printf("|"); // 本列结束
            for (int j = next_rbc_mult - row_byte_cnt /*小于i的最大的每行字节数的倍数，即本行开头*/; j <= i; j++) {
                if (content[j] >= 0x20 && content[j] <= 0x7e) putchar(content[j]); // 同上
                else putchar(placeholder); // 用上
            }
        }
        i += start; // 前面减了start，为继续遍历，这里加回来
    }
    free(content); // 把分配的content释放
    return 0;
}
// 打印使用方法，不多讲
void print_usage(char *name)
{
    puts("hexview 0.0.1 by foolish-shabby");
    printf("Usage: %s <filename> [-c num] [-p ch] [-h] [-s start] [-e end]\n", name);
    puts("Valid options:");
    puts("    -c       Specify the bytes shown per row, 16 in default (should fall in [1, 32])");
    puts("    -p       Specify the placeholder for ASCII unprintable characters, '.' in default");
    puts("    -s       Specify starting point, 0 in default (mustn't smaller than 0)");
    puts("    -e       Specify ending point, EOF in default (must larger than start)");
    puts("    -h       Show this message");
}
// 主函数，大部分代码在处理参数
int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }signal(0,sig);
    for (int i = 1; i < argc; i++) {
        // 寻找参数
        if (!strcmp(argv[i], "-c")) { // -c
            i++; // 跳过下一个参数
            row_byte_cnt = 0; // 为接受即将到来的数据提前置0
            int len = strlen(argv[i]); 
            // 不想破坏argv，所以采用这种遍历方法
            for (int j = 0; j < len; j++) {
                if (argv[i][j] < '0' || argv[i][j] > '9') { // 如果不是数字（负号也不行）
                    printf("Error: Expected positive number after -c, '%s' occurred\n", argv[i]); // 报错退出
                    return 1;
                }
                row_byte_cnt = row_byte_cnt * 10 + argv[i][j] - '0'; // 先乘10把前几位平移留出最后一位，然后把当前字符-'0'转化成数字加在末位
            } // 上面这个读整数的过程后面还存在，不再重复解释
            if (row_byte_cnt < 1 || row_byte_cnt > 32) { // [1..32]这个范围也疑似是vscode hex editor的范围
                printf("Error: invalid bytes per row\n"); // 超过，报错
                return 1;
            } // -c结束
        } else if (!strcmp(argv[i], "-p")) { // 是-p，比较简单
            i++; // 一样跳过下一个参数
            int len = strlen(argv[i]);
            if (len != 1) { // 只能是单个字符
                printf("Error: Expected 1 character after -p, %d occurred\n", len);
                return 1;
            }
            placeholder = *argv[i]; // 然后就没有然后了
        } else if (!strcmp(argv[i], "-h")) { // 有-h就不管别的选项了
            print_usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "-s")) {
            i++; // 后面全是解析数字，同上-c部分，不再解释
            start = 0;
            int len = strlen(argv[i]);
            for (int j = 0; j < len; j++) {
                if (argv[i][j] < '0' || argv[i][j] > '9') {
                    printf("Error: Expected positive number after -s, '%s' occurred\n", argv[i]);
                    return 1;
                }
                start = start * 10 + argv[i][j] - '0';
            }
        } else if (!strcmp(argv[i], "-e")) {
            // 到167行为止是解析数字部分，同-c，不解释
            i++;
            end = 0;
            int len = strlen(argv[i]);
            for (int j = 0; j < len; j++) {
                if (argv[i][j] < '0' || argv[i][j] > '9') {
                    printf("Error: Expected positive number after -e, '%s' occurred\n", argv[i]);
                    return 1;
                }
                end = end * 10 + argv[i][j] - '0';
            }
            if (end < start) { // 结束位置肯定得在起始位置之后
                printf("Error: ending point (%d) < starting point (%d)", end, start);
                return 1;
            }
        } else if (argv[i][0] == '-') { // 还有高手创造选项？
            printf("Invalid option: %s\n", argv[i]);
            puts("Sorry if that's your filename, but please remove this '-' in your filename.");
            print_usage(argv[0]);
            return 1;
        } else { // 除选项外，其余参数均视为文件名
            if (filename) { // 在此之前已经有文件名，报错
                printf("Error: multiple filenames (the first one is: '%s')\n", filename);
                return 1;
            }
            filename = malloc(strlen(argv[i]) + 5);
            if (!filename) { // 分配内存失败，报错
                printf("Error: no memory for filename\n");
                return 1;
            }
            strcpy(filename, argv[i]); // 复制一个，不用argv了
        }
    }
    if (!filename) { // 参数中没有文件名，报错
        printf("Error: filename required\n");
        return 1;
    }
    int ret = hexview(); // 调用十六进制预览部分主体
    free(filename); // 释放文件名
    return ret; // 返回hexview的返回值
}