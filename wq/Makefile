obj-m         := wq.o

ARCH          := x86_64
CROSS_COMPILE :=

M  := $(shell pwd)
K  := $(shell uname -r)
KS := /lib/modules/$(K)/build

all:
	make -C $(KS) M=$(M) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
