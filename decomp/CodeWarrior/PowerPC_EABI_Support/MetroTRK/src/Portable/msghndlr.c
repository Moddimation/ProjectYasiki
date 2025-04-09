#include "Portable/msghndlr.h"
#include "Portable/nubevent.h"
#include "Portable/msgbuf.h"
#include "Portable/msg.h"
#include "Os/dolphin/targcont.h"
#include "Processor/ppc/Generic/targimpl.h"
#include "trk.h"

static void TRKMessageIntoReply(TRKBuffer* buffer, u8 ackCmd,
                                DSReplyError errSentInAck)
{
	TRKResetBuffer(buffer, 1);

	TRKAppendBuffer1_ui8(buffer, ackCmd);
	TRKAppendBuffer1_ui8(buffer, errSentInAck);
}

DSError TRKSendACK(TRKBuffer* buffer)
{
	DSError err;
	int ackTries;

	ackTries = 3;
	do {
		err = TRKMessageSend((TRK_Msg*)buffer);
		--ackTries;
	} while (err != DS_NoError && ackTries > 0);

	return err;
}

DSError TRKStandardACK(TRKBuffer* buffer, MessageCommandID commandID,
                       DSReplyError replyError)
{
	TRKMessageIntoReply(buffer, commandID, replyError);
	TRKSendACK(buffer);
	return DS_NoError;
}

DSError TRKDoUnsupported(TRKBuffer* buffer)
{
	return TRKStandardACK(buffer, DSMSG_ReplyACK,
	                      DSREPLY_UnsupportedCommandError);
}

DSError TRKDoConnect(TRKBuffer* buffer)
{
	return TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_NoError);
}

DSError TRKDoDisconnect(TRKBuffer* buffer)
{
	DSError error = DS_NoError;
	TRKEvent event;

	if ((error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_NoError))
	    == DS_NoError) {
		TRKConstructEvent(&event, 1);
		TRKPostEvent(&event);
	}
	return error;
}

DSError TRKDoReset(TRKBuffer* buffer)
{
	TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_NoError);
	__TRK_reset();
	return DS_NoError;
}

DSError TRKDoVersions(TRKBuffer* buffer)
{
	DSError error;
	DSVersions versions;

	if (buffer->length != 1) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_PacketSizeError);
	} else {
		TRKMessageIntoReply(buffer, DSMSG_ReplyACK, DSREPLY_NoError);
		error = TRKTargetVersions(&versions);

		if (error == DS_NoError)
			error = TRKAppendBuffer1_ui8(buffer, versions.kernelMajor);
		if (error == DS_NoError)
			error = TRKAppendBuffer1_ui8(buffer, versions.kernelMinor);
		if (error == DS_NoError)
			error = TRKAppendBuffer1_ui8(buffer, versions.protocolMajor);
		if (error == DS_NoError)
			error = TRKAppendBuffer1_ui8(buffer, versions.protocolMinor);

		if (error != DS_NoError)
			error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_CWDSError);
		else
			error = TRKSendACK(buffer);
	}
	return error;
}

DSError TRKDoSupportMask(TRKBuffer* buffer)
{
	DSError error;
	u8 mask[32];

	if (buffer->length == 1) {
		TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_PacketSizeError);
	} else {
		TRKMessageIntoReply(buffer, DSMSG_ReplyACK, DSREPLY_NoError);
		error = TRKTargetSupportMask(mask);

		if (error == DS_NoError)
			error = TRKAppendBuffer(buffer, mask, 32);
		if (error == DS_NoError)
			error = TRKAppendBuffer1_ui8(buffer, 2);

		if (error != DS_NoError)
			TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_CWDSError);
		else
			TRKSendACK(buffer);
	}
}

