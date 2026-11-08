obj-m := steamlink_av_shmcc.o
steamlink_av_shmcc-y := module.o
ccflags-y += -I$(src)
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
