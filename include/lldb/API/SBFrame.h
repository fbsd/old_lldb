//===-- SBFrame.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBFrame_h_
#define LLDB_SBFrame_h_

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBValueList.h"
#include "lldb/API/SBWatchpoint.h"

namespace lldb {

class SBFrame
{
public:
    SBFrame ();

    SBFrame (const lldb::SBFrame &rhs);
    
#ifndef SWIG
    const lldb::SBFrame &
    operator =(const lldb::SBFrame &rhs);
#endif

   ~SBFrame();

    bool
    IsValid() const;

    uint32_t
    GetFrameID () const;

    lldb::addr_t
    GetPC () const;

    bool
    SetPC (lldb::addr_t new_pc);

    lldb::addr_t
    GetSP () const;

    lldb::addr_t
    GetFP () const;

    lldb::SBAddress
    GetPCAddress () const;

    lldb::SBSymbolContext
    GetSymbolContext (uint32_t resolve_scope) const;

    lldb::SBModule
    GetModule () const;

    lldb::SBCompileUnit
    GetCompileUnit () const;

    lldb::SBFunction
    GetFunction () const;

    lldb::SBSymbol
    GetSymbol () const;

    /// Gets the deepest block that contains the frame PC.
    ///
    /// See also GetFrameBlock().
    lldb::SBBlock
    GetBlock () const;

    /// Get the appropriate function name for this frame. Inlined functions in
    /// LLDB are represented by Blocks that have inlined function information, so
    /// just looking at the SBFunction or SBSymbol for a frame isn't enough.
    /// This function will return the appriopriate function, symbol or inlined
    /// function name for the frame.
    ///
    /// This function returns:
    /// - the name of the inlined function (if there is one)
    /// - the name of the concrete function (if there is one)
    /// - the name of the symbol (if there is one)
    /// - NULL
    ///
    /// See also IsInlined().
    const char *
    GetFunctionName();

    /// Return true if this frame represents an inlined function.
    ///
    /// See also GetFunctionName().
    bool
    IsInlined();
    
    /// The version that doesn't supply a 'use_dynamic' value will use the
    /// target's default.
    lldb::SBValue
    EvaluateExpression (const char *expr);    

    lldb::SBValue
    EvaluateExpression (const char *expr, lldb::DynamicValueType use_dynamic);

    /// Gets the lexical block that defines the stack frame. Another way to think
    /// of this is it will return the block that contains all of the variables
    /// for a stack frame. Inlined functions are represented as SBBlock objects
    /// that have inlined function information: the name of the inlined function,
    /// where it was called from. The block that is returned will be the first 
    /// block at or above the block for the PC (SBFrame::GetBlock()) that defines
    /// the scope of the frame. When a function contains no inlined functions,
    /// this will be the top most lexical block that defines the function. 
    /// When a function has inlined functions and the PC is currently
    /// in one of those inlined functions, this method will return the inlined
    /// block that defines this frame. If the PC isn't currently in an inlined
    /// function, the lexical block that defines the function is returned.
    lldb::SBBlock
    GetFrameBlock () const;

    lldb::SBLineEntry
    GetLineEntry () const;

    lldb::SBThread
    GetThread () const;

    const char *
    Disassemble () const;

    void
    Clear();

#ifndef SWIG
    bool
    operator == (const lldb::SBFrame &rhs) const;

    bool
    operator != (const lldb::SBFrame &rhs) const;

#endif

    /// The version that doesn't supply a 'use_dynamic' value will use the
    /// target's default.
    lldb::SBValueList
    GetVariables (bool arguments,
                  bool locals,
                  bool statics,
                  bool in_scope_only);

    lldb::SBValueList
    GetVariables (bool arguments,
                  bool locals,
                  bool statics,
                  bool in_scope_only,
                  lldb::DynamicValueType  use_dynamic);

    lldb::SBValueList
    GetRegisters ();

    /// The version that doesn't supply a 'use_dynamic' value will use the
    /// target's default.
    lldb::SBValue
    FindVariable (const char *var_name);

    lldb::SBValue
    FindVariable (const char *var_name, lldb::DynamicValueType use_dynamic);

    /// Find variables, register sets, registers, or persistent variables using
    /// the frame as the scope.
    ///
    /// The version that doesn't supply a 'use_dynamic' value will use the
    /// target's default.
    lldb::SBValue
    FindValue (const char *name, ValueType value_type);

    lldb::SBValue
    FindValue (const char *name, ValueType value_type, lldb::DynamicValueType use_dynamic);

    /// Find and watch a variable using the frame as the scope.
    /// It returns an SBValue, similar to FindValue() method, if find-and-watch
    /// operation succeeds.  Otherwise, an invalid SBValue is returned.
    /// You can use LLDB_WATCH_TYPE_READ | LLDB_WATCH_TYPE_WRITE for 'rw' watch.
    lldb::SBValue
    WatchValue (const char *name, ValueType value_type, uint32_t watch_type);

    /// Find and watch the location pointed to by a variable using the frame as
    /// the scope.
    /// It returns an SBValue, similar to FindValue() method, if find-and-watch
    /// operation succeeds.  Otherwise, an invalid SBValue is returned.
    /// You can use LLDB_WATCH_TYPE_READ | LLDB_WATCH_TYPE_WRITE for 'rw' watch.
    lldb::SBValue
    WatchLocation (const char *name, ValueType value_type, uint32_t watch_type, size_t size);

    bool
    GetDescription (lldb::SBStream &description);

#ifndef SWIG
    SBFrame (const lldb::StackFrameSP &lldb_object_sp);
#endif

protected:
    friend class SBValue;

private:
    friend class SBThread;
    friend class SBInstruction;
#ifndef LLDB_DISABLE_PYTHON
    friend class lldb_private::ScriptInterpreterPython;
#endif

#ifndef SWIG

    lldb_private::StackFrame *
    operator->() const;

    // Mimic shared pointer...
    lldb_private::StackFrame *
    get() const;

    lldb::StackFrameSP &
    get_sp();
    
#endif

    void
    SetFrame (const lldb::StackFrameSP &lldb_object_sp);

    lldb::StackFrameSP m_opaque_sp;
};

} // namespace lldb

#endif  // LLDB_SBFrame_h_
