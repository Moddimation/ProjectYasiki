.include "macros.s"

.section .init, "ax"  # 0x80003100 - 0x800054C0 ; 0x000023C0

.global TRK_memcpy
TRK_memcpy:
/* 80003464 00000464  38 84 FF FF */	addi r4, r4, -0x1
/* 80003468 00000468  38 C3 FF FF */	addi r6, r3, -0x1
/* 8000346C 0000046C  38 A5 00 01 */	addi r5, r5, 0x1
/* 80003470 00000470  48 00 00 0C */	b lbl_8000347C
lbl_80003474:
/* 80003474 00000474  8C 04 00 01 */	lbzu r0, 0x1(r4)
/* 80003478 00000478  9C 06 00 01 */	stbu r0, 0x1(r6)
lbl_8000347C:
/* 8000347C 0000047C  34 A5 FF FF */	addic. r5, r5, -0x1
/* 80003480 00000480  40 82 FF F4 */	bne lbl_80003474
/* 80003484 00000484  4E 80 00 20 */	blr

.global TRK_memset
TRK_memset:
/* 80003488 00000488  7C 08 02 A6 */	mflr r0
/* 8000348C 0000048C  90 01 00 04 */	stw r0, 0x4(r1)
/* 80003490 00000490  94 21 FF F0 */	stwu r1, -0x10(r1)
/* 80003494 00000494  93 E1 00 0C */	stw r31, 0xc(r1)
/* 80003498 00000498  7C 7F 1B 78 */	mr r31, r3
/* 8000349C 0000049C  48 20 5B 35 */	bl func_80208FD0
/* 800034A0 000004A0  7F E3 FB 78 */	mr r3, r31
/* 800034A4 000004A4  83 E1 00 0C */	lwz r31, 0xc(r1)
/* 800034A8 000004A8  38 21 00 10 */	addi r1, r1, 0x10
/* 800034AC 000004AC  80 01 00 04 */	lwz r0, 0x4(r1)
/* 800034B0 000004B0  7C 08 03 A6 */	mtlr r0
/* 800034B4 000004B4  4E 80 00 20 */	blr
