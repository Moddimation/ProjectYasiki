#include "dolphin/db/DBInterface.h"
#include "dolphin/hw_regs.h"
#include "dolphin/os/OSInterrupt.h"
#include <dolphin.h>
#include <types.h>

#define DB_STAT_SEND    0x1
#define DB_STAT_RECIEVE 0x2

#define ODEMU_ADDR_NNGC2PC   0x0001C000
#define ODEMU_ADDR_PC2NNGC   0x0001E000
#define ODEMU_OFFSET_NNGC2PC 0x00001000
#define ODEMU_OFFSET_PC2NNGC 0x00001000
#define ODEMU_MAIL_MAGIC     0x1F000000
#define ODEMU_MAIL_MASK      0x1fffffff

typedef void (*DBGCallbackType)(u32, OSContext*);

u8 EXIInputFlag;
u8* pEXIInputFlag;
s32 RecvDataLeng;
u32 SendMailData;
DBGCallbackType DBGCallback;
MTRCallbackType MTRCallback;

#pragma peephole off

void DBGEXIClearInterrupts(void) { __SIRegs[SI_EXILK] = 0; }

void DBGEXIInit(void)
{
	__OSMaskInterrupts(OS_INTERRUPTMASK_EXI_2);
	DBGEXIClearInterrupts();
	__EXIRegs[EXI_C2_SR] = 0;
}

BOOL DBGEXISelect(u32 v)
{
	u32 regs = __EXIRegs[EXI_C2_SR];
	regs &= 0x405;
	regs |= 0x80 | (v << 4);
	__EXIRegs[EXI_C2_SR] = regs;
	return TRUE;
}

BOOL DBGEXIDeselect(void)
{
	u32 regs = __EXIRegs[EXI_C2_SR];
	regs &= 0x405;
	__EXIRegs[EXI_C2_SR] = regs;
	return TRUE;
}

// NON_MATCHING
BOOL DBGEXISync(void)
{
	do {
	} while ((__EXIRegs[EXI_C2_CR] & 1) != 0);
	return TRUE;
}

// NON_MATCHING
BOOL DBGEXIImm(const void* data, s32 size, u32 mode)
{
	u32 writeVal;
	u32 readVal;
	int i;

	if (mode) {
		writeVal = 0;
		for (i = 0; i < size; ++i) {
			u8* nextWordPtr = (u8*)data + i;
			writeVal |= *nextWordPtr << ((3 - i) << 3);
		}
		__EXIRegs[14] = writeVal;
	}

	__EXIRegs[13] = 1 | (mode << 2) | ((size - 1U) << 4);

	DBGEXISync();
	if (!mode) {
		u8* dataPtr = (u8*)data;
		readVal     = __EXIRegs[14];
		for (i = 0; i < size; ++i) {
			*dataPtr++ = readVal >> ((3 - i) << 3);
		}
	}

	return TRUE;
}

BOOL DBGWriteMailbox(u32 v)
{

	BOOL err = FALSE;
	u32 value;

	if (!DBGEXISelect(4)) {
		return FALSE;
	}

	value = 0xc0000000 | (v & 0x1fffffff);
	err   = !DBGEXIImm(&value, 4, 1);
	err |= !DBGEXISync();
	err |= !DBGEXIDeselect();

	return !err;
}

BOOL DBGReadMailbox(u32* v)
{

	BOOL err = FALSE;
	u32 value;

	if (!DBGEXISelect(4)) {
		return FALSE;
	}

	value = 0x60000000;
	err   = !DBGEXIImm(&value, 2, 1);
	err |= !DBGEXISync();
	err |= !DBGEXIImm(v, 4, 0);
	err |= !DBGEXISync();
	err |= !DBGEXIDeselect();

	return !err;
}

// void DBGCheckID(void) { }

// NON_MATCHING
BOOL DBGWrite(u32 addr, const void* data, s32 size)
{
	BOOL err     = FALSE;
	u32* dataPtr = (u32*)data;
	u32 value;
	u32 nextWord;

	if (!DBGEXISelect(4)) {
		return FALSE;
	}

	value = 0xa0000000 | ((addr << 8) & 0x01fffc00);
	err   = !DBGEXIImm((u8*)&value, sizeof(value), 1);
	err |= !DBGEXISync();

	while (size != 0) {
		nextWord = *dataPtr++;

		err |= !DBGEXIImm((u8*)&nextWord, sizeof(nextWord), 1);
		err |= !DBGEXISync();

		size -= 4;
		if (size < 0)
			size = 0;
	}

	err |= !DBGEXIDeselect();

	return !err;
}

