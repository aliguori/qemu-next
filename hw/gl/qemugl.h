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

/* if defined, multiple guest processes can use the opengl "hardware"
 * simultaneously.
 */
#define QEMUGL_MULTITHREADED

#define QEMUGL_HWREG_REGIONBASE 0x4fff0000
#define QEMUGL_HWREG_REGIONSIZE 0x8000

/* global registers */
#define QEMUGL_GLOB_HWREG_PID 0x00
#define QEMUGL_GLOB_HWREG_SIZE 0x04 /* size of global register area */

/* local (per-process) registers */
#define QEMUGL_HWREG_FID 0x00 /* function identifier */
#define QEMUGL_HWREG_RSP 0x04 /* guest ptr for return string */
#define QEMUGL_HWREG_IAP 0x08 /* guest ptr for input args table */
#define QEMUGL_HWREG_IAS 0x0c /* guest ptr for input args size table */
#define QEMUGL_HWREG_CMD 0x10 /* command */
#define QEMUGL_HWREG_STA 0x14 /* status / integer return value */
#define QEMUGL_HWREG_BUF 0x18 /* opengl framebuffer access (i/o mode only) */
#define QEMUGL_HWREG_UNUSED 0x1c /* not used currently */
#define QEMUGL_HWREG_MASK 0x1f /* size mask for local register area */

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

#define QEMUGL_PID_SIGNATURE 0x51454d55 /* 'QEMU' */

#endif // QEMUGL_H__
