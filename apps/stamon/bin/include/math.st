import stddef;

func abs(n) {
    if n<0: return -n;
    return n;
}

func all(list) {
    for i in list {
        if !i {
            return false;
        }
    }
    return true;
}

func range(n) {
    def list = [n];
    def i = 0;
    while i<n {
        list[i] = i;
        i += 1;
    }
    return list;
}

func range_ab(a, b) {
    def list = [b-a+1];
    def i = a;
    while i<=b {
        list[i-a] = i;
        i += 1;
    }
    return list;
}

func int(s) {
    def port = 2;
    def arg = s;
    sfn port, arg;
    return arg;
}

func len(n) {
    def port = 4;
    def arg = n;
    sfn port, arg;
    return arg;
}