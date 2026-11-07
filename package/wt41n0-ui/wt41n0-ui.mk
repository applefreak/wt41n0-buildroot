################################################################################
#
# wt41n0-ui
#
################################################################################

WT41N0_UI_VERSION = 1
WT41N0_UI_SITE = $(BR2_EXTERNAL_WT41N0_PATH)/package/wt41n0-ui/src
WT41N0_UI_SITE_METHOD = local
WT41N0_UI_LICENSE = GPL-2.0, OFL-1.1 (Share Tech Mono font)
WT41N0_UI_LICENSE_FILES = fonts/OFL.txt
WT41N0_UI_DEPENDENCIES = lvgl host-pkgconf
WT41N0_UI_INSTALL_STAGING = YES
WT41N0_UI_CONF_OPTS = -DWT41N0_UI_EXAMPLE=$(if $(BR2_PACKAGE_WT41N0_UI_EXAMPLE),ON,OFF)

$(eval $(cmake-package))
