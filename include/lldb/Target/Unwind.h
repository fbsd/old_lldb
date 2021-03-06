//===-- Unwind.h ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Unwind_h_
#define liblldb_Unwind_h_

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"
#include "lldb/Host/Mutex.h"

namespace lldb_private {

class Unwind 
{
protected:
    //------------------------------------------------------------------
    // Classes that inherit from Unwind can see and modify these
    //------------------------------------------------------------------
    Unwind(Thread &thread) :
        m_thread (thread),
        m_unwind_mutex()
    {
    }

public:
    virtual
    ~Unwind()
    {
    }

    void
    Clear()
    {
        Mutex::Locker locker(m_unwind_mutex);
        DoClear();
    
    }

    uint32_t
    GetFrameCount()
    {
        Mutex::Locker locker(m_unwind_mutex);
        return DoGetFrameCount();
    }

    bool
    GetFrameInfoAtIndex (uint32_t frame_idx,
                         lldb::addr_t& cfa, 
                         lldb::addr_t& pc)
    {
        Mutex::Locker locker(m_unwind_mutex);
        return DoGetFrameInfoAtIndex (frame_idx, cfa, pc);
    }
    
    lldb::RegisterContextSP
    CreateRegisterContextForFrame (StackFrame *frame)
    {
        Mutex::Locker locker(m_unwind_mutex);
        return DoCreateRegisterContextForFrame (frame);
    }
    
    Thread &
    GetThread()
    {
        return m_thread;
    }

protected:
    //------------------------------------------------------------------
    // Classes that inherit from Unwind can see and modify these
    //------------------------------------------------------------------
    virtual void
    DoClear() = 0;

    virtual uint32_t
    DoGetFrameCount() = 0;

    virtual bool
    DoGetFrameInfoAtIndex (uint32_t frame_idx,
                         lldb::addr_t& cfa, 
                         lldb::addr_t& pc) = 0;
    
    virtual lldb::RegisterContextSP
    DoCreateRegisterContextForFrame (StackFrame *frame) = 0;

    Thread &m_thread;
    Mutex  m_unwind_mutex;
private:
    DISALLOW_COPY_AND_ASSIGN (Unwind);
};

} // namespace lldb_private

#endif  // liblldb_Unwind_h_