DSError TRKDoCPUType(TRKBuffer* buffer)
{
	DSError error;
	DSCPUType cputype;

	if (buffer->length != 1) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_PacketSizeError);
		return error;
	}

	TRKMessageIntoReply(buffer, DSMSG_ReplyACK, DSREPLY_NoError);

	error = TRKTargetCPUType(&cputype);

	if (error == DS_NoError)
		error = TRKAppendBuffer1_ui8(buffer, cputype.cpuMajor);
	if (error == DS_NoError)
		error = TRKAppendBuffer1_ui8(buffer, cputype.cpuMinor);
	if (error == DS_NoError)
		error = TRKAppendBuffer1_ui8(buffer, cputype.bigEndian);
	if (error == DS_NoError)
		error = TRKAppendBuffer1_ui8(buffer, cputype.defaultTypeSize);
	if (error == DS_NoError)
		error = TRKAppendBuffer1_ui8(buffer, cputype.fpTypeSize);
	if (error == DS_NoError)
		error = TRKAppendBuffer1_ui8(buffer, cputype.extended1TypeSize);
	if (error == DS_NoError)
		error = TRKAppendBuffer1_ui8(buffer, cputype.extended2TypeSize);

	if (error != DS_NoError)
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_CWDSError);
	else
		error = TRKSendACK(buffer);

	return error;
}

DSError TRKDoReadMemory(TRKBuffer* buffer)
{
	DSError error;
	DSReplyError replyError;
	u8 tempBuf[0x800];
	u32 length;
	u32 msg_start;
	u16 msg_length;
	u8 msg_options;
	u8 msg_command;

	if (buffer->length != 8) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_PacketSizeError);
		return error;
	}

	TRKSetBufferPosition(buffer, DSREPLY_NoError);
	error = TRKReadBuffer1_ui8(buffer, &msg_command);
	if (error == DS_NoError)
		error = TRKReadBuffer1_ui8(buffer, &msg_options);

	if (error == DS_NoError)
		error = TRKReadBuffer1_ui16(buffer, &msg_length);

	if (error == DS_NoError)
		error = TRKReadBuffer1_ui32(buffer, &msg_start);

	if (msg_options & 2) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK,
		                       DSREPLY_UnsupportedOptionError);
		return error;
	}

	if (msg_length > 0x800) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_ParameterError);
		return error;
	}

	TRKMessageIntoReply(buffer, DSMSG_ReplyACK, DSREPLY_NoError);

	if (error == DS_NoError) {
		length = (u32)msg_length;
		error  = TRKTargetAccessMemory(
            tempBuf, msg_start, &length,
            (msg_options & 8) ? MEMACCESS_UserMemory : MEMACCESS_DebuggerMemory,
            1);
		msg_length = (u16)length;
		if (error == DS_NoError)
			error = TRKAppendBuffer1_ui16(buffer, msg_length);
		if (error == DS_NoError)
			error = TRKAppendBuffer(buffer, tempBuf, length);
	}

	if (error != DS_NoError) {
		switch (error) {
		case DS_CWDSException:
			replyError = DSREPLY_CWDSException;
			break;
		case DS_InvalidMemory:
			replyError = DSREPLY_InvalidMemoryRange;
			break;
		case DS_InvalidProcessID:
			replyError = DSREPLY_InvalidProcessID;
			break;
		case DS_InvalidThreadID:
			replyError = DSREPLY_InvalidThreadID;
			break;
		case DS_OSError:
			replyError = DSREPLY_OSError;
			break;
		default:
			replyError = DSREPLY_CWDSError;
			break;
		}
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, replyError);
	} else {
		error = TRKSendACK(buffer);
	}

	return error;
}

