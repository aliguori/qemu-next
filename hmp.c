#include "hmp.h"

static int64_t mon_cpu_index = 0;

/*******************************************************/
/*                        HMP                          */
/*******************************************************/

/* These should not access any QEMU internals.  Just use QMP interfaces. */

void hmp_quit(Monitor *mon, const QDict *qdict)
{
    monitor_suspend(mon);
    qmp_quit(NULL);
}

void hmp_eject(Monitor *mon, const QDict *qdict)
{
    int force = qdict_get_try_bool(qdict, "force", 0);
    const char *filename = qdict_get_str(qdict, "device");
    Error *err = NULL;

    qmp_eject(filename, true, force, &err);
    if (err) {
        monitor_printf(mon, "eject: %s\n", error_get_pretty(err));
    }
}

void hmp_block_passwd(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *password = qdict_get_str(qdict, "password");
    Error *err = NULL;

    qmp_set_blockdev_password(device, password, &err);
    if (err) {
        monitor_printf(mon, "block_passwd: %s\n", error_get_pretty(err));
        error_free(err);
    }
}

static void cb_hmp_change_bdrv_pwd(Monitor *mon, const char *password,
                                   void *opaque)
{
    Error *encryption_err = opaque;
    Error *err = NULL;

    qmp_block_passwd(error_get_field(encryption_err, "device"),
                     password, &err);
    if (err) {
        monitor_printf(mon, "invalid password\n");
        error_free(err);
    }

    error_free(encryption_err);

    monitor_read_command(mon, 1);
}

void hmp_change(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *target = qdict_get_str(qdict, "target");
    const char *arg = qdict_get_try_str(qdict, "arg");
    Error *err = NULL;

    qmp_change(device, target, !!arg, arg, &err);
    if (error_is_type(err, QERR_DEVICE_ENCRYPTED)) {
        monitor_printf(mon, "%s (%s) is encrypted.\n",
                       error_get_field(err, "device"),
                       error_get_field(err, "encrypted_filename"));
        if (!monitor_get_rs(mon)) {
            monitor_printf(mon,
                           "terminal does not support password prompting\n");
            error_free(err);
            return;
        }
        readline_start(monitor_get_rs(mon), "Password: ", 1,
                       cb_hmp_change_bdrv_pwd, err);
    }
}

void hmp_screendump(Monitor *mon, const QDict *qdict)
{
    qmp_screendump(qdict_get_str(qdict, "filename"), NULL);
}

void hmp_stop(Monitor *mon, const QDict *qdict)
{
    qmp_stop(NULL);
}

void hmp_cont(Monitor *mon, const QDict *qdict)
{
    qmp_cont(NULL);
}

void hmp_system_reset(Monitor *mon, const QDict *qdict)
{
    qmp_system_reset(NULL);
}

void hmp_system_powerdown(Monitor *mon, const QDict *qdict)
{
    qmp_system_powerdown(NULL);
}

void hmp_set_link(Monitor *mon, const QDict *qdict)
{
    const char *name = qdict_get_str(qdict, "name");
    int up = qdict_get_bool(qdict, "up");
    Error *err = NULL;

    qmp_set_link(name, up, &err);
    if (err) {
        monitor_printf(mon, "set_link: %s\n", error_get_pretty(err));
        error_free(err);
    }
}

void hmp_set_password(Monitor *mon, const QDict *qdict)
{
    const char *protocol  = qdict_get_str(qdict, "protocol");
    const char *password  = qdict_get_str(qdict, "password");
    const char *connected = qdict_get_try_str(qdict, "connected");
    Error *err = NULL;

    qmp_set_password(password, protocol, !!connected, connected, &err);
    if (err) {
        monitor_printf(mon, "set_password: %s\n", error_get_pretty(err));
        error_free(err);
    }
}

void hmp_expire_password(Monitor *mon, const QDict *qdict)
{
    const char *protocol  = qdict_get_str(qdict, "protocol");
    const char *whenstr = qdict_get_str(qdict, "time");
    Error *err = NULL;

    qmp_expire_password(protocol, whenstr, &err);
    if (err) {
        monitor_printf(mon, "expire_password: %s\n", error_get_pretty(err));
        error_free(err);
    }
}

