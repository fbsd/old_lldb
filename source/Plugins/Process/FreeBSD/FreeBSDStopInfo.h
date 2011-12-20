//===-- FreeBSDStopInfo.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_FreeBSDStopInfo_H_
#define liblldb_FreeBSDStopInfo_H_

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Target/StopInfo.h"

#include "FreeBSDThread.h"
#include "ProcessMessage.h"

//===----------------------------------------------------------------------===//
/// @class FreeBSDStopInfo
/// @brief Simple base class for all FreeBSD-specific StopInfo objects.
///
class FreeBSDStopInfo
    : public lldb_private::StopInfo
{
public:
    FreeBSDStopInfo(lldb_private::Thread &thread, uint32_t status)
        : StopInfo(thread, status)
        { }
};

//===----------------------------------------------------------------------===//
/// @class FreeBSDLimboStopInfo
/// @brief Represents the stop state of a process ready to exit.
///
class FreeBSDLimboStopInfo
    : public FreeBSDStopInfo
{
public:
    FreeBSDLimboStopInfo(FreeBSDThread &thread)
        : FreeBSDStopInfo(thread, 0)
        { }

    ~FreeBSDLimboStopInfo();

    lldb::StopReason
    GetStopReason() const;

    const char *
    GetDescription();

    bool
    ShouldStop(lldb_private::Event *event_ptr);

    bool
    ShouldNotify(lldb_private::Event *event_ptr);
};


//===----------------------------------------------------------------------===//
/// @class FreeBSDCrashStopInfo
/// @brief Represents the stop state of process that is ready to crash.
///
class FreeBSDCrashStopInfo
    : public FreeBSDStopInfo
{
public:
    FreeBSDCrashStopInfo(FreeBSDThread &thread, uint32_t status, 
                  ProcessMessage::CrashReason reason)
        : FreeBSDStopInfo(thread, status),
          m_crash_reason(reason)
        { }

    ~FreeBSDCrashStopInfo();

    lldb::StopReason
    GetStopReason() const;

    const char *
    GetDescription();

    ProcessMessage::CrashReason
    GetCrashReason() const;

private:
    ProcessMessage::CrashReason m_crash_reason;
};    

#endif
