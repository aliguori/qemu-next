all: build-all

include ../../config-host.mak
include $(SRC_PATH)/rules.mak

VPATH=$(SRC_PATH)/roms/multiboot

CPPFLAGS = -Wall -Wstrict-prototypes -Werror -fomit-frame-pointer -fno-builtin
CPPFLAGS += -I$(SRC_PATH)
CPPFLAGS += $(call cc-option, $(CFLAGS), -fno-stack-protector,"")

build-all: multiboot.bin

%.img: %.o
	$(call quiet-command,$(LD) -Ttext 0 -e _start -s -o $@ $<,"  Building $(TARGET_DIR)$@")

%.raw: %.img
	$(call quiet-command,$(OBJCOPY) -O binary -j .text $< $@,"  Building $(TARGET_DIR)$@")

%.bin: %.raw
	$(call quiet-command,$(SRC_PATH)/roms/multiboot/signrom.sh $< $@,"  Signing $(TARGET_DIR)$@")

clean:
	$(RM) *.o *.img *.bin *~
