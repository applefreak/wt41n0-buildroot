.syntax unified
.arm
.global loader_checksum
/* Must be the LAST object on the link line: the loader sums every word
   before this one and compares with the value patched in here. */
.align 2
loader_checksum:
    .word 0
