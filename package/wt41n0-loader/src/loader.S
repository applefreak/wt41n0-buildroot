.syntax unified
.arm
.arch_extension sec
.global _start
.extern draw_string
.extern draw_hex32
.extern fill_screen
.extern loader_checksum

/*
 * Two screen modes, chosen at build time:
 *   default            black screen with one progress line per step: image
 *                      sizes, L2 cache, each copy, kernel start
 *   LOADER_DIAGNOSTIC  blue screen with image sizes and a register dump, and a
 *                      colour change per copy stage
 * Fatal errors always put a coloured screen with a message on the LCD.
 */

/*
 * The Symbol monitor stages the OS image at 0x80268000 and places a run copy
 * at 0x81168000, jumping to the OS entry 0x81179880. Only the first 0x780
 * bytes of the run copy are reliable, so this loader is linked for the
 * STAGING copy of itself (0x80268000 + 0x11880 = 0x80279880) and the entry
 * stub jumps there. See docs/boot-flow.md.
 */
.equ staging_base, 0x80268000
.equ entry_offset, 0x11880
.equ link_base, staging_base + entry_offset
.equ blob_base, staging_base + 0x20000
.equ kernel_addr, 0x82008000
.equ dtb_addr, 0x88000000
.equ initrd_addr, 0x89000000
.equ stack_top, 0x81100000
.equ pl310_base, 0x48242000
.equ lcd_state, 0x9B025800

.align 5
exception_vectors:
    ldr pc, reset_address       /* literal kept here: only the first 0x780 bytes
                                   of the monitor's run copy are intact */
    b undefined_instruction
    b supervisor_call
    b prefetch_abort
    b data_abort
    nop
    b irq_exception
    b fiq_exception
reset_address:
    .word reset

reset:
    cpsid if
    cps #0x13
    ldr sp, =stack_top

    /* Self-check: word sum of this image (staging copy) must equal the
       value mkhex.py stores in loader_checksum (the last word of the image).
       On failure, fill orange and halt (the font may be unusable). */
    ldr r3, =exception_vectors
    ldr r4, =loader_checksum
    mov r5, #0
