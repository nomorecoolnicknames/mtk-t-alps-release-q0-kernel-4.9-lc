MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include \
					-I$(srctree)/drivers/misc/mediatek/include/mt-plat
ifeq ($(CONFIG_TRUSTONIC_TEE_SUPPORT),y)
subdir-ccflags-y += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc
endif

ifeq ($(CONFIG_TRUSTONIC_TEE_SUPPORT),y)
	ifeq ($(CONFIG_TRUSTONIC_TEE_VERSION),"302c")
ccflags-y += -I$(srctree)/drivers/misc/mediatek/gud/302c/gud/MobiCoreKernelApi/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/gud/302c/gud/MobiCoreKernelApi/public
else
include $(srctree)/drivers/misc/mediatek/gud/Makefile.include
endif
endif

obj-$(CONFIG_MTK_SPI) += spi.o spi-dev.o
