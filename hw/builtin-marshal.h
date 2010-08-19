#ifndef QEMU_BUILTIN_MARSHAL_H
#define QEMU_BUILTIN_MARSHAL_H

#include "hw.h"
#include "notify.h"
#include "serialinterface.h"
#include "timer.h"
#include "marshal.h"

void marshal_DeviceState(Marshaller *m, DeviceState *v, const char *name);

void marshal_qemu_irq(Marshaller *m, qemu_irq *v, const char *name);

void marshal_Timer(Marshaller *m, Timer *v, const char *name);

void marshal_Notifier(Marshaller *m, Notifier *v, const char *name);

void marshal_SerialInterface(Marshaller *m, SerialInterface *v,
                             const char *name);

#endif
