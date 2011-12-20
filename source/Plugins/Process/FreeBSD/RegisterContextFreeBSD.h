//===-- RegisterContext_x86_64.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextFreeBSD_H_
#define liblldb_RegisterContextFreeBSD_H_

// C Includes
// C++ Includes
// Other libraries and framework includes
#include "lldb/Target/RegisterContext.h"

//------------------------------------------------------------------------------
/// @class RegisterContextFreeBSD
///
/// @brief Extends RegisterClass with a few virtual operations useful on FreeBSD.
class RegisterContextFreeBSD
    : public lldb_private::RegisterContext
{
public:
    RegisterContextFreeBSD(lldb_private::Thread &thread,
                         uint32_t concrete_frame_idx)
        : RegisterContext(thread, concrete_frame_idx) { }

    /// Updates the register state of the associated thread after hitting a
    /// breakpoint (if that make sense for the architecture).  Default
    /// implementation simply returns true for architectures which do not
    /// require any update.
    ///
    /// @return
    ///    True if the operation succeeded and false otherwise.
    virtual bool UpdateAfterBreakpoint() { return true; }
};

#endif // #ifndef liblldb_RegisterContextFreeBSD_H_
