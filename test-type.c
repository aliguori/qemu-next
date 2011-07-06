/** stub **/

int main(int argc, char **argv)
{
    Device device;
    SerialDevice serial;

    device_initialize(&device);
    serial_initialize(&serial);

    assert(device.parent.x == 42);
    assert(device.y == 84);

    device_reset(DEVICE(&serial));

    return 0;
}
