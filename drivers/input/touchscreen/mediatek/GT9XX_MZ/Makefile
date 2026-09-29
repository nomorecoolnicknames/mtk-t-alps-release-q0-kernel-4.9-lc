#
# GT9XX_MZ Makefile — stock Meizu m681 Goodix GT9xx driver,
# ported from the m681 3.10 tree for the graft 3.18.140 kernel.
# (No Makefile.custom on this tree; use explicit ccflags like ft5436/.)
#

ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/GT9XX_MZ/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/

obj-y	+=  gt9xx_driver.o
obj-y	+=  goodix_tool.o
obj-y	+=  gt9xx_update.o
