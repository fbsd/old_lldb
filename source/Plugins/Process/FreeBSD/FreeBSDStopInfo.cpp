//===-- FreeBSDStopInfo.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "FreeBSDStopInfo.h"

using namespace lldb;
using namespace lldb_private;


//===----------------------------------------------------------------------===//
// FreeBSDLimboStopInfo

FreeBSDLimboStopInfo::~FreeBSDLimboStopInfo() { }

lldb::StopReason
FreeBSDLimboStopInfo::GetStopReason() const
{
    return lldb::eStopReasonTrace;
}

const char *
FreeBSDLimboStopInfo::GetDescription()
{
    return "thread exiting";
}

bool
FreeBSDLimboStopInfo::ShouldStop(Event *event_ptr)
{
    return true;
}

bool
FreeBSDLimboStopInfo::ShouldNotify(Event *event_ptr)
{
    return true;
}

//===----------------------------------------------------------------------===//
// FreeBSDCrashStopInfo

FreeBSDCrashStopInfo::~FreeBSDCrashStopInfo() { }

lldb::StopReason
FreeBSDCrashStopInfo::GetStopReason() const
{
    return lldb::eStopReasonException;
}

const char *
FreeBSDCrashStopInfo::GetDescription()
{
    return ProcessMessage::GetCrashReasonString(m_crash_reason);
}
