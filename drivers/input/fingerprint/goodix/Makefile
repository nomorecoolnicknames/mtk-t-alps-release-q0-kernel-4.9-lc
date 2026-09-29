#
# Makefile for the fingerprint device.
#

# Each configuration option enables a list of files.
#include $(srctree)/drivers/misc/mediatek/Makefile.custom

# m681: MTK_PLATFORM is normally set only inside drivers/misc/mediatek - this
# dir builds from drivers/input, so derive it here or the -I resolves empty.
MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))

ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include

ifeq ($(CONFIG_TRUSTONIC_TEE_SUPPORT),y)
subdir-ccflags-y += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc
	ifeq ($(CONFIG_TRUSTONIC_TEE_VERSION),"302c")
ccflags-y += -I$(srctree)/drivers/misc/mediatek/gud/302c/gud/MobiCoreKernelApi/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/gud/302c/gud/MobiCoreKernelApi/public
else
include $(srctree)/drivers/misc/mediatek/gud/Makefile.include
endif
endif


obj-$(CONFIG_GOODIX_FINGERPRINT)	+= gf_spi_tee.o
