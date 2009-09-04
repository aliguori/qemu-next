#ifndef QEMUGL_H__
#define QEMUGL_H__

/* if defined, qemu host code and guest libgl are built to communicate via
 * a separate guest kernel module -- otherwise a direct connection is used
 * (requires root uid in libgl).
 */
#define QEMUGL_MODULE

/* if defined, the opengl frame buffer is owned by the qemu host and accessed
 * through mmio.
 * if undefined, the frame buffer is owned by the guest and qemu host draws
 * directly into it.
 */
//#define QEMUGL_IO_FRAMEBUFFER

#define QEMUGL_HWREG_REGIONBASE 0x4fff0000
#define QEMUGL_HWREG_REGIONSIZE 0x100

#define QEMUGL_HWREG_FID 0x00
#define QEMUGL_HWREG_PID 0x04
#define QEMUGL_HWREG_RSP 0x08
#define QEMUGL_HWREG_IAP 0x0c
#define QEMUGL_HWREG_IAS 0x10
#define QEMUGL_HWREG_CMD 0x14
#define QEMUGL_HWREG_STA 0x18
#define QEMUGL_HWREG_BUF 0x1c

#define QEMUGL_HWCMD_RESET  0xfeedcafe
#define QEMUGL_HWCMD_GLCALL 0xdeadbeef
#define QEMUGL_HWCMD_SETBUF 0x10adda7a

#define QEMUGL_DEVICE_NAME "qemugl"
#define QEMUGL_DEVICE_MAJOR 123
#define QEMUGL_DEVICE_MINOR 'G'

#define QEMUGL_FIORNCMD 0xd0dadeed
#define QEMUGL_FIORDSTA 0xc01d1ead
#define QEMUGL_FIOCPBUF 0xfeedbeef
#define QEMUGL_FIOSTBUF 0xbabe0000

#endif // QEMUGL_H__
