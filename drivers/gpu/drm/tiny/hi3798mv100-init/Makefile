# Local SDK source is compiled into the initialization object; no binary blob is tracked.
obj-$(CONFIG_HI3798MV100_DISPLAY_INIT) += mv100_display_bridge.o
mv100_display_bridge-y := display-linux-adapter.o display-init-lib.o
targets += display-init-lib.o
quiet_cmd_mv100_sdk = MV100SDK $@
      cmd_mv100_sdk = python3 $(srctree)/hi3798/display-init/build-display-init-port.py --raw-only --out $(obj)/vendor && cp $(obj)/vendor/display-init-lib.o_shipped $@
$(obj)/display-init-lib.o: $(srctree)/hi3798/display-init/build-display-init-port.py $(srctree)/hi3798/display-init/display-pdm-defaults.c FORCE
	$(call if_changed,mv100_sdk)
