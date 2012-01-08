ui-obj-y += keymaps.o
ui-obj-$(CONFIG_SDL) += sdl.o sdl_zoom.o x_keymap.o
ui-obj-$(CONFIG_COCOA) += cocoa.o
ui-obj-$(CONFIG_CURSES) += curses.o
ui-obj-$(CONFIG_VNC) += vnc.o d3des.o
ui-obj-$(CONFIG_VNC) += vnc-enc-zlib.o vnc-enc-hextile.o
ui-obj-$(CONFIG_VNC) += vnc-enc-tight.o vnc-palette.o
ui-obj-$(CONFIG_VNC) += vnc-enc-zrle.o
ui-obj-$(CONFIG_VNC_TLS) += vnc-tls.o vnc-auth-vencrypt.o
ui-obj-$(CONFIG_VNC_SASL) += vnc-auth-sasl.o
ifdef CONFIG_VNC_THREAD
ui-obj-y += vnc-jobs-async.o
else
ui-obj-y += vnc-jobs-sync.o
endif