void hmp_cpu(Monitor *mon, const QDict *qdict)
{
    int64_t cpu_index = qdict_get_int(qdict, "index");
    mon_cpu_index = cpu_index;
}

void hmp_memsave(Monitor *mon, const QDict *qdict)
{
    uint32_t size = qdict_get_int(qdict, "size");
    const char *filename = qdict_get_str(qdict, "filename");
    uint64_t addr = qdict_get_int(qdict, "val");

    qmp_memsave(filename, addr, size, true, mon_cpu_index, NULL);
}

void hmp_pmemsave(Monitor *mon, const QDict *qdict)
{
    uint32_t size = qdict_get_int(qdict, "size");
    const char *filename = qdict_get_str(qdict, "filename");
    uint64_t addr = qdict_get_int(qdict, "val");

    qmp_pmemsave(filename, addr, size, NULL);
}

void hmp_info_version(Monitor *mon)
{
    VersionInfo *info;

    info = qmp_query_version(NULL);

    monitor_printf(mon, "%" PRId64 ".%" PRId64 ".%" PRId64 "%s\n",
                   info->qemu.major, info->qemu.minor, info->qemu.micro,
                   info->package);

    qmp_free_version_info(info);
}

void hmp_info_status(Monitor *mon)
{
    StatusInfo *info;

    info = qmp_query_status(NULL);

    monitor_printf(mon, "VM status: %s%s\n",
                   info->running ? "running" : "paused",
                   info->singlestep ? " (single step mode)" : "");

    qmp_free_status_info(info);
}

void hmp_info_block(Monitor *mon)
{
    BlockInfo *block_list, *info;

    block_list = qmp_query_block(NULL);

    for (info = block_list; info; info = info->next) {
        monitor_printf(mon, "%s: type=%s removable=%d",
                       info->device, info->type, info->removable);

        if (info->removable) {
            monitor_printf(mon, " locked=%d", info->locked);
        }

        if (info->has_inserted) {
            monitor_printf(mon, " file=");
            monitor_print_filename(mon, info->inserted->file);

            if (info->inserted->has_backing_file) {
                monitor_printf(mon, " backing_file=");
                monitor_print_filename(mon, info->inserted->backing_file);
            }
            monitor_printf(mon, " ro=%d drv=%s encrypted=%d",
                           info->inserted->ro, info->inserted->drv,
                           info->inserted->encrypted);
        } else {
            monitor_printf(mon, " [not inserted]");
        }

        monitor_printf(mon, "\n");
    }

    qmp_free_block_info(block_list);
}

void hmp_info_blockstats(Monitor *mon)
{
    BlockStats *stats_list, *stats;

    stats_list = qmp_query_blockstats(NULL);

    for (stats = stats_list; stats; stats = stats->next) {
        if (!stats->has_device) {
            continue;
        }

        monitor_printf(mon, "%s:", stats->device);
        monitor_printf(mon, " rd_bytes=%" PRId64
                       " wr_bytes=%" PRId64
                       " rd_operations=%" PRId64
                       " wr_operations=%" PRId64
                       "\n",
                       stats->stats->rd_bytes,
                       stats->stats->wr_bytes,
                       stats->stats->rd_operations,
                       stats->stats->wr_operations);
    }

    qmp_free_block_stats(stats_list);
}

void hmp_info_vnc(Monitor *mon)
{
    VncInfo *info;
    VncClientInfo *client;

    info = qmp_query_vnc(NULL);

    if (!info->enabled) {
        monitor_printf(mon, "Server: disabled\n");
        return;
    }

    monitor_printf(mon, "Server:\n");
    if (info->has_host && info->has_service) {
        monitor_printf(mon, "     address: %s:%s\n", info->host, info->service);
    }
    if (info->has_auth) {
        monitor_printf(mon, "        auth: %s\n", info->auth);
    }

    if (!info->has_clients || info->clients == NULL) {
        monitor_printf(mon, "Client: none\n");
    } else {
        for (client = info->clients; client; client = client->next) {
            monitor_printf(mon, "Client:\n");
            monitor_printf(mon, "     address: %s:%s\n",
                           client->host, client->service);
            if (client->has_x509_dname) {
                monitor_printf(mon, "  x509_dname: %s\n", client->x509_dname);
            } else {
                monitor_printf(mon, "  x509_dname: none\n");
            }
            monitor_printf(mon, "    username: %s\n",
                           client->has_sasl_username ?
                           client->sasl_username : "none");
        }
    }

    qmp_free_vnc_info(info);
}

