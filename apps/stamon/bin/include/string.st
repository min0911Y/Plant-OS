import stddef;

func str(s) {
    def port = 3;
    def arg = s;
    sfn port, arg;
    return arg;
}