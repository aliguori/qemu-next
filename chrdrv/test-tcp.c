#include "test-type-stub.h"
#include "mem.h"

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    test_type_stub_init();

    g_test_run();

    return 0;
}
