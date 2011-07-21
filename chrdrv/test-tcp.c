#include "test-type-stub.h"
#include "tcp.h"

static void tcp_server_read(void *opaque, const uint8_t *buf, int size)
{
    fwrite(buf, size, 1, stdout);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    TcpServer srv;

    g_test_init(&argc, &argv, NULL);

    test_type_stub_init();

    tcp_server_initialize(&srv, "foo");

    char_driver_add_handlers(CHAR_DRIVER(&srv),
                             NULL,
                             tcp_server_read,
                             NULL,
                             CHAR_DRIVER(&srv));

    tcp_server_set_host(&srv, "localhost");
    tcp_server_set_port(&srv, "8080");
    socket_server_set_wait(SOCKET_SERVER(&srv), false);

    plug_set_realized(PLUG(&srv), true);

    g_main_loop_run(g_main_loop_new(g_main_context_default(), FALSE));

    g_test_run();

    return 0;
}
