################################################################################
#
# wt41n0-utils
#
################################################################################

WT41N0_UTILS_VERSION = 1
WT41N0_UTILS_SITE = $(BR2_EXTERNAL_WT41N0_PATH)/package/wt41n0-utils/src
WT41N0_UTILS_SITE_METHOD = local
WT41N0_UTILS_LICENSE = GPL-2.0

define WT41N0_UTILS_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -o $(@D)/pcmtest $(@D)/pcmtest.c -lm
endef

define WT41N0_UTILS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pcmtest $(TARGET_DIR)/usr/bin/pcmtest
	$(INSTALL) -D -m 0755 $(@D)/wt41n0-fpga $(TARGET_DIR)/usr/sbin/wt41n0-fpga
	$(INSTALL) -D -m 0755 $(@D)/wt41n0-survey $(TARGET_DIR)/usr/sbin/wt41n0-survey
	$(INSTALL) -D -m 0755 $(@D)/keys $(TARGET_DIR)/usr/bin/keys
endef

$(eval $(generic-package))
