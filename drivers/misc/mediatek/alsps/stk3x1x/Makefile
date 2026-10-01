ccflags-y += -I$(srctree)/drivers/misc/mediatek/alsps/inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/hwmon/include
# forge (m5c 4.9): dsm_sensor/ does not exist in this tree (nor in 3.18);
# all DSM code in stk3x1x.c is behind CONFIG_HUAWEI_DSM which stays off.
obj-y	:=  stk3x1x.o