DSError TRKDoWriteMemory(TRKBuffer* buffer)
{
	DSError error;
	DSReplyError replyError;
	u8 tmpBuffer[0x800];
	u32 length;
	u32 msg_start;
	u16 msg_length;
	u8 msg_options;
	u8 msg_command;

	if (buffer->length <= 8) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_PacketSizeError);
		return error;
	}

	TRKSetBufferPosition(buffer, DSREPLY_NoError);
	error = TRKReadBuffer1_ui8(buffer, &msg_command);
	if (error == DS_NoError)
		error = TRKReadBuffer1_ui8(buffer, &msg_options);

	if (error == DS_NoError)
		error = TRKReadBuffer1_ui16(buffer, &msg_length);

	if (error == DS_NoError)
		error = TRKReadBuffer1_ui32(buffer, &msg_start);

	if (msg_options & 2) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK,
		                       DSREPLY_UnsupportedOptionError);
		return error;
	}

	if ((buffer->length != msg_length + 8) || (msg_length > 0x800)) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_ParameterError);
	} else {
		if (error == DS_NoError) {
			length = (u32)msg_length;
			error  = TRKReadBuffer(buffer, tmpBuffer, length);
			if (error == DS_NoError) {
				error = TRKTargetAccessMemory(tmpBuffer, msg_start, &length,
				                              (msg_options & 8)
				                                  ? MEMACCESS_UserMemory
				                                  : MEMACCESS_DebuggerMemory,
				                              FALSE);
			}
			msg_length = (u16)length;
		}

		if (error == DS_NoError)
			TRKMessageIntoReply(buffer, DSMSG_ReplyACK, DSREPLY_NoError);

		if (error == DS_NoError)
			error = TRKAppendBuffer1_ui16(buffer, msg_length);

		if (error != DS_NoError) {
			switch (error) {
			case DS_CWDSException:
				replyError = DSREPLY_CWDSException;
				break;
			case DS_InvalidMemory:
				replyError = DSREPLY_InvalidMemoryRange;
				break;
			case DS_InvalidProcessID:
				replyError = DSREPLY_InvalidProcessID;
				break;
			case DS_InvalidThreadID:
				replyError = DSREPLY_InvalidThreadID;
				break;
			case DS_OSError:
				replyError = DSREPLY_OSError;
				break;
			default:
				replyError = DSREPLY_CWDSError;
				break;
			}
			error = TRKStandardACK(buffer, DSMSG_ReplyACK, replyError);
		} else {
			error = TRKSendACK(buffer);
		}
	}

	return error;
}

DSError TRKDoReadRegisters(TRKBuffer* buffer)
{
	DSError error;
	DSReplyError replyError;
	DSMessageRegisterOptions options;
	u32 registerDataLength;
	u16 msg_lastRegister;
	u16 msg_firstRegister;
	u8 msg_options;
	u8 msg_command;

	if (buffer->length != 6) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_PacketSizeError);
		return error;
	}
	TRKSetBufferPosition(buffer, DSREPLY_NoError);
	error = TRKReadBuffer1_ui8(buffer, &msg_command);
	if (error == DS_NoError)
		error = TRKReadBuffer1_ui8(buffer, &msg_options);

	if (error == DS_NoError)
		error = TRKReadBuffer1_ui16(buffer, &msg_firstRegister);

	if (error == DS_NoError)
		error = TRKReadBuffer1_ui16(buffer, &msg_lastRegister);

	if (msg_firstRegister > msg_lastRegister) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK,
		                       DSREPLY_InvalidRegisterRange);
		return error;
	}

	if (error == DS_NoError)
		TRKMessageIntoReply(buffer, DSMSG_ReplyACK, DSREPLY_NoError);

	options = (DSMessageRegisterOptions)msg_options;
	switch (options) {
	case DSREG_Default:
		error = TRKTargetAccessDefault(msg_firstRegister, msg_lastRegister,
		                               buffer, &registerDataLength, TRUE);
		break;
	case DSREG_FP:
		error = TRKTargetAccessFP(msg_firstRegister, msg_lastRegister, buffer,
		                          &registerDataLength, TRUE);
		break;
	case DSREG_Extended1:
		error = TRKTargetAccessExtended1(msg_firstRegister, msg_lastRegister,
		                                 buffer, &registerDataLength, TRUE);
		break;
	case DSREG_Extended2:
		error = TRKTargetAccessExtended2(msg_firstRegister, msg_lastRegister,
		                                 buffer, &registerDataLength, TRUE);
		break;
	default:
		error = DS_UnsupportedError;
		break;
	}

	if (error != DS_NoError) {
		switch (error) {
		case DS_UnsupportedError:
			replyError = DSREPLY_UnsupportedOptionError;
			break;
		case DS_InvalidRegister:
			replyError = DSREPLY_InvalidRegisterRange;
			break;
		case DS_CWDSException:
			replyError = DSREPLY_CWDSException;
			break;
		case DS_InvalidProcessID:
			replyError = DSREPLY_InvalidProcessID;
			break;
		case DS_InvalidThreadID:
			replyError = DSREPLY_InvalidThreadID;
			break;
		case DS_OSError:
			replyError = DSREPLY_OSError;
			break;
		default:
			replyError = DSREPLY_CWDSError;
		}

		error = TRKStandardACK(buffer, DSMSG_ReplyACK, replyError);
	} else {
		error = TRKSendACK(buffer);
	}

	return error;
}

