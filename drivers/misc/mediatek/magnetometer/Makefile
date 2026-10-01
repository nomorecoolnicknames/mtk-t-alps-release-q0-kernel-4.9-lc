# Legacy (3.18) magnetometer framework — ported for m5c 4.9 (forge/sensors49).
# Only the akm09912 chip driver is carried over from 3.18; all other
# obj-$(CONFIG_MTK_*) chip entries from 3.18 were dropped with their dirs.
# In case the platform does NOT support this type of sensors
ccflags-y += -I$(srctree)/drivers/misc/mediatek/hwmon/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/accelerometer/inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/magnetometer/inc
obj-y   +=  mag.o
obj-y   +=  mag_factory.o

obj-$(CONFIG_MTK_AKM09912)   +=  akm09912/
