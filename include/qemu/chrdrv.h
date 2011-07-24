#ifndef QEMU_CHAR_DRIVER
#define QEMU_CHAR_DRIVER

#include "qemu/plug.h"

/* Temporarily for a couple of enum */
#include "qemu-char.h"

/**
 * @CharDriver
 *
 * A streaming data connection, typically used to transfer data from a @Device
 * to some process on the host.
 *
 * A @CharDriver subclass implements the driver.  Typically, this is the host
 * routine and may include a TCP, stdio, or fd transport.  The @Device that is
 * interacting with the driver is the client.  This is typically a device that
 * looks like a serial port including UARTs, virtio-serial, etc.
 *
 * @CharDriver also supports by direction messaging.  These messages carry no
 * data though.
 */
typedef struct CharDriver
{
    Plug parent;

    /* Public */

    /**
     * @avail_connection used by qdev to keep track of how "connections" are
     * available in the @CharDriver verses how many qdev are using.  This is
     * meant to ensure that the same @CharDriver isn't connected to multiple
     * sockets at the same time.
     *
     * It's not well thought through though as there are other things that can
     * use a @CharDriver and the attempt at supporting mux drivers results in
     * this value just being ignored for mux (although not predictably).
     *
     * Sufficed to say, this needs to go away.
     */
    int avail_connections;

    /**
     * @opened despite what the name implies, this doesn't correspond to whether
     * the @CharDriver is opened.  @CharDriver doesn't have a notion of opened,
     * instead, @opened tracks whether the CHR_EVENT_OPEN event has been
     * generated.  The CHR_EVENT_CLOSED event will clear the @opened flag.
     *
     * The primary purpose of this flag is to ensure the CHR_EVENT_OPEN event is
     * not generated twice in a row.
     */
    int opened;

    /* Private */

    /**
     * @chr_can_read when a client has added its handlers to a @CharDriver, this
     * contains the can read callback for the client.
     */
    IOCanReadHandler *chr_can_read;

    /**
     * @chr_read when a client has added its handlers to a @CharDriver, this
     * contains the read callback for the client.
     */
    IOReadHandler *chr_read;

    /**
     * @chr_event when a client has added its handlers to a @CharDriver, this
     * contains the event callback for the client.
     */
    IOEventHandler *chr_event;

    /**
     * @handler_opaque when a client has added its handlers to a @CharDriver,
     * this contains the opaque associated with the callbacks for the client.
     */
    void *handler_opaque;
} CharDriver;

typedef struct CharDriverClass
{
    PlugClass parent_class;

    /* Public */
    int (*write)(CharDriver *s, const uint8_t *buf, int len);
    int (*ioctl)(CharDriver *s, int cmd, void *arg);
    int (*get_msgfd)(CharDriver *s);
    void (*send_event)(CharDriver *chr, int event);
    void (*accept_input)(CharDriver *chr);
    void (*set_echo)(CharDriver *chr, bool echo);
    void (*guest_open)(CharDriver *chr);
    void (*guest_close)(CharDriver *chr);

    /* Protected */
    /**
     * @open:
     *
     * This is called during realize to initialize the object.  At this point,
     * all of the properties should have been set.
     */
    void (*open)(CharDriver *s, Error **errp);

    /**
     * @update_read_handler:
     *
     * This is called after @char_driver_add_handlers is called to allow sub-
     * classes to re-register their callbacks if necessary.
     */
    void (*update_read_handler)(CharDriver *s);

    /**
     * @close:
     *
     * Called during the finalize path.  The default behavior sends a
     * CHR_EVENT_CLOSED.  It's generally better to use the classes destructor to
     * implement driver specific cleanup.
     */
    void (*close)(CharDriver *chr);
} CharDriverClass;

#define TYPE_CHAR_DRIVER "char-driver"
#define CHAR_DRIVER(obj) TYPE_CHECK(CharDriver, obj, TYPE_CHAR_DRIVER)
#define CHAR_DRIVER_CLASS(class) \
    TYPE_CLASS_CHECK(CharDriverClass, class, TYPE_CHAR_DRIVER)
#define CHAR_DRIVER_GET_CLASS(obj) \
    TYPE_GET_CLASS(CharDriverClass, obj, TYPE_CHAR_DRIVER)

