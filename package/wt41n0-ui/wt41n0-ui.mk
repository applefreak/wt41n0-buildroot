################################################################################
#
# wt41n0-ui
#
################################################################################

WT41N0_UI_VERSION = 1
WT41N0_UI_SITE = $(BR2_EXTERNAL_WT41N0_PATH)/package/wt41n0-ui/src
WT41N0_UI_SITE_METHOD = local
WT41N0_UI_LICENSE = GPL-2.0, OFL-1.1 (fonts)
WT41N0_UI_LICENSE_FILES = fonts/OFL-sharetechmono.txt fonts/OFL-audiowide.txt fonts/OFL-chakrapetch.txt \
	fonts/OFL-aldrich.txt fonts/OFL-spacemono.txt fonts/OFL-dotgothic16.txt
WT41N0_UI_DEPENDENCIES = lvgl alsa-lib libmpdclient host-pkgconf
WT41N0_UI_INSTALL_STAGING = YES
WT41N0_UI_CONF_OPTS = -DWT41N0_UI_EXAMPLE=$(if $(BR2_PACKAGE_WT41N0_UI_EXAMPLE),ON,OFF)

define WT41N0_UI_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(WT41N0_UI_PKGDIR)/S30ui-settings $(TARGET_DIR)/etc/init.d/S30ui-settings
endef

$(eval $(cmake-package))
