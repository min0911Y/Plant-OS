//系统类

import stddef;
import math;

System = class {

    def version;

    func __init__(self) {
        //当前版本号
        return native("version", null);
    }

    func typeof(self, v) {
        //返回类型
        return native("typeof", v);
    }

    func throw(self, s) {
        //抛出异常，同时让虚拟机退出
        return native("throw", s);
    }

    func system(self, s) {
        return native("system", s);
    }

    func exit(self, n) {
        native("exit", int(n));
    }

}.new;