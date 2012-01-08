include $(SRC_PATH)/hw/ide/Makefile
hw-obj-y += $(addprefix ide/,$(ide-obj-y))

include $(SRC_PATH)/hw/9pfs/Makefile
hw-obj-y += $(addprefix 9pfs/, $(9pfs-obj-y))

hw-obj-$(CONFIG_PTIMER) += ptimer.o
hw-obj-$(CONFIG_MAX7310) += max7310.o
hw-obj-$(CONFIG_WM8750) += wm8750.o
hw-obj-$(CONFIG_TWL92230) += twl92230.o
hw-obj-$(CONFIG_TSC2005) += tsc2005.o
hw-obj-$(CONFIG_LM832X) += lm832x.o
hw-obj-$(CONFIG_TMP105) += tmp105.o
hw-obj-$(CONFIG_STELLARIS_INPUT) += stellaris_input.o
hw-obj-$(CONFIG_SSD0303) += ssd0303.o
hw-obj-$(CONFIG_SSD0323) += ssd0323.o
hw-obj-$(CONFIG_ADS7846) += ads7846.o
hw-obj-$(CONFIG_MAX111X) += max111x.o
hw-obj-$(CONFIG_DS1338) += ds1338.o
hw-obj-y += i2c.o smbus.o smbus_eeprom.o
hw-obj-y += eeprom93xx.o
hw-obj-y += scsi-disk.o cdrom.o
hw-obj-y += scsi-generic.o scsi-bus.o
hw-obj-y += hid.o
hw-obj-y += usb.o usb-hub.o usb-$(HOST_USB).o usb-hid.o usb-msd.o usb-wacom.o
hw-obj-y += usb-serial.o usb-net.o usb-bus.o usb-desc.o
hw-obj-$(CONFIG_SSI) += ssi.o
hw-obj-$(CONFIG_SSI_SD) += ssi-sd.o
hw-obj-$(CONFIG_SD) += sd.o
hw-obj-y += bt.o bt-host.o bt-vhci.o bt-l2cap.o bt-sdp.o bt-hci.o bt-hid.o usb-bt.o
hw-obj-y += bt-hci-csr.o
hw-obj-y += msmouse.o ps2.o
hw-obj-y += qdev.o qdev-properties.o

hw-obj-$(CONFIG_BRLAPI) += baum.o
hw-obj-$(CONFIG_POSIX) += migration-exec.o migration-unix.o migration-fd.o
hw-obj-$(CONFIG_WIN32) += version.o

# xen backend driver support
hw-obj-$(CONFIG_XEN_BACKEND) += xen_backend.o xen_devconfig.o
hw-obj-$(CONFIG_XEN_BACKEND) += xen_console.o xenfb.o xen_disk.o xen_nic.o

hw-obj-y += loader.o
hw-obj-$(CONFIG_VIRTIO) += virtio-console.o
hw-obj-y += usb-libhw.o
hw-obj-$(CONFIG_VIRTIO_PCI) += virtio-pci.o
hw-obj-y += fw_cfg.o
hw-obj-$(CONFIG_PCI) += pci.o pci_bridge.o
hw-obj-$(CONFIG_PCI) += msix.o msi.o
hw-obj-$(CONFIG_PCI) += pci_host.o pcie_host.o
hw-obj-$(CONFIG_PCI) += ioh3420.o xio3130_upstream.o xio3130_downstream.o
hw-obj-y += watchdog.o
hw-obj-$(CONFIG_ISA_MMIO) += isa_mmio.o
hw-obj-$(CONFIG_ECC) += ecc.o
hw-obj-$(CONFIG_NAND) += nand.o
hw-obj-$(CONFIG_PFLASH_CFI01) += pflash_cfi01.o
hw-obj-$(CONFIG_PFLASH_CFI02) += pflash_cfi02.o

hw-obj-$(CONFIG_M48T59) += m48t59.o
hw-obj-$(CONFIG_ESCC) += escc.o
hw-obj-$(CONFIG_EMPTY_SLOT) += empty_slot.o

