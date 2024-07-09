import stddef;
import native;

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
    return native("int", s);
}

func len(n) {
    return native("len", n);
}

