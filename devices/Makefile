devices-obj-$(CONFIG_DEVICE) := device.o pin.o
devices-obj-$(CONFIG_PCI) += pci_bus.o pci_device.o
devices-obj-$(CONFIG_ROM) += rom.o

include $(SRC_PATH)/devices/pc/Makefile
devices-obj-y += $(addprefix pc/,$(pc-obj-y))

include $(SRC_PATH)/devices/gates/Makefile
devices-obj-y += $(addprefix gates/,$(gates-obj-y))
