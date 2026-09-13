#ifndef PLANT_LANGINFO_H
#define PLANT_LANGINFO_H

typedef int nl_item;

enum { CODESET = 1 };

#ifdef __cplusplus
extern "C" {
#endif
char *nl_langinfo(nl_item item);
#ifdef __cplusplus
}
#endif

#endif
