/*
 * QEMU Errors
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori <aliguori@us.ibm.com>
 *
 */

#ifndef QEMU_ERROR_H
#define QEMU_ERROR_H

#include "qemu-common.h"

/*
 * QEMU Errors
 *
 * These error objects are meant to be used throughout the QEMU code base as
 * a standard way to propagate errors.  The correct way to use these errors is
 * as follows:
 *
 * 1) For a function that may generate or propagate an error, it should take
 *    an additional argument of `Error **err'.
 *
 * 2) The call site should always initialize the passed in Error object to NULL
 *    and check for non-NULL to determine if the function failed.
 *
 * 3) If the call site does not care about failure, it should just pass NULL.
 *    The callee should handle a NULL argument for `err'.
 *
 * 4) The call site should always propagate the error.  If that's not possible,
 *    the call site should attempt to display the error to the user in the form:
 *    '%(domain)s: %(message)s'
 *
 * 5) The call site owns the reference to an error.  If propagating an error,
 *    it's necessary to consider the life cycle (in the event that err is NULL).
 *
 * Here's an example of the correct usage of Error:
 *
 *  int my_func(int x, int y, Error **err)
 *  {
 *      if (x > y) {
 *          error_set(err, "math", EINVAL, "`x' should be less than 'y'");
 *          return 0;
 *      }
 *
 *      return other_func(x, y, 32, err);
 *  }
 *
 *  int main(int argc, char **argv)
 *  {
 *      Error *err = NULL;
 *      int res;
 *
 *      res = my_func(atoi(argv[0]), atoi(argv[1]), &err);
 *      if (err) {
 *          fprintf(stderr, "Error: %s: %s\n", err->domain, err->message);
 *          error_free(err);
 *          return 1;
 *      }
 *      return 0;
 *  }
 */

typedef struct Error
{
    /* The context the error was generated in.  Typically, this should be the
     * name of a user recognizable module or driver such as "virtio-net" or
     * "vnc-server".
     */
    char *domain;

    /* An integer code identifying the class of error.  The meaning of this code
     * depends on the domain.
     */
    int code;

    /* A human readable message containing additional data about the error. */
    char *message;
} Error;

/* Create a new error with a printf style message */
Error *error_new(const char *domain,
                 int code,
                 const char *format,
                 ...)
    __attribute__ ((format(printf, 3, 4)));


/* Create a new error with a vprintf style va-arg */
Error *error_new_valist(const char *domain,
                        int code,
                        const char *format,
                        va_list ap);

/* Create a new error with a literal message */
Error *error_new_literal(const char *domain,
                         int code,
                         const char *message);

/* Create an error and store it in the pointer specified by errp.  If errp is
 * NULL, do nothing.
 */
void error_set(Error **errp,
               const char *domain,
               int code,
               const char *format,
               ...)
    __attribute__ ((format(printf, 4, 5)));

/* Propagate an error object handling the case that the caller function passes
 * a NULL pointer.
 */
void error_propagate(Error **errp,
                     Error *err);

/* Copy an existing error object */
Error *error_copy(const Error *err);

/* Free an error object */
void error_free(Error *e);

#endif
