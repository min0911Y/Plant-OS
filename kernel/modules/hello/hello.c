#include <module.h>
#include <stdio.h>

static int hello_add(int a, int b) { return a + b; }

static int hello_init(void) {
  printk("hello_mod: init\n");
  printk("hello_mod: 2 + 3 = %d\n", hello_add(2, 3));
  return 0;
}

static int hello_exit(void) {
  printk("hello_mod: exit\n");
  return 0;
}

static const module_export_t hello_exports[] = {
    MODULE_EXPORT_SYMBOL(hello_add),
};

MODULE_INFO_DEFINE("hello_mod", hello_init, hello_exit, hello_exports);