// NON_MATCHING
BOOL DBGRead(u32 addr, const u32* data, s32 byte_size)
{
	BOOL err     = FALSE;
	u32* dataPtr = (u32*)data;
	u32 writeValue;
	u32 readValue;

	if (!DBGEXISelect(4))
		return FALSE;

	writeValue = 0x20000000 | ((addr << 8) & 0x01fffc00); // TODO: enum
	err        = !DBGEXIImm(&writeValue, 4, 1);
	err |= !DBGEXISync();

	while (byte_size != 0) {
		err |= !DBGEXIImm(&readValue, 4, 0);
		err |= !DBGEXISync();

		*dataPtr++ = readValue;

		byte_size -= 4;
		if (byte_size < 0) {
			byte_size = 0;
		}
	}
	err |= !DBGEXIDeselect();

	return !err;
}

BOOL DBGReadStatus(u32* status)
{
	BOOL err;
	u32 cmd;

	if (!DBGEXISelect(4))
		return FALSE;

	cmd = 0x40000000;
	err = !DBGEXIImm(&cmd, 2, 1); // TODO: Enums: 1 = Write, 0 = Read
	err |= !DBGEXISync();
	err |= !DBGEXIImm(status, 4, 0);
	err |= !DBGEXISync();
	err |= !DBGEXIDeselect();

	return !err;
}

void MWCallback(u32 a, OSContext* b)
{
	EXIInputFlag = TRUE;
	if (MTRCallback != nullptr)
		MTRCallback(0);
}

void DBGHandler(s16 a, OSContext* b)
{
	__PIRegs[PI_INTSR] = 0x1000;

	if (DBGCallback != nullptr)
		DBGCallback(a, b);
}

void DBInitComm(u8** a, MTRCallbackType b)
{
	BOOL interr = OSDisableInterrupts();
	{
		pEXIInputFlag = &EXIInputFlag;
		*a            = pEXIInputFlag;
		MTRCallback   = b;
		DBGEXIInit();
	}
	OSRestoreInterrupts(interr);
}

void DBInitInterrupts(void)
{
	__OSMaskInterrupts(OS_INTERRUPTMASK_EXI_2);
	__OSMaskInterrupts(OS_INTERRUPTMASK_PI_DEBUG);
	DBGCallback = &MWCallback;
	__OSSetInterruptHandler(__OS_INTERRUPT_PI_DEBUG, DBGHandler);
	__OSUnmaskInterrupts(OS_INTERRUPTMASK_PI_DEBUG);
}

void CheckMailBox(void)
{
	u32 v;
	DBGReadStatus(&v);
	if (v & 1) {
		DBGReadMailbox(&v);
		v &= ODEMU_MAIL_MASK;

		if ((v & ODEMU_MAIL_MAGIC) == ODEMU_MAIL_MAGIC) {
			SendMailData = v;
			RecvDataLeng = v & 0x7fff;
			EXIInputFlag = 1;
		}
	}
}

u32 DBQueryData()
{
	BOOL irq;
	EXIInputFlag = FALSE;
	if (RecvDataLeng == 0) {
		irq = OSDisableInterrupts();
		CheckMailBox();
	}
	OSRestoreInterrupts(irq);
	return RecvDataLeng;
}

BOOL DBRead(const u32* buffer, s32 count)
{
	BOOL irq = OSDisableInterrupts();
	u32 v    = SendMailData & 0x10000 ? ODEMU_OFFSET_PC2NNGC : 0;
	v += ODEMU_ADDR_PC2NNGC;

	DBGRead(v, buffer, ALIGN_NEXT(count, 4));

	RecvDataLeng = 0;
	EXIInputFlag = 0;

	OSRestoreInterrupts(irq);

	return 0;
}

inline void __DBWaitForSendMail(u32* busyFlag)
{
	do {
		DBGReadStatus(busyFlag);
	} while (*busyFlag & DB_STAT_RECIEVE);
}

inline void __DBWaitForSendMailSafe(u32* busyFlag)
{
	do {
		while (!DBGReadStatus(busyFlag))
			;
	} while (*busyFlag & DB_STAT_RECIEVE);
}

// NON_MATCHING
int DBWrite(const s32* data, u32 size)
{
	u32 value;
	u32 busyFlag;
	BOOL enabled;
	static u8 SendCount = 0x80;

	enabled = OSDisableInterrupts();

	__DBWaitForSendMail(&busyFlag);

	++SendCount;

	value = ((SendCount & 1) ? 0x1000 : 0);
	while (!DBGWrite(value | 0x1c000, data, ALIGN_NEXT(size, 4)))
		;

	__DBWaitForSendMail(&busyFlag);

	value = ODEMU_MAIL_MAGIC | SendCount << 0x10 | size;
	while (!DBGWriteMailbox(value))
		;

	__DBWaitForSendMailSafe(&busyFlag);

	OSRestoreInterrupts(enabled);

	return 0;
}

void DBOpen(void) { }

void DBClose(void) { }

