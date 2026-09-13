#ifndef PLANT_SYS_PARAM_H
#define PLANT_SYS_PARAM_H

#include <machine/endian.h>
#include <limits.h>

#define MAXPATHLEN PATH_MAX
#define howmany(value, unit) (((value) + ((unit) - 1)) / (unit))
#define MIN(left, right) ((left) < (right) ? (left) : (right))
#define MAX(left, right) ((left) > (right) ? (left) : (right))

#endif