void hmp_info_name(Monitor *mon)
{
    NameInfo *info;

    info = qmp_query_name(NULL);
    if (info->has_name) {
        monitor_printf(mon, "%s\n", info->name);
    }
    qmp_free_name_info(info);
}

void hmp_info_uuid(Monitor *mon)
{
    UuidInfo *info;

    info = qmp_query_uuid(NULL);
    monitor_printf(mon, "%s\n", info->UUID);
    qmp_free_uuid_info(info);
}

void hmp_info_cpus(Monitor *mon)
{
    CpuInfo *cpu_list, *cpu;

    cpu_list = qmp_query_cpus(NULL);

    for (cpu = cpu_list; cpu; cpu = cpu->next) {
        int active = ' ';

        if (cpu->CPU == mon_cpu_index) {
            active = '*';
        }

        monitor_printf(mon, "%c CPU #%" PRId64 ": ", active, cpu->CPU);

        if (cpu->has_pc) {
            monitor_printf(mon, "pc=0x%" PRIx64 " ", cpu->pc);
        }
        if (cpu->has_nip) {
            monitor_printf(mon, "nip=0x%" PRIx64 " ", cpu->nip);
        }
        if (cpu->has_npc) {
            monitor_printf(mon, "npc=0x%" PRIx64 " ", cpu->npc);
        }
        if (cpu->has_PC) {
            monitor_printf(mon, "PC=0x%" PRIx64 " ", cpu->PC);
        }

        if (cpu->halted) {
            monitor_printf(mon, "(halted)");
        }

        monitor_printf(mon, "\n");
    }

    qmp_free_cpu_info(cpu_list);
}

void hmp_info_kvm(Monitor *mon)
{
    KvmInfo *info;

    info = qmp_query_kvm(NULL);
    monitor_printf(mon, "kvm support: ");
    if (info->present) {
        monitor_printf(mon, "%s\n", info->enabled ? "enabled" : "disabled");
    } else {
        monitor_printf(mon, "not compiled\n");
    }

    qmp_free_kvm_info(info);
}

void hmp_info_chardev(Monitor *mon)
{
    ChardevInfo *char_info, *info;

    char_info = qmp_query_chardev(NULL);
    for (info = char_info; info; info = info->next) {
        monitor_printf(mon, "%s: filename=%s\n", info->label, info->filename);
    }

    qmp_free_chardev_info(char_info);
}

void hmp_info_mice(Monitor *mon)
{
    MouseInfo *mice_list, *mouse;

    mice_list = qmp_query_mice(NULL);
    if (mice_list == NULL) {
        monitor_printf(mon, "No mouse devices connected\n");
        return;
    }

    for (mouse = mice_list; mouse; mouse = mouse->next) {
        monitor_printf(mon, "%c Mouse #%" PRId64 ": %s%s\n",
                       mouse->current ? '*' : ' ',
                       mouse->index, mouse->name,
                       mouse->absolute ? " (absolute)" : "");
    }

    qmp_free_mouse_info(mice_list);
}

