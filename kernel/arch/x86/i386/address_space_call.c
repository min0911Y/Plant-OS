#include <arch.h>
#include <dos.h>

uintptr_t arch_address_space_call(arch_address_space_t address_space,
                                  uintptr_t entry, void *argument) {
  uintptr_t (*function)(void *) = (uintptr_t(*)(void *))entry;
  current_task()->signal_disable = 1;
  arch_address_space_t backup = current_task()->address_space;
  current_task()->address_space = address_space;
  arch_address_space_activate(address_space);
  uintptr_t result = function(argument);
  current_task()->address_space = backup;
  arch_address_space_activate(backup);
  current_task()->signal_disable = 0;
  return result;
}
