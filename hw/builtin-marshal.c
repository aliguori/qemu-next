#include "builtin-marshal.h"

/* these are builtin marshallers that either don't need to be sent over the wire
 * or they just don't have an easy way to be auto generated.
 */

void marshal_DeviceState(Marshaller *m, DeviceState *v, const char *name)
{
    /* no state */
    /* but there should be */
}

void marshal_qemu_irq(Marshaller *m, qemu_irq *v, const char *name)
{
    /* no state */
}

void marshal_Timer(Marshaller *m, Timer *v, const char *name)
{
    /* FIXME */
}

void marshal_Notifier(Marshaller *m, Notifier *v, const char *name)
{
    /* no state */
}

void marshal_SerialInterface(Marshaller *m, SerialInterface *v,
                             const char *name)
{
    /* no state */
    /* should we trigger a connect/disconnect? */
}
