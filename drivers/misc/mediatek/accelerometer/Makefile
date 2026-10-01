# Legacy (3.18) accelerometer framework — ported for m5c 4.9 (forge/sensors49).
# Only the mc3410-new chip driver is carried over from 3.18; all other
# obj-$(CONFIG_MTK_*) chip entries from 3.18 were dropped with their dirs.
# In case the platform does NOT support this type of sensors
ccflags-y += -I$(srctree)/drivers/misc/mediatek/hwmon/include

obj-y   +=  accel.o accel_factory.o

obj-$(CONFIG_MTK_MC3410_NEW)   +=  mc3410-new/
