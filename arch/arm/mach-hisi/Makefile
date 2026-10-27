# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Hisilicon processors family
#

CFLAGS_platmcpm.o	:= -march=armv7-a

obj-y	+= hisilicon.o
obj-$(CONFIG_MCPM)		+= platmcpm.o
obj-$(CONFIG_SMP)		+= platsmp.o hotplug.o

obj-$(CONFIG_MACH_HI3798MV100) += hi3798mv100.o
ifeq ($(CONFIG_MACH_HI3798MV100),y)
obj-$(CONFIG_SMP) += hi3798mv100-smp.o hi3798mv100-headsmp.o
endif