DSError TRKDoWriteRegisters(TRKBuffer* buffer)
{
	DSError error;
	DSReplyError replyError;
	DSMessageRegisterOptions options;
	u32 registerDataLength;
	u16 msg_lastRegister;
	u16 msg_firstRegister;
	u8 msg_options;
	u8 msg_command;

	if (buffer->length <= 6) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_PacketSizeError);
		return error;
	}
	TRKSetBufferPosition(buffer, DSREPLY_NoError);
	error = TRKReadBuffer1_ui8(buffer, &msg_command);
	if (error == DS_NoError)
		error = TRKReadBuffer1_ui8(buffer, &msg_options);

	if (error == DS_NoError)
		error = TRKReadBuffer1_ui16(buffer, &msg_firstRegister);

	if (error == DS_NoError)
		error = TRKReadBuffer1_ui16(buffer, &msg_lastRegister);

	if (msg_firstRegister > msg_lastRegister) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK,
		                       DSREPLY_InvalidRegisterRange);
		return error;
	}

	options = (DSMessageRegisterOptions)msg_options;
	switch (options) {
	case DSREG_Default:
		error = TRKTargetAccessDefault(msg_firstRegister, msg_lastRegister,
		                               buffer, &registerDataLength, FALSE);
		break;
	case DSREG_FP:
		error = TRKTargetAccessFP(msg_firstRegister, msg_lastRegister, buffer,
		                          &registerDataLength, FALSE);
		break;
	case DSREG_Extended1:
		error = TRKTargetAccessExtended1(msg_firstRegister, msg_lastRegister,
		                                 buffer, &registerDataLength, FALSE);
		break;
	case DSREG_Extended2:
		error = TRKTargetAccessExtended2(msg_firstRegister, msg_lastRegister,
		                                 buffer, &registerDataLength, FALSE);
		break;
	default:
		error = DS_UnsupportedError;
		break;
	}

	if (error == DS_NoError)
		TRKMessageIntoReply(buffer, DSMSG_ReplyACK, DSREPLY_NoError);

	if (error != DS_NoError) {
		switch (error) {
		case DS_UnsupportedError:
			replyError = DSREPLY_UnsupportedOptionError;
			break;
		case DS_InvalidRegister:
			replyError = DSREPLY_InvalidRegisterRange;
			break;
		case DS_MessageBufferReadError:
			replyError = DSREPLY_PacketSizeError;
			break;
		case DS_CWDSException:
			replyError = DSREPLY_CWDSException;
			break;
		case DS_InvalidProcessID:
			replyError = DSREPLY_InvalidProcessID;
			break;
		case DS_InvalidThreadID:
			replyError = DSREPLY_InvalidThreadID;
			break;
		case DS_OSError:
			replyError = DSREPLY_OSError;
			break;
		default:
			replyError = DSREPLY_CWDSError;
		}

		error = TRKStandardACK(buffer, DSMSG_ReplyACK, replyError);
	} else {
		error = TRKSendACK(buffer);
	}

	return error;
}

