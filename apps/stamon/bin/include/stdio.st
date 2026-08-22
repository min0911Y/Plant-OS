//标准输入输出库

import stddef;
import math;
import string;

func puts(s) {
    native("puts", s);
}

func print(n) {
    puts(str(n));
}

func println(n) {
    puts(str(n));
    puts("\n");
}

func input() {
    return native("input", null);
}