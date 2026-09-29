#
# Makefile for the kernel mobicore drivers
#

ifneq ($(MTK_ROOT_BUILD),)
include $(MTK_ROOT_BUILD)/Makefile
endif

GUD_ROOT_FOLDER := $(srctree)/drivers/misc/mediatek/gud/302c/gud/
# add our modules to kernel.
obj-$(CONFIG_TRUSTONIC_TEE_SUPPORT) += mcKernelApi.o
obj-$(CONFIG_TRUSTONIC_TEE_SUPPORT) += mcDrvModule.o
obj-$(CONFIG_TRUSTONIC_TRUSTED_UI) += TlcTui.o

mcDrvModule-objs := MobiCoreDriver/logging.o \
		MobiCoreDriver/ops.o \
		MobiCoreDriver/mem.o \
		MobiCoreDriver/api.o \
		MobiCoreDriver/pm.o \
		MobiCoreDriver/main.o

mcKernelApi-objs := MobiCoreKernelApi/main.o \
		MobiCoreKernelApi/clientlib.o \
		MobiCoreKernelApi/device.o \
		MobiCoreKernelApi/session.o \
		MobiCoreKernelApi/connection.o

TlcTui-objs := TlcTui/main.o \
		       TlcTui/tlcTui.o \
		       TlcTui/trustedui.o \
		       TlcTui/tui-hal_$(MTK_PLATFORM).o

include $(srctree)/drivers/misc/mediatek/gud/302c/gud/Makefile.flags
