#ifndef QEMU_PYEMBED_H
#define QEMU_PYEMBED_H

void python_init(void);
void python_load(const char *filename);
void python_cleanup(void);

#endif
