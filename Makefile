# Comment/uncomment the following line to disable/enable debugging
DEBUG ?= y
# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
	DEBFLAGS = -O -g -DCONFIG_MORSE_DEBUG -DDEBUG # "-O" is needed to expand inlines
else
	DEBFLAGS = -O2
endif

# Set 0 to a version number. This is done to match the Linux expectations
#override MORSE_VERSION = "0-rel_mm6108_2_0_1_2026_Jun_11"
MORSE_VERSION ?= "0-rel_mm6108_2_0_1_2026_Jun_11"

USING_CLANG := $(shell $(CC) -v 2>&1 | grep -c "clang version")

ccflags-$(CONFIG_MORSE_USER_ACCESS) += "-DCONFIG_MORSE_USER_ACCESS"
ccflags-y += $(DEBFLAGS) -Wall -Werror
ccflags-y += -Wno-address-of-packed-member
ifeq ($(USING_CLANG), 0)
ccflags-y += -Wno-stringop-truncation -Wmaybe-uninitialized -Wno-missing-attributes
endif
ccflags-y += "-DMORSE_VERSION=$(MORSE_VERSION)"
ccflags-y += -I$(KERNEL_SRC)
ccflags-$(CONFIG_MORSE_SDIO) += "-DCONFIG_MORSE_SDIO"
ccflags-$(CONFIG_MORSE_SPI) += "-DCONFIG_MORSE_SPI"
ccflags-$(CONFIG_MORSE_USB) += "-DCONFIG_MORSE_USB"
ccflags-$(CONFIG_MORSE_VENDOR_COMMAND) += "-DCONFIG_MORSE_VENDOR_COMMAND"
ccflags-$(CONFIG_MORSE_DEBUGFS) += "-DCONFIG_MORSE_DEBUGFS"
ccflags-$(CONFIG_MORSE_ENABLE_TEST_MODES) += "-DCONFIG_MORSE_ENABLE_TEST_MODES"
ccflags-$(CONFIG_MORSE_HW_TRACE) += "-DCONFIG_MORSE_HW_TRACE"
ccflags-$(CONFIG_MORSE_DEBUG_IRQ) += "-DCONFIG_MORSE_DEBUG_IRQ"
ccflags-$(CONFIG_MORSE_DEBUG_TXSTATUS) += "-DCONFIG_MORSE_DEBUG_TXSTATUS"
ccflags-$(CONFIG_MORSE_IPMON) += "-DCONFIG_MORSE_IPMON"
ccflags-$(CONFIG_MORSE_MONITOR) += "-DCONFIG_MORSE_MONITOR"
ccflags-$(CONFIG_MORSE_HW_BOOT_TIMEBASE) += "-DCONFIG_MORSE_HW_BOOT_TIMEBASE"
ccflags-$(CONFIG_MORSE_TRACE_LOG_MSG) += -D"CONFIG_MORSE_TRACE_LOG_MSG"
ccflags-$(CONFIG_MORSE_TRACE_BUS) += "-DCONFIG_MORSE_TRACE_BUS"
ccflags-$(CONFIG_MORSE_TRACE_HW_IRQ) += "-DCONFIG_MORSE_TRACE_HW_IRQ"
ccflags-$(CONFIG_MORSE_TRACE_PS) += "-DCONFIG_MORSE_TRACE_PS"
ccflags-$(CONFIG_MORSE_TRACE_PAGER_HW) += "-DCONFIG_MORSE_TRACE_PAGER_HW"
ccflags-$(CONFIG_MORSE_TRACE_PAGESET) += "-DCONFIG_MORSE_TRACE_PAGESET"
ccflags-$(CONFIG_MORSE_TRACE_RX) += "-DCONFIG_MORSE_TRACE_RX"
ccflags-$(CONFIG_MORSE_TRACE_HEADLESS) += "-DCONFIG_MORSE_TRACE_HEADLESS"
ccflags-$(CONFIG_MORSE_TRACE_SUSPEND) += "-DCONFIG_MORSE_TRACE_SUSPEND"
ccflags-$(CONFIG_ANDROID) += "-DCONFIG_ANDROID"

