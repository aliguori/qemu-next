#define MAX_QEMU_RAM_SLOTS 20

typedef struct QemuRamSlot
{
    target_phys_addr_t start_addr;
    ram_addr_t size;
    ram_addr_t offset;
    int ref;
} QemuRamSlot;

static int num_ram_slots;
static QemuRamSlot ram_slots[MAX_QEMU_RAM_SLOTS];

static int in_slot(target_phys_addr_t needle,
                   target_phys_addr_t haystack, ram_addr_t size)
{
    if (needle >= haystack && needle < (haystack + size)) {
        return 1;
    }
    return 0;
}

static QemuRamSlot *qemu_ram_find_slot(target_phys_addr_t start_addr,
                                       ram_addr_t size)
{
    int i;

    for (i = 0; i < num_ram_slots; i++) {
        QemuRamSlot *s = &ram_slots[i];

        if (s->start_addr == start_addr && s->size == size) {
            return s;
        }

        if (in_slot(start_addr, s->start_addr, s->size) ||
            in_slot(start_addr + size, s->start_addr, s->size) ||
            in_slot(s->start_addr, start_addr, size) ||
            in_slot(s->start_addr + s->size, start_addr, size)) {
            abort();
        }
    }

    return NULL;
}

void qemu_ram_register(target_phys_addr_t start_addr, ram_addr_t size)
{
    ram_addr_t offset;
    QemuRamSlot *s;

    s = qemu_ram_find_slot(start_addr, size);
    assert(s == NULL);
    assert(num_ram_slots < MAX_QEMU_RAM_SLOTS);

    s = &ram_slots[num_ram_slots++];

    s->start_addr = start_addr;
    s->size = size;
    s->offset = qemu_ram_alloc(size);
    s->ref = 0;

    cpu_register_physical_memory(s->start_addr, s->size, s->offset);
}

int qemu_ram_unregister(target_phys_addr_t start_addr, ram_addr_t size)
{
    QemuRamSlot *s;
    int i;

    s = qemu_ram_find_slot(start_addr, size);
    assert(s != NULL);

    if (s->ref > 0) {
        return -EINUSE;
    }

    i = ((s - ram_slots) / sizeof(ram_slots[0]);)
    memcpy(&ram_slots[i], &ram_slots[i + 1],
           (num_ram_slots - i) * sizeof(ram_slots[0]));
    num_ram_slots--;

    cpu_register_physical_memory(start_addr, size, IO_MEM_UNASSIGNED);

    return 0;
}

void qemu_ram_alias(target_phys_addr_t src_addr, target_phys_addr_t dst_addr,
                    ram_addr_t size)
{
    QemuRamSlot *s, *d;

    s = qemu_ram_find_slot(src_addr, size);
    assert(s != NULL);
    assert(num_ram_slots < MAX_QEMU_RAM_SLOTS);

    d = &ram_slots[num_ram_slots++];
    d->start_addr = dst_addr;
    d->size = size;
    d->offset = s->offset;
    d->ref = 0;
}
