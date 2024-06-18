//标准输入输出库

import stddef;
import math;
import string;

func puts(s) {
    def port = 0;
    sfn port, s;
}

func print(n) {
    puts(str(n));
}

func println(n) {
    puts(str(n));
    puts("\n");
}

func input() {

    def port = 5;
    def arg;

    sfn port, arg;

    return arg;
}