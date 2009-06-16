#define NO_QEMU_PROTOS
#include "fw_cfg.h"

#define FW_CFG_CTL_PORT   0x510
#define FW_CFG_DATA_PORT  0x511

	.code16
.text
	.global _start
_start:	
	.short 0xaa55
	.byte (_end - _start) / 512
	push %eax
	push %ds

	/* setup ds so we can access the IVT */
	xor %ax, %ax
	mov %ax, %ds

	/* install out int 19 handler */
	movw $int19_handler, (0x19*4)
	mov %cs, (0x19*4+2)

	pop %ds
	pop %eax
	lret

/* squashes eax */
.macro fw_cfg_readl val
	push %ebx
	push %edx

	/* select port */
	movw $FW_CFG_CTL_PORT, %dx
	movw \val, %ax
	outw %ax, %dx

	xor %ebx, %ebx

	xor %eax, %eax
	movw $FW_CFG_DATA_PORT, %dx
	inb %dx, %al
	shl $24, %eax
	or %eax, %ebx

	xor %eax, %eax
	movw $FW_CFG_DATA_PORT, %dx
	inb %dx, %al
	shl $16, %eax
	or %eax, %ebx
	
	xor %eax, %eax
	movw $FW_CFG_DATA_PORT, %dx
	inb %dx, %al
	shl $8, %eax
	or %eax, %ebx
	
	xor %eax, %eax
	movw $FW_CFG_DATA_PORT, %dx
	inb %dx, %al
	or %eax, %ebx

	mov %ebx, %eax
	
	pop %edx
	pop %ebx
.endm
	
int19_handler:	
	cli
	cld

	/* cs = (kernel_addr >> 4) + 0x20 */
	fw_cfg_readl $FW_CFG_KERNEL_ADDR
	shrl $4, %eax
	addl $0x20, %eax

	/* ip = 0 */
	movw %ax, %cx

	/* esp = (cmdline_addr - kernel_addr - 16) */
	fw_cfg_readl $FW_CFG_KERNEL_ADDR
	mov %eax, %ebx

	fw_cfg_readl $FW_CFG_KERNEL_CMDLINE

	subl %ebx, %eax
	subl $16, %eax
	
	mov %eax, %esp

	pushw %ax
	pushw $0

	/* all other segs/gprs are 0 */
	
	xor %eax, %eax
	xor %ebx, %ebx
	xor %ecx, %ecx
	xor %edx, %edx
	xor %ebp, %ebp
	xor %edi, %edi
	xor %esi, %esi

	xor %ax, %ax
	mov %ax, %ss
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs

	/* execute kernel */
	lret

.align 512, 0
_end:	
