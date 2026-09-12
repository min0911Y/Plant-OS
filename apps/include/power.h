#ifndef PLANT_POWER_H
#define PLANT_POWER_H
#ifdef __cplusplus
extern "C" {
#endif
/* Request ACPI S5. Returns -1 on failure; success does not return. */
int power_off(void);
#ifdef __cplusplus
}
#endif
#endif
