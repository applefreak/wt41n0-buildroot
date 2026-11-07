################################################################################
#
# lvgl
#
################################################################################

LVGL_VERSION = 9.2.2
LVGL_SITE = $(call github,lvgl,lvgl,v$(LVGL_VERSION))
LVGL_LICENSE = MIT
LVGL_LICENSE_FILES = LICENCE.txt
LVGL_INSTALL_STAGING = YES
LVGL_CONF_OPTS = \
	-DLV_CONF_PATH=$(LVGL_PKGDIR)/lv_conf.h \
	-DBUILD_SHARED_LIBS=ON \
	-DLV_CONF_BUILD_DISABLE_EXAMPLES=ON \
	-DLV_CONF_BUILD_DISABLE_DEMOS=ON \
	-DLV_CONF_BUILD_DISABLE_THORVG_INTERNAL=ON

$(eval $(cmake-package))