/**
 * @char_driver_write:
 *
 * Write data to a @CharDriver
 *
 * @buf      The data to write
 * @len      The size of the data in buf
 *
 * Returns:  The number of bytes written to the device
 *
 * Notes:    Each backend deals with flow control on its own.  Depending on the
 *           driver, this function may block execution or silently drop data.
 *
 *           Dropping data may also occur if the backend uses a connection
 *           oriented transport and the transport is disconnected.
 *
 *           The caller receives no indication that data was dropped.  This
 *           function may return a partial write result.
 */
int char_driver_write(CharDriver *s, const uint8_t *buf, int len);

/**
 * @char_driver_ioctl:
 *
 * Performs a device specific ioctl.
 *
 * @cmd      an ioctl, see CHR_IOCTL_*
 * @arg      values depends on @cmd
 *
 * Returns:  The result of this depends on the @cmd.  If @cmd is not supported
 *           by this device, -ENOTSUP.
 */
int char_driver_ioctl(CharDriver *s, int cmd, void *arg);

/**
 * @char_driver_get_msgfd:
 *
 * If the driver has received a file descriptor through its transport, this
 * function will return the file descriptor.
 *
 * Returns:  The file descriptor or -1 if transport doesn't support file
 *           descriptor passing or doesn't currently have a file descriptor.
 */
int char_driver_get_msgfd(CharDriver *s);

/**
 * @char_driver_send_event:
 *
 * Raise an event to a driver.
 *
 * @event  see CHR_EVENT_*
 *
 * Notes:  There is no way to determine if the driver successfully handled the
 *         event.
 */
void char_driver_send_event(CharDriver *chr, int event);

/**
 * @char_driver_accept_input:
 *
 * I honestly can't tell what this function is meant to do.
 */
void char_driver_accept_input(CharDriver *chr);

/**
 * @char_driver_set_echo:
 *
 * Requests to override the backends use of echo.  This is really only
 * applicable to the stdio backend but is a generic interface today.
 *
 * @echo true to enable echo
 */
void char_driver_set_echo(CharDriver *chr, bool echo);

/**
 * @char_driver_guest_open:
 *
 * If the client has a notion of a connection, this is invoked when the
 * connection is created.
 *
 * Note:  There is no way to determine if a client has the notion of a
 *        connection.  A driver cannot rely on this function ever being called.
 */
void char_driver_guest_open(CharDriver *chr);

/**
 * @char_driver_guest_close:
 *
 * If the client has a notion of a connection, this is invoked when the
 * connection is closed.
 *
 * Note:  There is no way to determine if a client has the notion of a
 *        connection.  A driver cannot rely on this function ever being called.
 */
void char_driver_guest_close(CharDriver *chr);

/**
 * @char_driver_can_read:
 *
 * Returns:  The maximum number of bytes the client connected to the driver can
 *           receive at the moment.
 */
int char_driver_can_read(CharDriver *chr);

/**
 * @char_driver_read:
 *
 * This function transfers the contents of buf to the client.
 *
 * @buf  the buffer to write to the client
 * @len  the number of bytes to write
 *
 * Notes:  This function should only be invoked after receiving a non-zero
 *         value from @char_driver_can_read.  @len may be larger than the return
 *         value but the results are undefined.  It may result in the entire
 *         message being dropped or the message being truncated.
 */
void char_driver_read(CharDriver *chr, uint8_t *buf, int len);

/**
 * @char_driver_event:
 *
 * Sends an event to the client of a driver.
 *
 * @event  the CHR_EVENT_* to send to the client
 */
void char_driver_event(CharDriver *chr, int event);

/**
 * @char_driver_add_handlers:
 *
 * Connect a client to a @CharDriver.  A connected client may send data to the
 * driver by using the @char_driver_write function.  Data is received from the
 * driver to the client using the callbacks registered in this function.
 *
 * @fd_can_read  This callback returns the maximum amount of data the client is
 *               prepared to receive from the driver.
 *
 * @fd_read      This callback is used by the driver to pass data to the client.
 *
 * @fd_event     This callback is used to send events from the driver to the
 *               client.
 *
 * @opaque       An opaque value that is passed with the registered callbacks to
 *               form a closure.
 */
void char_driver_add_handlers(CharDriver *s,
                              IOCanReadHandler *fd_can_read,
                              IOReadHandler *fd_read,
                              IOEventHandler *fd_event,
                              void *opaque);

#endif
