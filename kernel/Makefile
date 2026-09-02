# SPDX-License-Identifier: GPL-2.0-or-later

obj-m += odyssey_ddc.o

KVERSION ?= $(shell uname -r)
KDIR     ?= /lib/modules/$(KVERSION)/build
PWD      := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

.PHONY: all clean modules_install
