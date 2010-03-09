#ifndef QEMU_NOTIFY_H
#define QEMU_NOTIFY_H

#include "qemu-queue.h"

typedef struct QEMUNotifier QEMUNotifier;
typedef struct QEMUNotifierNode QEMUNotifierNode;

struct QEMUNotifier
{
    void (*notify)(QEMUNotifier *notifier);
};

struct QEMUNotifierNode
{
    QEMUNotifier *notifier;
    QTAILQ_ENTRY(QEMUNotifierNode) node;
};

typedef struct QEMUNotifierList
{
    QTAILQ_HEAD(, QEMUNotifierNode) notifiers;
} QEMUNotifierList;

#define QEMU_NOTIFIER_LIST_INITIALIZER(head) \
    { QTAILQ_HEAD_INITIALIZER((head).notifiers) }

void qemu_notifier_list_init(QEMUNotifierList *list);

void qemu_notifier_list_add(QEMUNotifierList *list, QEMUNotifier *notifier);

void qemu_notifier_list_remove(QEMUNotifierList *list, QEMUNotifier *notifier);

void qemu_notifier_list_notify(QEMUNotifierList *list);

#endif
