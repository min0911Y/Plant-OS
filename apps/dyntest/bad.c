extern int deliberately_missing(void);
int bad_symbol(void) { return deliberately_missing(); }
