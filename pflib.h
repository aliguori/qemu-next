/* public */
typedef struct QemuPfConv QemuPfConv;

QemuPfConv *qemu_pf_conv_get(PixelFormat *dst, PixelFormat *src);
void qemu_pf_conv_run(QemuPfConv *conv, void *dst, void *src, uint32_t cnt);
void qemu_pf_conv_put(QemuPfConv *conv);
