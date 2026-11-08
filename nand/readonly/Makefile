obj-m := steamlink_marvell_nand.o steamlink_nand_overlay.o
steamlink_marvell_nand-y := drivers/mtd/nand/raw/marvell_nand.o
ccflags-y += -I$(src)

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
