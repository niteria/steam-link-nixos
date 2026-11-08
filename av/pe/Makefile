obj-m += steamlink_pe.o
steamlink_pe-y := pe_driver.o avio_dhub_drv.o hal_dhub.o agent.o mmio.o
ccflags-y += -I$(src) -I$(src)/sdk/include -I$(src)/sdk/modules/pe -I$(src)/sdk/modules/pe/gsinc
ccflags-y += -I$(src)/sdk/modules/pe/gsinc/Firmware_Berlin_BG2CD_A0 -I$(src)/sdk/modules/nfc
ccflags-y += -I$(src)/sdk/modules/cc -I$(src)/sdk/modules/shm -include $(src)/compat.h
ccflags-y += -DBERLIN -DSoC_Galois -D__LINUX_KERNEL__ -D__CODE_LINK__=0 -DCPUINDEX=0 -DBERLIN_SINGLE_CPU
ccflags-y += -DBERLIN_B_0=0x1000 -DBERLIN_C_0=0x2000 -DBERLIN_BG2=0x3000 -DBERLIN_BG2_Z2=0x3010
ccflags-y += -DBERLIN_BG2_A0=0x3020 -DBERLIN_BG2CD_A0=0x3100 -DBERLIN_BG3=0x4000 -DBERLIN_CHIP_VERSION=0x3100
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