selfcheck_loop:
    ldr r6, [r3], #4
    add r5, r5, r6
    cmp r3, r4
    blo selfcheck_loop
    ldr r6, [r4]
    cmp r5, r6
    bne loader_corrupt

    /* Reset the kernel LCD debug console cursor (col,row) past the fb. */
    ldr r3, =lcd_state
    mov r4, #0
    str r4, [r3]
    str r4, [r3, #4]
    mrc p15, 0, r0, c1, c0, 0
    bic r0, r0, #0x2000
    mcr p15, 0, r0, c1, c0, 0
    isb sy
    ldr r0, =exception_vectors
    mcr p15, 0, r0, c12, c0, 0
    isb sy

#ifdef LOADER_DIAGNOSTIC
    ldr r6, =0x001F
    bl fill_screen
    ldr r0, =header_text
    bl draw_stage
#else
    mov r6, #0
    bl fill_screen
    ldr r0, =title_text
    bl draw_stage
#endif

    /* Blob header: three little-endian u32 lengths (zImage, DTB, initramfs). */
    ldr r12, =blob_base
    ldr r7, [r12, #0]
    ldr r8, [r12, #4]
    ldr r9, [r12, #8]

    ldr r0, =zimage_len_text
    mov r1, #1
    mov r2, #0
    bl draw_string
    mov r0, r7
    mov r1, #1
    mov r2, #12
    bl draw_hex32
    ldr r0, =dtb_len_text
    mov r1, #2
    mov r2, #0
    bl draw_string
    mov r0, r8
    mov r1, #2
    mov r2, #12
    bl draw_hex32
    ldr r0, =initrd_len_text
    mov r1, #3
    mov r2, #0
    bl draw_string
    mov r0, r9
    mov r1, #3
    mov r2, #12
    bl draw_hex32

    /* Sanity limits: 1 MiB < zImage <= 16 MiB, 4 KiB < DTB <= 1 MiB,
       4 KiB < initramfs <= 64 MiB. */
    cmp r7, #0x100000
    bls bad_header
    cmp r7, #0x1000000
    bhi bad_header
    cmp r8, #0x1000
    bls bad_header
    cmp r8, #0x100000
    bhi bad_header
    cmp r9, #0x1000
    bls bad_header
    cmp r9, #0x4000000
    bhi bad_header

    /* PL310 L2 cache: show state, then clean+invalidate and disable it
       if the bootloader left it on. Linux's decompressor only maintains
       L1, so a live L2 with dirty lines makes the kernel start randomly. */
#ifdef LOADER_DIAGNOSTIC
    ldr r0, =l2_text
    mov r1, #5
    mov r2, #0
    bl draw_string
    ldr r10, =pl310_base
    ldr r0, [r10, #0x100]
    mov r1, #5
    mov r2, #9
    bl draw_hex32
    ldr r0, =aux_text
    mov r1, #5
    mov r2, #18
    bl draw_string
    ldr r0, [r10, #0x104]
    mov r1, #5
    mov r2, #24
    bl draw_hex32
#endif
    ldr r10, =pl310_base
    ldr r0, [r10, #0x100]
    tst r0, #1
    beq l2_done
    ldr r0, =0xFFFF
    str r0, [r10, #0x7FC]
l2_clean_wait:
    ldr r0, [r10, #0x7FC]
    cmp r0, #0
    bne l2_clean_wait
    mov r0, #0
    str r0, [r10, #0x730]
l2_sync_wait:
    ldr r0, [r10, #0x730]
    cmp r0, #0
    bne l2_sync_wait
    dsb sy
    /* OMAP4 ROM secure service 0x102: set PL310 control register. */
    mov r12, #0x102
    mov r0, #0
    dsb sy
    smc #0
#ifdef LOADER_DIAGNOSTIC
    ldr r10, =pl310_base
    ldr r0, =l2_off_text
    mov r1, #6
    mov r2, #0
    bl draw_string
    ldr r0, [r10, #0x100]
    mov r1, #6
    mov r2, #9
    bl draw_hex32
#endif
l2_done:
#ifndef LOADER_DIAGNOSTIC
    ldr r0, =l2_progress_text
    mov r1, #5
    bl draw_progress
#endif

#ifdef LOADER_DIAGNOSTIC
    /* DRAM size evidence: DMM LISA section map and EMIF SDRAM_CONFIG. */
    ldr r10, =0x4E000040
    ldr r0, =dmm0_text
    mov r1, #10
    mov r2, #0
    bl draw_string
    ldr r0, [r10, #0]
    mov r1, #10
    mov r2, #7
    bl draw_hex32
    ldr r0, =dmm1_text
    mov r1, #10
    mov r2, #16
    bl draw_string
    ldr r0, [r10, #4]
    mov r1, #10
    mov r2, #23
    bl draw_hex32
    ldr r0, =dmm2_text
    mov r1, #11
    mov r2, #0
    bl draw_string
    ldr r0, [r10, #8]
    mov r1, #11
    mov r2, #7
    bl draw_hex32
    ldr r0, =dmm3_text
    mov r1, #11
    mov r2, #16
    bl draw_string
    ldr r0, [r10, #12]
    mov r1, #11
    mov r2, #23
    bl draw_hex32
    ldr r0, =emif_text
    mov r1, #12
    mov r2, #0
    bl draw_string
    ldr r10, =0x4C000008
    ldr r0, [r10]
    mov r1, #12
    mov r2, #7
    bl draw_hex32
    ldr r10, =0x4D000008
    ldr r0, [r10]
    mov r1, #12
    mov r2, #18
    bl draw_hex32

    /* USB state as the Symbol monitor left it (it just enumerated over this port):
       row 13: USBOTGHS_CONTROL, CONTROL_DEV_CONF (control module, always on)
       row 14: CM_L3INIT_USB_OTG_CLKCTRL, CM_L3INIT_USBPHY_CLKCTRL, CM_IDLEST_DPLL_USB,
               CM_L3INIT_USB_HOST_HS_CLKCTRL. MUSB registers are not read: the block may be
               clock gated and the read aborts (seen on hardware). */
    ldr r0, =usb_text
    mov r1, #13
    mov r2, #0
    bl draw_string
    ldr r10, =0x4A00233C
    ldr r0, [r10]                       /* USBOTGHS_CONTROL */
    mov r1, #13
    mov r2, #4
    bl draw_hex32
    ldr r10, =0x4A002300
    ldr r0, [r10]                       /* CONTROL_DEV_CONF */
    mov r1, #13
    mov r2, #13
    bl draw_hex32
    ldr r0, =cm_text
    mov r1, #14
    mov r2, #0
    bl draw_string
    ldr r10, =0x4A009360
    ldr r0, [r10]
    mov r1, #14
    mov r2, #4
    bl draw_hex32
    ldr r10, =0x4A0093E0
    ldr r0, [r10]
    mov r1, #14
    mov r2, #13
    bl draw_hex32
    ldr r10, =0x4A008184
    ldr r0, [r10]
    mov r1, #14
    mov r2, #22
    bl draw_hex32
    ldr r10, =0x4A009358
    ldr r0, [r10]                       /* CM_L3INIT_USB_HOST_HS_CLKCTRL */
    mov r1, #14
    mov r2, #31
    bl draw_hex32

    /* FPGA as the Symbol monitor left it (GPMC CS1 at 0x02000000 set up by the
       x-loader): row 15 = S+0x08 (IRQ status A), S+0x10, X+0x10 (ready flag),
       Y+0x04. Real values here but address echo under Linux => Linux kills the FPGA. */
    ldr r0, =fpga_text
    mov r1, #15
    mov r2, #0
    bl draw_string
    ldr r10, =0x50000000
    ldr r0, [r10, #0xA8]                /* CS1 CONFIG7 as found */
    mov r1, #15
    mov r2, #5
    bl draw_hex32
    ldr r10, =0x02020000
    ldrh r0, [r10, #0x08]
    ldrh r3, [r10, #0x10]
    orr r0, r0, r3, lsl #16             /* S+0x10 : S+0x08 */
    mov r1, #15
    mov r2, #14
    bl draw_hex32
    ldr r10, =0x02080000
    ldrh r0, [r10, #0x10]
    ldr r10, =0x020C0000
    ldrh r3, [r10, #0x04]
    orr r0, r0, r3, lsl #16             /* Y+0x04 : X+0x10 */
    mov r1, #15
    mov r2, #23
    bl draw_hex32

    /* GPIO banks 1..6 as the bootloader left them: row 17 = OE (1 = input) for
       banks 1,2,3,4 ... too wide; print OE of all 6 on rows 17-18 and DATAOUT on
       rows 19-20 (4 per row, bank order 1 2 3 4 / 5 6). */
    ldr r0, =oe_text
    mov r1, #17
    mov r2, #0
    bl draw_string
    ldr r10, =0x4A310134
    ldr r0, [r10]
    mov r1, #17
    mov r2, #3
    bl draw_hex32
    ldr r10, =0x48055134
    ldr r0, [r10]
    mov r1, #17
    mov r2, #12
    bl draw_hex32
    ldr r10, =0x48057134
    ldr r0, [r10]
    mov r1, #17
    mov r2, #21
    bl draw_hex32
    ldr r10, =0x48059134
    ldr r0, [r10]
    mov r1, #17
    mov r2, #30
    bl draw_hex32
    ldr r10, =0x4805B134
    ldr r0, [r10]
    mov r1, #18
    mov r2, #3
    bl draw_hex32
    ldr r10, =0x4805D134
    ldr r0, [r10]
    mov r1, #18
    mov r2, #12
    bl draw_hex32
    ldr r0, =out_text
    mov r1, #19
    mov r2, #0
    bl draw_string
    ldr r10, =0x4A31013C
    ldr r0, [r10]
    mov r1, #19
    mov r2, #3
    bl draw_hex32
    ldr r10, =0x4805513C
    ldr r0, [r10]
    mov r1, #19
    mov r2, #12
    bl draw_hex32
    ldr r10, =0x4805713C
    ldr r0, [r10]
    mov r1, #19
    mov r2, #21
    bl draw_hex32
    ldr r10, =0x4805913C
    ldr r0, [r10]
    mov r1, #19
    mov r2, #30
    bl draw_hex32
    ldr r10, =0x4805B13C
    ldr r0, [r10]
    mov r1, #20
    mov r2, #3
    bl draw_hex32
    ldr r10, =0x4805D13C
    ldr r0, [r10]
    mov r1, #20
    mov r2, #12
    bl draw_hex32

    ldr r0, =kernel_copy_text
    mov r1, #22
    mov r2, #0
    bl draw_string
#else
    ldr r0, =kernel_copy_text
    mov r1, #6
    bl draw_progress
#endif
    ldr r12, =blob_base
    add r3, r12, #12
    mov r5, r7
    ldr r4, =kernel_addr
    bl copy

#ifdef LOADER_DIAGNOSTIC
    ldr r6, =0xFFE0
    bl fill_screen
    ldr r0, =dtb_copy_text
    bl draw_stage
#else
    ldr r0, =dtb_copy_text
    mov r1, #7
    bl draw_progress
#endif
    mov r5, r8
    ldr r4, =dtb_addr
    bl copy

#ifdef LOADER_DIAGNOSTIC
    ldr r6, =0xF81F
    bl fill_screen
    ldr r0, =initrd_copy_text
    bl draw_stage
#else
    ldr r0, =initrd_copy_text
    mov r1, #8
    bl draw_progress
#endif
    mov r5, r9
    ldr r4, =initrd_addr
    bl copy

#ifdef LOADER_DIAGNOSTIC
    ldr r6, =0x07FF
    bl fill_screen
    ldr r0, =copies_done_text
    bl draw_stage
#endif

    ldr r4, =kernel_addr
    ldr r5, [r4]
    ldr r6, =0xE1A00000
    cmp r5, r6
    bne bad_kernel_start
    ldr r5, [r4, #0x24]
    ldr r6, =0x016F2818
    cmp r5, r6
    bne bad_kernel_marker

    ldr r4, =dtb_addr
    ldr r5, [r4]
    ldr r6, =0xEDFE0DD0
    cmp r5, r6
    bne bad_dtb

    ldr r4, =initrd_addr
    ldrh r5, [r4]
    ldr r6, =0x8B1F
    cmp r5, r6
    bne bad_initrd

#ifdef LOADER_DIAGNOSTIC
    ldr r6, =0x07E0
    bl fill_screen
    ldr r0, =jump_text
    bl draw_stage
#else
    ldr r0, =jump_text
    mov r1, #9
    bl draw_progress
#endif

    bl prepare_kernel_entry

#ifdef LOADER_DIAGNOSTIC
    ldr r6, =0xFFFF
    bl fill_screen
#endif

    mov r0, #0
    mov r1, #0xFFFFFFFF
    ldr r2, =dtb_addr
    ldr pc, =kernel_addr

prepare_kernel_entry:
    mrc p15, 1, r0, c0, c0, 1
    ands r3, r0, #0x07000000
    lsr r3, r3, #23
    beq cache_clean_done
    mov r10, #0
cache_level_loop:
    add r2, r10, r10, lsr #1
    lsr r1, r0, r2
    and r1, r1, #7
    cmp r1, #2
    blt cache_level_next
    mcr p15, 2, r10, c0, c0, 0
    mov r1, #0
    mcr p15, 0, r1, c7, c5, 4
    mrc p15, 1, r1, c0, c0, 0
    and r2, r1, #7
    add r2, r2, #4
    ldr r4, =0x3FF
    ands r4, r4, r1, lsr #3
    clz r5, r4
    ldr r7, =0x7FFF
    ands r7, r7, r1, lsr #13
cache_way_loop:
    mov r9, r4
cache_set_loop:
    orr r11, r10, r9, lsl r5
    orr r11, r11, r7, lsl r2
    mcr p15, 0, r11, c7, c14, 2
    subs r9, r9, #1
    bge cache_set_loop
    subs r7, r7, #1
    bge cache_way_loop
cache_level_next:
    add r10, r10, #2
    cmp r3, r10
    bgt cache_level_loop
cache_clean_done:
    mov r10, #0
    mcr p15, 2, r10, c0, c0, 0
    dsb sy

    mrc p15, 0, r0, c1, c0, 0
    bic r0, r0, #0x0005
    mcr p15, 0, r0, c1, c0, 0
    dsb sy
    isb sy

    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    mcr p15, 0, r0, c7, c5, 6
    mcr p15, 0, r0, c8, c7, 0
    dsb sy
    isb sy
    bx lr

copy:
    subs r5, r5, #1
    bcc copy_done
    ldrb r6, [r3], #1
    strb r6, [r4], #1
    b copy
copy_done:
    bx lr

draw_stage:
    mov r1, #0
    mov r2, #0
    b draw_string

/* One progress line: r0 = text, r1 = row. Keeps r3, the copy source pointer
   that runs on from one image to the next. */
draw_progress:
    push {r3, lr}
    mov r2, #0
    bl draw_string
    pop {r3, lr}
    bx lr

undefined_instruction:
    ldr r0, =undefined_text
    ldr r6, =0x4208
    b capture_fault
supervisor_call:
    ldr r0, =svc_text
    ldr r6, =0x4208
    b capture_fault
prefetch_abort:
    ldr r0, =prefetch_text
    ldr r6, =0xFFFF
    b capture_fault
data_abort:
    ldr r0, =data_abort_text
    ldr r6, =0xF800
    b capture_fault
irq_exception:
    ldr r0, =irq_text
    ldr r6, =0xFD20
    b capture_fault
fiq_exception:
    ldr r0, =fiq_text
    ldr r6, =0xFD20

capture_fault:
    mov r8, r0
    mov r9, r6
    ldr r10, =fault_regs
    mrc p15, 0, r0, c5, c0, 0
    str r0, [r10, #0]
    mrc p15, 0, r0, c6, c0, 0
    str r0, [r10, #4]
    str lr, [r10, #8]
    str r3, [r10, #12]
    str r4, [r10, #16]
    str r5, [r10, #20]
    mov r6, r9
    bl fill_screen
    mov r0, r8
    bl draw_stage
    ldr r0, =dfsr_text
    mov r1, #3
    bl draw_fault_value
    ldr r0, =dfar_text
    mov r1, #4
    bl draw_fault_value
    ldr r0, =abort_lr_text
    mov r1, #5
    bl draw_fault_value
    ldr r0, =source_text
    mov r1, #6
    bl draw_fault_value
    ldr r0, =destination_text
    mov r1, #7
    bl draw_fault_value
    ldr r0, =remaining_text
    mov r1, #8
    bl draw_fault_value
fault_loop:
    b fault_loop

draw_fault_value:
    push {r0, r1, lr}
    mov r2, #0
    bl draw_string
    pop {r0, r1, lr}
    sub r3, r1, #3
    ldr r0, [r10, r3, lsl #2]
    mov r2, #10
    b draw_hex32

loader_corrupt:
    ldr r6, =0xFC00
    bl fill_screen
corrupt_loop:
    b corrupt_loop

bad_header:
    ldr r0, =bad_header_text
    ldr r6, =0xF800
    b show_error
bad_kernel_start:
    ldr r0, =bad_kernel_start_text
    ldr r6, =0xF800
    b show_error
bad_kernel_marker:
    ldr r0, =bad_kernel_marker_text
    ldr r6, =0xFD20
    b show_error
bad_dtb:
    ldr r0, =bad_dtb_text
    ldr r6, =0xF81F
    b show_error
bad_initrd:
    ldr r0, =bad_initrd_text
    ldr r6, =0x07FF
show_error:
    bl fill_screen
    ldr r0, =bad_image_text
    bl draw_stage
error_loop:
    b error_loop

.align 2
title_text:
    .asciz "WT41N0 Linux loader"
l2_progress_text:
    .asciz "L2 cache cleaned and disabled"
header_text:
    .asciz "WT41N0 LINUX LOADER"
l2_text:
    .asciz "L2CTRL 0x"
aux_text:
    .asciz "AUX 0x"
l2_off_text:
    .asciz "L2 OFF 0x"
dmm0_text:
    .asciz "DMM0 0x"
dmm1_text:
    .asciz "DMM1 0x"
dmm2_text:
    .asciz "DMM2 0x"
dmm3_text:
    .asciz "DMM3 0x"
emif_text:
    .asciz "EMIF 0x"
usb_text:
    .asciz "USB "
cm_text:
    .asciz "CM  "
fpga_text:
    .asciz "FPGA "
oe_text:
    .asciz "OE "
out_text:
    .asciz "DO "
zimage_len_text:
    .asciz "zImage    0x"
dtb_len_text:
    .asciz "DTB       0x"
initrd_len_text:
    .asciz "initramfs 0x"
kernel_copy_text:
    .asciz "Copying kernel"
dtb_copy_text:
    .asciz "Copying device tree"
initrd_copy_text:
    .asciz "Copying initramfs"
copies_done_text:
    .asciz "ALL COPIES OK"
jump_text:
    .asciz "Starting kernel"
undefined_text:
    .asciz "UNDEFINED INSTRUCTION"
svc_text:
    .asciz "UNEXPECTED SVC"
prefetch_text:
    .asciz "PREFETCH ABORT"
data_abort_text:
    .asciz "DATA ABORT"
irq_text:
    .asciz "UNEXPECTED IRQ"
fiq_text:
    .asciz "UNEXPECTED FIQ"
dfsr_text:
    .asciz "DFSR=0x"
dfar_text:
    .asciz "DFAR=0x"
abort_lr_text:
    .asciz "ABORT LR=0x"
source_text:
    .asciz "SRC R3=0x"
destination_text:
    .asciz "DST R4=0x"
remaining_text:
    .asciz "LEFT R5=0x"
bad_image_text:
    .asciz "IMAGE VALIDATION FAILED"
bad_header_text:
    .asciz "BAD BLOB HEADER"
bad_kernel_start_text:
    .asciz "BAD KERNEL START"
bad_kernel_marker_text:
    .asciz "BAD KERNEL MARKER"
bad_dtb_text:
    .asciz "BAD DTB"
bad_initrd_text:
    .asciz "BAD INITRD"

.align 2
fault_regs:
    .word 0, 0, 0, 0, 0, 0