ifneq ($(CONFIG_BACKPORT_VERSION),)
# Convert a version string from "vX.Y.Z-stuff" into an integer for comparison with KERNEL_VERSION
ccflags-y += "-DMAC80211_BACKPORT_VERSION_CODE=$(shell echo "$(CONFIG_BACKPORT_VERSION)" | \
		awk -F[v.-] '// {printf("%u", lshift($$2, 16) + lshift($$3, 8) + $$4)}')"
endif
ifneq ($(LINUX_BACKPORT_VERSION_CODE),)
#error 1
endif

CONFIG_MORSE_COUNTRY ?= "AU"
ccflags-y += "-DCONFIG_MORSE_COUNTRY=\"$(CONFIG_MORSE_COUNTRY)\""

# Default debug_mask to MORSE_MSG_ERR
CONFIG_MORSE_DEBUG_MASK ?= 8
ccflags-y += "-DCONFIG_MORSE_DEBUG_MASK=$(CONFIG_MORSE_DEBUG_MASK)"

CONFIG_MORSE_SDIO_RESET_TIME ?= 400
ccflags-y += "-DCONFIG_MORSE_SDIO_RESET_TIME=$(CONFIG_MORSE_SDIO_RESET_TIME)"

# Default enable_ps to POWERSAVE_MODE_FULLY_ENABLED
CONFIG_MORSE_POWERSAVE_MODE ?= 2
ccflags-y += "-DCONFIG_MORSE_POWERSAVE_MODE=$(CONFIG_MORSE_POWERSAVE_MODE)"

CONFIG_MORSE_SDIO_ALIGNMENT ?= 2
ccflags-y += "-DCONFIG_MORSE_SDIO_ALIGNMENT=$(CONFIG_MORSE_SDIO_ALIGNMENT)"

# Default enable_wiphy to ENABLE_WIPHY
CONFIG_MORSE_ENABLE_WIPHY ?= 0
ccflags-y += "-DCONFIG_MORSE_ENABLE_WIPHY=$(CONFIG_MORSE_ENABLE_WIPHY)"

# Default reattach_hw to REATACH_HW
CONFIG_MORSE_REATTACH_HW ?= 0
ccflags-y += "-DCONFIG_MORSE_REATTACH_HW=$(CONFIG_MORSE_REATTACH_HW)"

# Default dhcpc_lease_update_script to DHCPC_LEASE_UPDATE_SCRIPT
CONFIG_MORSE_DHCPC_LEASE_UPDATE_SCRIPT ?= "/morse/scripts/dhcpc_update.sh"
ccflags-y += "-DCONFIG_MORSE_DHCPC_LEASE_UPDATE_SCRIPT=\"$(CONFIG_MORSE_DHCPC_LEASE_UPDATE_SCRIPT)\""

ifneq ($(CONFIG_DISABLE_MORSE_FULLMAC),y)
	ccflags-y += "-DCONFIG_MORSE_FULLMAC"
endif
ifneq ($(CONFIG_DISABLE_MORSE_RC),y)
	ccflags-y += "-DCONFIG_MORSE_RC"
ifeq ($(CONFIG_ANDROID),y)
	ccflags-y += "-I$(srctree)/$(src)/mmrc"
else
	ccflags-y += "-I$(src)/mmrc"
endif
endif

ifeq ($(CONFIG_MORSE_DHCP_OFFLOAD),y)
	ccflags-y += "-DENABLE_DHCP_OFFLOAD_DEFAULT=1"
else
	ccflags-y += "-DENABLE_DHCP_OFFLOAD_DEFAULT=0"
endif

ifeq ($(CONFIG_MORSE_ARP_OFFLOAD),y)
	ccflags-y += "-DENABLE_ARP_OFFLOAD_DEFAULT=1"
else
	ccflags-y += "-DENABLE_ARP_OFFLOAD_DEFAULT=0"
endif

ifeq ($(CONFIG_MORSE_WATCHDOG_RESET_DEFAULT_DISABLED),y)
	ccflags-y += "-DENABLE_WATCHDOG_DEFAULT=0"
else
	ccflags-y += "-DENABLE_WATCHDOG_DEFAULT=1"
