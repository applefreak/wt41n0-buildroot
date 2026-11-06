################################################################################
#
# wt41n0-loader
#
################################################################################

WT41N0_LOADER_VERSION = 3
WT41N0_LOADER_SITE = $(BR2_EXTERNAL_WT41N0_PATH)/package/wt41n0-loader/src
WT41N0_LOADER_SITE_METHOD = local
WT41N0_LOADER_LICENSE = GPL-2.0
WT41N0_LOADER_INSTALL_TARGET = NO
WT41N0_LOADER_INSTALL_IMAGES = YES

# Linked for the address of the monitor's staging copy of the OS entry point
# (0x80268000 + 0x11880). loader_checksum.o must stay last: it holds the word
# the loader checks itself against, which mkhex.py fills in.
WT41N0_LOADER_LINK_ADDRESS = 0x80279880
WT41N0_LOADER_OBJS = loader.o lcd_text.o loader_checksum.o
WT41N0_LOADER_ASFLAGS = -march=armv7-a -mfpu=vfpv3-d16 -marm -nostdinc -I$(@D)

# Screen: progress lines by default, the register-dump screen for the debug
# boot profile.
ifeq ($(BR2_WT41N0_BOOT_DEBUG),y)
WT41N0_LOADER_ASFLAGS += -DLOADER_DIAGNOSTIC
endif

define WT41N0_LOADER_BUILD_CMDS
	$(foreach o,$(WT41N0_LOADER_OBJS),\
		$(TARGET_CC) $(WT41N0_LOADER_ASFLAGS) -c -o $(@D)/$(o) $(@D)/$(o:.o=.S)$(sep))
	$(TARGET_LD) -Ttext=$(WT41N0_LOADER_LINK_ADDRESS) -e 0 --no-warn-rwx-segments \
		-o $(@D)/loader.elf $(addprefix $(@D)/,$(WT41N0_LOADER_OBJS))
	$(TARGET_OBJCOPY) -O binary -j .text $(@D)/loader.elf $(@D)/wt41n0-loader.bin
endef

define WT41N0_LOADER_INSTALL_IMAGES_CMDS
	$(INSTALL) -D -m 0644 $(@D)/wt41n0-loader.bin $(BINARIES_DIR)/wt41n0-loader.bin
endef

$(eval $(generic-package))