static void hmp_info_pci_device(Monitor *mon, PciDeviceInfo *dev)
{
    PciMemoryRegion *region;

    monitor_printf(mon, "  Bus %2" PRId64 ", ", dev->bus);
    monitor_printf(mon, "device %3" PRId64 ", function %" PRId64 ":\n",
                   dev->slot, dev->function);
    monitor_printf(mon, "    ");

    if (dev->class_info.has_desc) {
        monitor_printf(mon, "%s", dev->class_info.desc);
    } else {
        monitor_printf(mon, "Class %04" PRId64, dev->class_info.class);
    }

    monitor_printf(mon, ": PCI device %04" PRIx64 ":%04" PRIx64 "\n",
                   dev->id.device, dev->id.vendor);

    if (dev->has_irq) {
        monitor_printf(mon, "      IRQ %" PRId64 ".\n", dev->irq);
    }

    if (dev->has_pci_bridge) {
        monitor_printf(mon, "      BUS %" PRId64 ".\n",
                       dev->pci_bridge->bus.number);
        monitor_printf(mon, "      secondary bus %" PRId64 ".\n",
                       dev->pci_bridge->bus.secondary);
        monitor_printf(mon, "      subordinate bus %" PRId64 ".\n",
                       dev->pci_bridge->bus.subordinate);

        monitor_printf(mon, "      IO range [0x%04"PRIx64", 0x%04"PRIx64"]\n",
                       dev->pci_bridge->bus.io_range->base,
                       dev->pci_bridge->bus.io_range->limit);

        monitor_printf(mon,
                       "      memory range [0x%08"PRIx64", 0x%08"PRIx64"]\n",
                       dev->pci_bridge->bus.memory_range->base,
                       dev->pci_bridge->bus.memory_range->limit);

        monitor_printf(mon, "      prefetchable memory range "
                       "[0x%08"PRIx64", 0x%08"PRIx64"]\n",
                       dev->pci_bridge->bus.prefetchable_range->base,
                       dev->pci_bridge->bus.prefetchable_range->limit);
    }

    for (region = dev->regions; region; region = region->next) {
        uint64_t addr, size;

        addr = region->address;
        size = region->size;

        monitor_printf(mon, "      BAR%" PRId64 ": ", region->bar);

        if (!strcmp(region->type, "io")) {
            monitor_printf(mon, "I/O at 0x%04" PRIx64
                                " [0x%04" PRIx64 "].\n",
                           addr, addr + size - 1);
        } else {
            monitor_printf(mon, "%d bit%s memory at 0x%08" PRIx64
                               " [0x%08" PRIx64 "].\n",
                           region->mem_type_64 ? 64 : 32,
                           region->prefetch ? " prefetchable" : "",
                           addr, addr + size - 1);
        }
    }

    monitor_printf(mon, "      id \"%s\"\n", dev->qdev_id);

    if (dev->has_pci_bridge) {
        if (dev->pci_bridge->has_devices) {
            PciDeviceInfo *cdev;
            for (cdev = dev->pci_bridge->devices; cdev; cdev = cdev->next) {
                hmp_info_pci_device(mon, cdev);
            }
        }
    }
}

void hmp_info_pci(Monitor *mon)
{
    PciInfo *pci_list, *info;

    pci_list = qmp_query_pci(NULL);

    for (info = pci_list; info; info = info->next) {
        PciDeviceInfo *dev;

        for (dev = info->devices; dev; dev = dev->next) {
            hmp_info_pci_device(mon, dev);
        }
    }

    qmp_free_pci_info(pci_list);
}

void hmp_info_balloon(Monitor *mon)
{
    BalloonInfo *info;
    Error *err = NULL;

    info = qmp_query_balloon(&err);
    if (err) {
        monitor_printf(mon, "info balloon: %s\n", error_get_pretty(err));
        error_free(err);
        return;
    }

    monitor_printf(mon, "balloon: actual=%" PRId64, info->actual >> 20);
    if (info->has_mem_swapped_in) {
        monitor_printf(mon, " mem_swapped_in=%" PRId64, info->mem_swapped_in);
    }
    if (info->has_mem_swapped_out) {
        monitor_printf(mon, " mem_swapped_out=%" PRId64, info->mem_swapped_out);
    }
    if (info->has_major_page_faults) {
        monitor_printf(mon, " major_page_faults=%" PRId64,
                       info->major_page_faults);
    }
    if (info->has_minor_page_faults) {
        monitor_printf(mon, " minor_page_faults=%" PRId64,
                       info->minor_page_faults);
    }
    if (info->has_free_mem) {
        monitor_printf(mon, " free_mem=%" PRId64, info->free_mem);
    }
    if (info->has_total_mem) {
        monitor_printf(mon, " total_mem=%" PRId64, info->total_mem);
    }

    monitor_printf(mon, "\n");
}
