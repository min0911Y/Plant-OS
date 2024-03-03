func puts(s) {
    def port = 0;
    sfn port, s;
}

func printNum(n) {
    def port = 1;
    sfn port, n;
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