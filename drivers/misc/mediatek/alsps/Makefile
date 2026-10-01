# Legacy (3.18) alsps framework — ported for m5c 4.9 (forge/sensors49).
# Only the stk3x1x chip driver is carried over from 3.18; all other
# obj-$(CONFIG_MTK_*) chip entries from 3.18 were dropped with their dirs.
# In case the platform does NOT support this type of sensors
ccflags-y += -I$(srctree)/drivers/misc/mediatek/hwmon/include
obj-y   +=  alsps.o
obj-y   +=  alsps_factory.o
obj-y   +=  aal_control.o

obj-$(CONFIG_MTK_STK3X1X)	+=  stk3x1x/
