include $(sort $(wildcard $(BR2_EXTERNAL_WT41N0_PATH)/package/*/*.mk))

# Debug boot profile: add the kernel options for the LCD low-level debug
# console on top of the normal configuration, right before the kernel build.
ifeq ($(BR2_WT41N0_BOOT_DEBUG),y)
define WT41N0_LINUX_DEBUG_CONFIG
	$(LINUX_DIR)/scripts/kconfig/merge_config.sh -m -O $(LINUX_DIR) \
		$(LINUX_DIR)/.config \
		$(BR2_EXTERNAL_WT41N0_PATH)/board/zebra/wt41n0/linux-debug.fragment
	$(LINUX_MAKE_ENV) $(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) olddefconfig
endef
LINUX_PRE_BUILD_HOOKS += WT41N0_LINUX_DEBUG_CONFIG
endif
