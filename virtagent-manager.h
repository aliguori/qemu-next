#ifndef VIRTAGENT_MANAGER_H
#define VIRTAGENT_MANAGER_H

#include "qemu-common.h"
#include "qemu-queue.h"

/*
 * Protocol Overview:
 *
 * The virtagent protocol depends on a state machine to manage communication
 * over a single connection stream, currently a virtio or isa serial channel.
 * The basic characterization of the work being done is that clients
 * send/handle client jobs locally, which are then read/handled remotely as
 * server jobs. A client job consists of a request which is sent, and a
 * response which is eventually recieved. A server job consists of a request
 * which is recieved from the other end, and a response which is sent back.
 * 
 * Server jobs are given priority over client jobs, i.e. if we send a client
 * job (our request) and recieve a server job (their request), rather than
 * await a response to the client job, we immediately begin processing the
 * server job and then send back the response. This prevents us from being
 * deadlocked in a situation where both sides have sent a client job and are
 * awaiting the response before handling the other side's client job.
 *
 * Multiple in-flight requests are supported, but high request rates can
 * potentially starve out the other side's client jobs / requests, so we'll
 * behaved participants should periodically backoff on high request rates, or
 * limit themselves to 1 request at a time (anything more than 1 can still
 * potentionally remove any window for the other end to service it's own
 * client jobs, since we can begin sending the next request before it begins
 * send the response for the 2nd).
 * 
 * On a related note, in the future, bidirectional user/session-level guest
 * agents may also be supported via a forwarding service made available
 * through the system-level guest agent. In this case it is up to the
 * system-level agent to handle forwarding requests in such a way that we
 * don't starve the host-side service out sheerly by having too many
 * sessions/users trying to send RPCs at a constant rate. This would be
 * supported through this job Manager via an additional "forwarder" job type.
 *
 * To encapsulate some of this logic, we define here a "Manager" class, which
 * provides an abstract interface to a state machine which handles most of
 * the above logic transparently to the transport/application-level code.
 * This also makes it possible to utilize alternative
 * transport/application-level protocols in the future.
 *
 */

/*
 * Two types of jobs are generated from various components of virtagent.
 * Each job type has a priority, and a set of prioritized functions as well.
 *
 * The read handler generates new server jobs as it recieves requests from
 * the channel. Server jobs make progress through the following operations.
 *
 * EXECUTE->EXECUTE_DONE->SEND->SEND_DONE
 *
 * EXECUTE (provided by user, manager calls)
 * When server jobs are added, eventually (as execution slots become
 * available) an execute() will be called to begin executing the job. An
 * error value will be returned if there is no room in the queue for another
 * server job.
 *
 * EXECUTE_DONE (provided by manager, user calls)
 * As server jobs complete, execute_completed() is called to update execution
 * status of that job (failure/success), inject the payload, and kick off the
 * next operation.
 *
 * SEND (provided by user, manager calls)
 * Eventually the send() operation is made. This will cause the send handler
 * to begin sending the response.
 *
 * SEND_DONE (provided by manager, user calls)
 * Upon completion of that send, the send_completed() operation will be
 * called. This will free up the job, and kick off the next operation.
 */
typedef int (va_job_op)(void *opaque, const char *tag);
typedef struct VAServerJobOps {
    va_job_op *execute;
    va_job_op *send;
    va_job_op *callback;
} VAServerJobOps;

/*
 * The client component generates new client jobs as they're made by
 * virtagent in response to monitored events or user-issued commands.
 * Client jobs progress via the following operations.
 *
 * SEND->SEND_DONE->READ_DONE
 * 
 * SEND (provided by user, called by manager)
 * After client jobs are added, send() will eventually be called to queue
 * the job up for xmit over the channel.
 *
 * SEND_DONE (provided by manager, called by user)
 * Upon completion of the send, send_completed() should be called with
 * failure/success indication.
 *
 * READ_DONE (provided by manager, called by user)
 * When a response for the request is read back via the transport layer,
 * read_done() will be called by the user to indicate success/failure,
 * inject the response, and make the associated callback.
 */
typedef int (va_client_job_cb)(void *opaque, void *resp_opaque,
                               const char *tag);
typedef struct VAClientJobOps {
    va_job_op *send;
    va_client_job_cb *callback;
} VAClientJobOps;

typedef struct VAManager VAManager;

VAManager *va_manager_new(void);
void va_kick(VAManager *m);

/* interfaces for server jobs */
int va_server_job_add(VAManager *m, const char *tag, void *opaque,
                      VAServerJobOps ops);
void va_server_job_execute_done(VAManager *m, const char *tag);
void va_server_job_send_done(VAManager *m, const char *tag);
void va_server_job_cancel(VAManager *m, const char *tag);

/* interfaces for client jobs */
int va_client_job_add(VAManager *m, const char *tag, void *opaque,
                      VAClientJobOps ops);
void va_client_job_cancel(VAManager *m, const char *tag);
void va_client_job_send_done(VAManager *m, const char *tag);
void va_client_job_read_done(VAManager *m, const char *tag, void *resp);

#endif /* VIRTAGENT_MANAGER_H */
