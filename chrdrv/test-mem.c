#include "test-type-stub.h"
#include "mem.h"

static void test_hello_world(void)
{
    MemoryCharDriver mem;
    const char *str = "Hello World";
    QString *qs;

    memory_char_driver_initialize(&mem, "foo");

    char_driver_write(CHAR_DRIVER(&mem), (const uint8_t *)str, strlen(str));

    qs = memory_char_driver_get_qs(&mem);

    memory_char_driver_finalize(&mem);

    g_assert_cmpstr(str, ==, qstring_get_str(qs));

    QDECREF(qs);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    test_type_stub_init();

    g_test_add_func("/hello_world", test_hello_world);

    g_test_run();

    return 0;
}
