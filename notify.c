#include "qemu-common.h"
#include "notify.h"

void qemu_notifier_list_init(QEMUNotifierList *list)
{
    QTAILQ_INIT(&list->notifiers);
}

void qemu_notifier_list_add(QEMUNotifierList *list, QEMUNotifier *notifier)
{
    QEMUNotifierNode *node = qemu_mallocz(sizeof(*node));

    node->notifier = notifier;
    QTAILQ_INSERT_HEAD(&list->notifiers, node, node);
}

void qemu_notifier_list_remove(QEMUNotifierList *list, QEMUNotifier *notifier)
{
    QEMUNotifierNode *node;

    QTAILQ_FOREACH(node, &list->notifiers, node) {
        if (node->notifier == notifier) {
            break;
        }
    }

    if (node) {
        QTAILQ_REMOVE(&list->notifiers, node, node);
        qemu_free(node);
    }
}

void qemu_notifier_list_notify(QEMUNotifierList *list)
{
    QEMUNotifierNode *node, *node_next;

    QTAILQ_FOREACH_SAFE(node, &list->notifiers, node, node_next) {
        node->notifier->notify(node->notifier);
    }
}
