#include "libqmp.h"
#include <stdio.h>
#include <string.h>

static bool strequals(const char *lhs, const char *rhs)
{
    return strcmp(lhs, rhs) == 0;
}

static QmpSession *libqmp_session_new_str(const char *str)
{
    if (*str && *str >= '0' && *str <= '9') {
        return libqmp_session_new_pid(strtoul(str, NULL, 10));
    }
    return libqmp_session_new_name(str);
}

static QmpSession *libqmp_session_new_gi(GuestInfo *g)
{
    if (g->has_pid) {
        return libqmp_session_new_pid(g->pid);
    }
    return libqmp_session_new_name(g->name);
}

static int qsh_list(int argc, char **argv, Error **errp)
{
    GuestInfo *guests, *g;

    guests = libqmp_list_guests();
    printf("%-10s %-10s %-10s\n", "Name", "Pid", "State");
    printf("-----------------------------\n");
    for (g = guests; g; g = g->next) {
        QmpSession *sess;
        StatusInfo *info;

        sess = libqmp_session_new_gi(g);
        info = libqmp_query_status(sess, errp);
        if (error_is_set(errp)) {
            return 1;
        }

        printf("%-10s %-10ld %-7s\n",
               g->has_name ? g->name : " ",
               g->has_pid ? g->pid : -1,
               info->running ? "running" : "halted");

        qmp_free_status_info(info);
        qmp_session_destroy(sess);
    }
    return 0;
}

static int qsh_destroy(int argc, char **argv, Error **errp)
{
    QmpSession *sess = libqmp_session_new_str(argv[1]);

    libqmp_quit(sess, errp);
    if (error_is_set(errp)) {
        return 1;
    }
    qmp_session_destroy(sess);

    return 0;
}

static int qsh_start(int argc, char **argv, Error **errp)
{
    QmpSession *sess = libqmp_session_new_str(argv[1]);

    libqmp_cont(sess, errp);
    if (error_is_set(errp)) {
        return 1;
    }
    qmp_session_destroy(sess);

    return 0;
}

static int qsh_stop(int argc, char **argv, Error **errp)
{
    QmpSession *sess = libqmp_session_new_str(argv[1]);

    libqmp_stop(sess, errp);
    if (error_is_set(errp)) {
        return 1;
    }
    qmp_session_destroy(sess);

    return 0;
}

int main(int argc, char **argv)
{
    const char *command;
    Error *err = NULL;
    int ret;

    if (argc < 2) {
        return 0;
    }

    command = argv[1];
    argv++;
    argc--;

    if (strequals(command, "list")) {
        ret = qsh_list(argc, argv, &err);
    } else if (strequals(command, "destroy")) {
        ret = qsh_destroy(argc, argv, &err);
    } else if (strequals(command, "start")) {
        ret = qsh_start(argc, argv, &err);
    } else if (strequals(command, "stop")) {
        ret = qsh_stop(argc, argv, &err);
    } else {
        ret = 1;
    }

    if (err) {
        fprintf(stderr, "qsh: %s: error: %s\n", command, error_get_pretty(err));
    }

    return ret;
}