endif

ifeq ($(CONFIG_MORSE_DISABLE_CHANNEL_SURVEY),y)
	ccflags-y += "-DENABLE_SURVEY_DEFAULT=0"
else
	ccflags-y += "-DENABLE_SURVEY_DEFAULT=1"
endif

ccflags_trace.o := -I$(src)
CFLAGS_trace.o := -I$(src)

ifeq ($(MORSE_TRACE_PATH),)
	MORSE_TRACE_PATH := .
endif

ifneq ($(MORSE_TRACE_PATH),)
	ccflags-y += -DMORSE_TRACE_PATH=$(MORSE_TRACE_PATH)
endif

obj-$(CONFIG_WLAN_VENDOR_MORSE) += mm6108_sdio.o dot11ah/

mm6108_sdio-y = mac.o
mm6108_sdio-y += init.o
mm6108_sdio-y += skbq.o
mm6108_sdio-y += debug.o
mm6108_sdio-y += trace.o
mm6108_sdio-y += mm8108.o
mm6108_sdio-y += mm6108.o
mm6108_sdio-y += command.o
mm6108_sdio-y += hw.o
mm6108_sdio-y += beacon.o
mm6108_sdio-y += pageset.o
mm6108_sdio-y += pager_hw.o
mm6108_sdio-y += ps.o
mm6108_sdio-y += raw.o
mm6108_sdio-y += twt.o
mm6108_sdio-y += cac.o
mm6108_sdio-y += ndpprobe.o
mm6108_sdio-y += of.o
mm6108_sdio-y += firmware.o
mm6108_sdio-y += yaps.o
mm6108_sdio-y += yaps_hw.o
mm6108_sdio-y += watchdog.o
mm6108_sdio-y += event.o
mm6108_sdio-y += crc16_xmodem.o
mm6108_sdio-y += offload.o
mm6108_sdio-y += vendor_ie.o
mm6108_sdio-y += bus_test.o
mm6108_sdio-y += ocs.o
mm6108_sdio-y += mbssid.o
mm6108_sdio-y += mem_access.o
mm6108_sdio-y += mesh.o
mm6108_sdio-y += page_slicing.o
mm6108_sdio-y += pv1.o
mm6108_sdio-y += hw_scan.o
mm6108_sdio-y += coredump.o
mm6108_sdio-y += peer.o
mm6108_sdio-y += led.o
mm6108_sdio-y += bss_stats.o
mm6108_sdio-y += hw_beacon.o
mm6108_sdio-y += sysfs.o
mm6108_sdio-y += scan_result_cache.o
mm6108_sdio-$(CONFIG_PM) += wowlan.o
mm6108_sdio-$(CONFIG_MORSE_MONITOR) += monitor.o
mm6108_sdio-$(CONFIG_MORSE_SDIO) += sdio.o
mm6108_sdio-$(CONFIG_MORSE_SPI) += spi.o
mm6108_sdio-$(CONFIG_MORSE_USB) += usb.o
mm6108_sdio-$(CONFIG_MORSE_VENDOR_COMMAND) += vendor.o
mm6108_sdio-$(CONFIG_MORSE_USER_ACCESS) += uaccess.o
mm6108_sdio-$(CONFIG_MORSE_HW_TRACE) += hw_trace.o
mm6108_sdio-$(CONFIG_ANDROID) += apf.o

ifneq ($(CONFIG_DISABLE_MORSE_FULLMAC),y)
	mm6108_sdio-y += wiphy.o
endif
ifeq ($(CONFIG_DISABLE_MORSE_RC),y)
	mm6108_sdio-y += minstrel_rc.o
else
	mm6108_sdio-y += mmrc/mmrc_osal.o
	mm6108_sdio-y += mmrc-submodule/src/core/mmrc.o
	mm6108_sdio-y += rc.o
	mm6108_sdio-y += mmrc_debugfs.o
endif

SRC := $(shell pwd)

all:
	$(MAKE) MORSE_VERSION=$(MORSE_VERSION) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) MORSE_VERSION=$(MORSE_VERSION) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	rm -f  $(mm6108_sdio-y) *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
	make -C ./dot11ah clean
