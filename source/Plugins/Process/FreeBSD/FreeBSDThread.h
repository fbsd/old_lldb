//===-- FreeBSDThread.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_FreeBSDThread_H_
#define liblldb_FreeBSDThread_H_

// C Includes
// C++ Includes
#include <memory>

// Other libraries and framework includes
#include "lldb/Target/Thread.h"

class ProcessMessage;
class ProcessMonitor;
class RegisterContextFreeBSD;

//------------------------------------------------------------------------------
// @class FreeBSDThread
// @brief Abstraction of a FreeBSD process (thread).
class FreeBSDThread
    : public lldb_private::Thread
{
public:
    FreeBSDThread(lldb_private::Process &process, lldb::tid_t tid);

    virtual ~FreeBSDThread();

    void
    RefreshStateAfterStop();

    bool
    WillResume(lldb::StateType resume_state);

    const char *
    GetInfo();

    virtual lldb::RegisterContextSP
    GetRegisterContext();

    virtual lldb::RegisterContextSP
    CreateRegisterContextForFrame (lldb_private::StackFrame *frame);

    //--------------------------------------------------------------------------
    // These methods form a specialized interface to FreeBSD threads.
    //
    bool Resume();

    void Notify(const ProcessMessage &message);

private:
    RegisterContextFreeBSD *
    GetRegisterContextFreeBSD ()
    {
        if (!m_reg_context_sp)
            GetRegisterContext();
        return (RegisterContextFreeBSD *)m_reg_context_sp.get();
    }
    
    std::auto_ptr<lldb_private::StackFrame> m_frame_ap;

    lldb::BreakpointSiteSP m_breakpoint;
    lldb::StopInfoSP m_stop_info;

    ProcessMonitor &
    GetMonitor();

    lldb::StopInfoSP
    GetPrivateStopReason();

    void BreakNotify(const ProcessMessage &message);
    void TraceNotify(const ProcessMessage &message);
    void LimboNotify(const ProcessMessage &message);
    void SignalNotify(const ProcessMessage &message);
    void SignalDeliveredNotify(const ProcessMessage &message);
    void CrashNotify(const ProcessMessage &message);

    lldb_private::Unwind *
    GetUnwinder();
};

#endif // #ifndef liblldb_FreeBSDThread_H_
