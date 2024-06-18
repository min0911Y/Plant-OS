//系统类

import stddef;
import math;

System = class {

    def version;

    func __init__(self) {
        self.version = "2.4.3";  //当前版本号
    }

    func typeof(self, v) {
        //返回类型
        def port = 6;
        def arg = v;
        sfn port, arg;
        return arg;
    }

    func throw(self, s) {
        //抛出异常，同时让虚拟机退出
        def port = 7;
        def arg = str(s);
        sfn port, arg;
    }

    func system(self, s) {
        def port = 8;
        def arg = str(s);
        sfn port, arg;
        return arg;
    }

    func exit(self, n) {
        def port = 9;
        def arg = int(n);
        sfn port, arg;
    }

}.new;