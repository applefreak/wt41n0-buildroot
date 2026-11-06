.syntax unified
.arm
.global draw_string
.global draw_hex32
.global fill_screen

draw_string:
    push {r4-r12, lr}
    mov r8, r0
    mov r9, r1
    mov r10, r2
string_loop:
    ldrb r0, [r8], #1
    cmp r0, #0
    beq string_done
    mov r1, r9
    mov r2, r10
    bl draw_char
    add r10, r10, #1
    b string_loop
string_done:
    pop {r4-r12, lr}
    bx lr

draw_hex32:
    push {r4-r12, lr}
    mov r8, r0
    mov r9, r1
    mov r10, r2
    mov r11, #28
    mov r12, #8
hex_loop:
    mov r0, r8, lsr r11
    and r0, r0, #0xF
    cmp r0, #9
    addls r0, r0, #'0'
    addhi r0, r0, #'A' - 10
    mov r1, r9
    mov r2, r10
    bl draw_char
    add r10, r10, #1
    sub r11, r11, #4
    subs r12, r12, #1
    bne hex_loop
    pop {r4-r12, lr}
    bx lr

draw_char:
    push {r4-r12, lr}
    cmp r0, #32
    blo invalid_char
    cmp r0, #126
    bls valid_char
invalid_char:
    mov r0, #'?'
valid_char:
    sub r0, r0, #32
    ldr r6, =font8x8
    add r6, r6, r0, lsl #3
    mov r4, r1, lsl #3
    mov r5, r2, lsl #3
    mov r7, #0
font_row:
    ldrb r9, [r6, r7]
    mov r8, #0
font_pixel:
    tst r9, #0x80
    beq skip_pixel
    add r10, r5, r8
    ldr r11, =319
    sub r10, r11, r10
    add r11, r4, r7
    lsl r11, r11, #1
    ldr r12, =480
    mul r0, r10, r12
    add r0, r0, r11
    ldr r11, =0x9B000000
    add r0, r0, r11
    ldr r12, =0xFFFF
    strh r12, [r0]
skip_pixel:
    lsl r9, r9, #1
    add r8, r8, #1
    cmp r8, #8
    blo font_pixel
    add r7, r7, #1
    cmp r7, #8
    blo font_row
    pop {r4-r12, lr}
    bx lr

fill_screen:
    ldr r4, =0x9B000000
    ldr r5, =0x9B025800
fill_loop:
    strh r6, [r4], #2
    cmp r4, r5
    blo fill_loop
    dmb sy
    bx lr

.align 2
/* Glyphs 32-126 of the Linux kernel's 8x8 console font (lib/fonts/font_8x8.c,
 * GPL-2.0), one byte per row, most significant bit = leftmost pixel. */
font8x8:
    .incbin "font8x8-ascii.bin"