hw-obj-$(CONFIG_SERIAL) += serial.o
hw-obj-$(CONFIG_PARALLEL) += parallel.o
hw-obj-$(CONFIG_I8254) += i8254.o
hw-obj-$(CONFIG_PCSPK) += pcspk.o
hw-obj-$(CONFIG_PCKBD) += pckbd.o
hw-obj-$(CONFIG_USB_UHCI) += usb-uhci.o
hw-obj-$(CONFIG_USB_OHCI) += usb-ohci.o
hw-obj-$(CONFIG_USB_EHCI) += usb-ehci.o
hw-obj-$(CONFIG_FDC) += fdc.o
hw-obj-$(CONFIG_ACPI) += acpi.o acpi_piix4.o
hw-obj-$(CONFIG_APM) += pm_smbus.o apm.o
hw-obj-$(CONFIG_DMA) += dma.o
hw-obj-$(CONFIG_HPET) += hpet.o
hw-obj-$(CONFIG_APPLESMC) += applesmc.o
hw-obj-$(CONFIG_SMARTCARD) += usb-ccid.o ccid-card-passthru.o
hw-obj-$(CONFIG_SMARTCARD_NSS) += ccid-card-emulated.o
hw-obj-$(CONFIG_USB_REDIR) += usb-redir.o
hw-obj-$(CONFIG_I8259) += i8259.o

# PPC devices
hw-obj-$(CONFIG_PREP_PCI) += prep_pci.o
# Mac shared devices
hw-obj-$(CONFIG_MACIO) += macio.o
hw-obj-$(CONFIG_CUDA) += cuda.o
hw-obj-$(CONFIG_ADB) += adb.o
hw-obj-$(CONFIG_MAC_NVRAM) += mac_nvram.o
hw-obj-$(CONFIG_MAC_DBDMA) += mac_dbdma.o
# OldWorld PowerMac
hw-obj-$(CONFIG_HEATHROW_PIC) += heathrow_pic.o
hw-obj-$(CONFIG_GRACKLE_PCI) += grackle_pci.o
# NewWorld PowerMac
hw-obj-$(CONFIG_UNIN_PCI) += unin_pci.o
hw-obj-$(CONFIG_DEC_PCI) += dec_pci.o
# PowerPC E500 boards
hw-obj-$(CONFIG_PPCE500_PCI) += ppce500_pci.o

# MIPS devices
hw-obj-$(CONFIG_PIIX4) += piix4.o
hw-obj-$(CONFIG_G364FB) += g364fb.o

# PCI watchdog devices
hw-obj-$(CONFIG_PCI) += wdt_i6300esb.o

hw-obj-$(CONFIG_PCI) += pcie.o pcie_aer.o pcie_port.o

# PCI network cards
hw-obj-$(CONFIG_NE2000_PCI) += ne2000.o
hw-obj-$(CONFIG_EEPRO100_PCI) += eepro100.o
hw-obj-$(CONFIG_PCNET_PCI) += pcnet-pci.o
hw-obj-$(CONFIG_PCNET_COMMON) += pcnet.o
hw-obj-$(CONFIG_E1000_PCI) += e1000.o
hw-obj-$(CONFIG_RTL8139_PCI) += rtl8139.o

hw-obj-$(CONFIG_SMC91C111) += smc91c111.o
hw-obj-$(CONFIG_LAN9118) += lan9118.o
hw-obj-$(CONFIG_NE2000_ISA) += ne2000-isa.o
hw-obj-$(CONFIG_OPENCORES_ETH) += opencores_eth.o

# SCSI layer
hw-obj-$(CONFIG_LSI_SCSI_PCI) += lsi53c895a.o
hw-obj-$(CONFIG_ESP) += esp.o

hw-obj-y += dma-helpers.o sysbus.o isa-bus.o
hw-obj-y += qdev-addr.o container.o

# VGA
hw-obj-$(CONFIG_VGA_PCI) += vga-pci.o
hw-obj-$(CONFIG_VGA_ISA) += vga-isa.o
hw-obj-$(CONFIG_VGA_ISA_MM) += vga-isa-mm.o
hw-obj-$(CONFIG_VMWARE_VGA) += vmware_vga.o
hw-obj-$(CONFIG_VMMOUSE) += vmmouse.o

hw-obj-$(CONFIG_RC4030) += rc4030.o
hw-obj-$(CONFIG_DP8393X) += dp8393x.o
hw-obj-$(CONFIG_DS1225Y) += ds1225y.o
hw-obj-$(CONFIG_MIPSNET) += mipsnet.o

# Sound
hw-obj-$(CONFIG_SB16) += sb16.o
hw-obj-$(CONFIG_ES1370) += es1370.o
hw-obj-$(CONFIG_AC97) += ac97.o
hw-obj-$(CONFIG_ADLIB) += fmopl.o adlib.o
hw-obj-$(CONFIG_GUS) += gus.o gusemu_hal.o gusemu_mixer.o
hw-obj-$(CONFIG_CS4231A) += cs4231a.o
hw-obj-$(CONFIG_HDA) += intel-hda.o hda-audio.o

adlib.o fmopl.o: QEMU_CFLAGS += -DBUILD_Y8950=0