DSError TRKDoFlushCache(TRKBuffer* buffer)
{
	DSError error;
	DSReplyError replyErr;
	u32 msg_end;
	u32 msg_start;
	u8 msg_options;
	u8 msg_command;

	if (buffer->length != 10) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_PacketSizeError);
		return error;
	}

	TRKSetBufferPosition(buffer, DSREPLY_NoError);
	error = TRKReadBuffer1_ui8(buffer, &msg_command);
	if (error == DS_NoError)
		error = TRKReadBuffer1_ui8(buffer, &msg_options);
	if (error == DS_NoError)
		error = TRKReadBuffer1_ui32(buffer, &msg_start);
	if (error == DS_NoError)
		error = TRKReadBuffer1_ui32(buffer, &msg_end);

	if (msg_start > msg_end) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK,
		                       DSREPLY_InvalidMemoryRange);
		return error;
	}

	if (error == DS_NoError)
		error = TRKTargetFlushCache(msg_options, (void*)msg_start,
		                            (void*)msg_end);

	if (error == DS_NoError)
		TRKMessageIntoReply(buffer, DSMSG_ReplyACK, DSREPLY_NoError);

	if (error != DS_NoError) {
		switch (error) {
		case DS_UnsupportedError:
			replyErr = DSREPLY_UnsupportedOptionError;
			break;
		default:
			replyErr = DSREPLY_CWDSError;
			break;
		}

		error = TRKStandardACK(buffer, DSMSG_ReplyACK, replyErr);
	} else {
		error = TRKSendACK(buffer);
	}

	return error;
}

DSError TRKDoContinue(TRKBuffer* buffer)
{
	DSError error;

	error = TRKTargetStopped();
	if (error == DS_NoError) {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_NotStopped);
		return error;
	}

	error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_NoError);
	if (error == DS_NoError)
		error = TRKTargetContinue();

	return error;
}

DSError TRKDoStep(TRKBuffer* buffer)
{
	DSError error;
	u8 msg_command;
	u8 msg_options;
	u8 msg_count;
	u32 msg_rangeStart;
	u32 msg_rangeEnd;
	u32 pc;

	if (buffer->length < 3)
		return TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_PacketSizeError);

	TRKSetBufferPosition(buffer, DSREPLY_NoError);

	error = TRKReadBuffer1_ui8(buffer, &msg_command);
	if (error == DS_NoError)
		error = TRKReadBuffer1_ui8(buffer, &msg_options);

	switch (msg_options) {
	case DSSTEP_IntoCount:
	case DSSTEP_OverCount:
		if (error == DS_NoError)
			TRKReadBuffer1_ui8(buffer, &msg_count);
		if (msg_count >= 1) {
			break;
		}
		return TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_ParameterError);
	case DSSTEP_IntoRange:
	case DSSTEP_OverRange:
		if (buffer->length != 10)
			return TRKStandardACK(buffer, DSMSG_ReplyACK,
			                      DSREPLY_PacketSizeError);

		if (error == DS_NoError)
			error = TRKReadBuffer1_ui32(buffer, &msg_rangeStart);
		if (error == DS_NoError)
			error = TRKReadBuffer1_ui32(buffer, &msg_rangeEnd);

		pc = TRKTargetGetPC();
		if (pc >= msg_rangeStart && pc <= msg_rangeEnd) {
			break;
		}
		return TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_ParameterError);
	default:
		return TRKStandardACK(buffer, DSMSG_ReplyACK,
		                      DSREPLY_UnsupportedOptionError);
	}

	if (!TRKTargetStopped()) {
		return TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_NotStopped);
	} else {
		error = TRKStandardACK(buffer, DSMSG_ReplyACK, DSREPLY_NoError);
		if (error == DS_NoError)
			switch (msg_options) {
			case DSSTEP_IntoCount:
			case DSSTEP_OverCount:
				error = TRKTargetSingleStep(msg_count,
				                            (msg_options == DSSTEP_OverCount));
				break;
			case DSSTEP_IntoRange:
			case DSSTEP_OverRange:
				error = TRKTargetStepOutOfRange(
				    msg_rangeStart, msg_rangeEnd,
				    (msg_options == DSSTEP_OverRange));
				break;
			}

		return error;
	}
}

DSError TRKDoStop(TRKBuffer* b)
{
	DSReplyError replyError;

	switch (TRKTargetStop()) {
	case DS_NoError:
		replyError = DSREPLY_NoError;
		break;
	case DS_InvalidProcessID:
		replyError = DSREPLY_InvalidProcessID;
		break;
	case DS_InvalidThreadID:
		replyError = DSREPLY_InvalidThreadID;
		break;
	case DS_OSError:
		replyError = DSREPLY_OSError;
		break;
	default:
		replyError = DSREPLY_Error;
		break;
	}

	return TRKStandardACK(b, DSMSG_ReplyACK, replyError);
}
