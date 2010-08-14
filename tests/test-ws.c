#include "buffer.h"
#include "ui/ws.h"

static const struct {
    const char *key1;
    const char *key2;
    uint8_t key3[8];
    uint8_t rsp[16];
} test_data[] = {
    {
        .key1 = "3e6b263  4 17 80",
        .key2 = "17  9 G`ZD9   2 2b 7X 3 /r90",
        .key3 = { 0x57, 0x6A, 0x4E, 0x7D, 0x7C, 0x4D, 0x28, 0x36 },
        .rsp = { 0x6E, 0x60, 0x39, 0x65, 0x42, 0x6B, 0x39, 0x7A,
                 0x24, 0x52, 0x38, 0x70, 0x4F, 0x74, 0x56, 0x62 },
    },
    { }
};

static void print_md5sum(const uint8_t *sum)
{
    printf("%02x%02x%02x%02x" "%02x%02x%02x%02x"
           "%02x%02x%02x%02x" "%02x%02x%02x%02x",
           sum[0], sum[1], sum[2], sum[3],
           sum[4], sum[5], sum[6], sum[7],
           sum[8], sum[9], sum[10], sum[11],
           sum[12], sum[13], sum[14], sum[15]);
}

int main(int argc, char **argv)
{
    int i;

    for (i = 0; test_data[i].key1; i++) {
        uint8_t response[16];

        if (ws_compute_challenge(test_data[i].key1,
                                 test_data[i].key2,
                                 test_data[i].key3,
                                 response)) {
            if (memcmp(test_data[i].rsp, response, 16) != 0) {
                printf("FAILED computing challenge\n");
                printf(" expected: ");
                print_md5sum(test_data[i].rsp);
                printf("\n");
                printf(" actual: ");
                print_md5sum(response);
                printf("\n");
            } 
        } else {
            fprintf(stderr, "invalid input\n");
            return 1;
        }
    }
    
    return 0;
}
