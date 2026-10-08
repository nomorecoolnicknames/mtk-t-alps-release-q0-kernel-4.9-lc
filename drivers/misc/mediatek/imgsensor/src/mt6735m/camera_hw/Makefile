#$(warning "CONFIG_TABLET_PROJECT:$(CONFIG_TABLET_PROJECT)")
ifeq ($(CONFIG_TABLET_PROJECT),y)
# is tablet project
obj-y				+= tb_kd_camera_hw.o
else
# not tablet project
obj-y				+= kd_camera_hw.o
endif

# forge (m5c): front-camera test kernels pick their experiment here, e.g.
# make ... FORGE_S5K5E8_FLAGS="-DFORGE_S5K5E8_PDN_DEFAULT=1" (see kd_camera_hw.c).
CFLAGS_kd_camera_hw.o += $(FORGE_S5K5E8_FLAGS)
