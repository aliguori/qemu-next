/*
 * QEMU Module Infrastructure
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_MODULE_H
#define QEMU_MODULE_H

#define module_init(function, priority)                                  \
static void __attribute__((constructor)) qemu_init_ ## function(void) {  \
   register_module_init(function, priority);                             \
}

#define module_exit(function, priority)                                  \
static void __attribute__((constructor)) qemu_exit_ ## function(void) {  \
   register_module_exit(function, priority);                             \
}

#define MOD_PRI_HIGHEST     0
#define MOD_PRI_BLOCK       (MOD_PRI_HIGHEST + 1)
#define MOD_PRI_CHAR_DRIVER (MOD_PRI_BLOCK + 1)

#define block_init(function) module_init(function, MOD_PRI_BLOCK)
#define block_exit(function) module_exit(function, MOD_PRI_BLOCK)

#define char_driver_init(function) module_init(function, MOD_PRI_CHAR_DRIVER)
#define char_driver_exit(function) module_exit(function, MOD_PRI_CHAR_DRIVER)

void register_module_init(int (*fn)(void), int priority);

void register_module_exit(void (*fn)(void), int priority);

int module_call_init(int priority);

void module_call_exit(int priority);

#endif
