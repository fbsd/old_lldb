//===-- SBValue.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBValue.h"
#include "lldb/API/SBStream.h"

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Core/DataExtractor.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Scalar.h"
#include "lldb/Core/Stream.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"

#include "lldb/API/SBProcess.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBFrame.h"
#include "lldb/API/SBDebugger.h"

using namespace lldb;
using namespace lldb_private;

SBValue::SBValue () :
    m_opaque_sp ()
{
}

SBValue::SBValue (const lldb::ValueObjectSP &value_sp) :
    m_opaque_sp (value_sp)
{
}

SBValue::SBValue(const SBValue &rhs) :
    m_opaque_sp (rhs.m_opaque_sp)
{
}

SBValue &
SBValue::operator = (const SBValue &rhs)
{
    if (this != &rhs)
        m_opaque_sp = rhs.m_opaque_sp;
    return *this;
}

SBValue::~SBValue()
{
}

bool
SBValue::IsValid ()
{
    // If this function ever changes to anything that does more than just
    // check if the opaque shared pointer is non NULL, then we need to update
    // all "if (m_opaque_sp)" code in this file.
    return m_opaque_sp.get() != NULL;
}

void
SBValue::Clear()
{
    m_opaque_sp.reset();
}

SBError
SBValue::GetError()
{
    SBError sb_error;
    
    if (m_opaque_sp.get())
        sb_error.SetError(m_opaque_sp->GetError());
    
    return sb_error;
}

user_id_t
SBValue::GetID()
{
    if (m_opaque_sp)
        return m_opaque_sp->GetID();
    return LLDB_INVALID_UID;
}

const char *
SBValue::GetName()
{

    const char *name = NULL;
    if (m_opaque_sp)
        name = m_opaque_sp->GetName().GetCString();

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (name)
            log->Printf ("SBValue(%p)::GetName () => \"%s\"", m_opaque_sp.get(), name);
        else
            log->Printf ("SBValue(%p)::GetName () => NULL", m_opaque_sp.get());
    }

    return name;
}

const char *
SBValue::GetTypeName ()
{
    const char *name = NULL;
    if (m_opaque_sp)
        name = m_opaque_sp->GetTypeName().GetCString();
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (name)
            log->Printf ("SBValue(%p)::GetTypeName () => \"%s\"", m_opaque_sp.get(), name);
        else
            log->Printf ("SBValue(%p)::GetTypeName () => NULL", m_opaque_sp.get());
    }

    return name;
}

size_t
SBValue::GetByteSize ()
{
    size_t result = 0;

    if (m_opaque_sp)
        result = m_opaque_sp->GetByteSize();

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::GetByteSize () => %zu", m_opaque_sp.get(), result);

    return result;
}

bool
SBValue::IsInScope ()
{
    bool result = false;

    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            result = m_opaque_sp->IsInScope ();
        }
    }

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::IsInScope () => %i", m_opaque_sp.get(), result);

    return result;
}

const char *
SBValue::GetValue ()
{
    const char *cstr = NULL;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            cstr = m_opaque_sp->GetValueAsCString ();
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (cstr)
            log->Printf ("SBValue(%p)::GetValue => \"%s\"", m_opaque_sp.get(), cstr);
        else
            log->Printf ("SBValue(%p)::GetValue => NULL", m_opaque_sp.get());
    }

    return cstr;
}

ValueType
SBValue::GetValueType ()
{
    ValueType result = eValueTypeInvalid;
    if (m_opaque_sp)
        result = m_opaque_sp->GetValueType();
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        switch (result)
        {
        case eValueTypeInvalid:         log->Printf ("SBValue(%p)::GetValueType () => eValueTypeInvalid", m_opaque_sp.get()); break;
        case eValueTypeVariableGlobal:  log->Printf ("SBValue(%p)::GetValueType () => eValueTypeVariableGlobal", m_opaque_sp.get()); break;
        case eValueTypeVariableStatic:  log->Printf ("SBValue(%p)::GetValueType () => eValueTypeVariableStatic", m_opaque_sp.get()); break;
        case eValueTypeVariableArgument:log->Printf ("SBValue(%p)::GetValueType () => eValueTypeVariableArgument", m_opaque_sp.get()); break;
        case eValueTypeVariableLocal:   log->Printf ("SBValue(%p)::GetValueType () => eValueTypeVariableLocal", m_opaque_sp.get()); break;
        case eValueTypeRegister:        log->Printf ("SBValue(%p)::GetValueType () => eValueTypeRegister", m_opaque_sp.get()); break;
        case eValueTypeRegisterSet:     log->Printf ("SBValue(%p)::GetValueType () => eValueTypeRegisterSet", m_opaque_sp.get()); break;
        case eValueTypeConstResult:     log->Printf ("SBValue(%p)::GetValueType () => eValueTypeConstResult", m_opaque_sp.get()); break;
        default:     log->Printf ("SBValue(%p)::GetValueType () => %i ???", m_opaque_sp.get(), result); break;
        }
    }
    return result;
}

const char *
SBValue::GetObjectDescription ()
{
    const char *cstr = NULL;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            cstr = m_opaque_sp->GetObjectDescription ();
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (cstr)
            log->Printf ("SBValue(%p)::GetObjectDescription => \"%s\"", m_opaque_sp.get(), cstr);
        else
            log->Printf ("SBValue(%p)::GetObjectDescription => NULL", m_opaque_sp.get());
    }
    return cstr;
}

SBType
SBValue::GetType()
{
    SBType result;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            result = SBType(ClangASTType (m_opaque_sp->GetClangAST(), m_opaque_sp->GetClangType()));
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (result.IsValid())
            log->Printf ("SBValue(%p)::GetType => %p", m_opaque_sp.get(), &result);
        else
            log->Printf ("SBValue(%p)::GetType => NULL", m_opaque_sp.get());
    }
    return result;
}

bool
SBValue::GetValueDidChange ()
{
    bool result = false;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            result = m_opaque_sp->GetValueDidChange ();
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::GetValueDidChange => %i", m_opaque_sp.get(), result);

    return result;
}

const char *
SBValue::GetSummary ()
{
    const char *cstr = NULL;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            cstr = m_opaque_sp->GetSummaryAsCString();
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (cstr)
            log->Printf ("SBValue(%p)::GetSummary => \"%s\"", m_opaque_sp.get(), cstr);
        else
            log->Printf ("SBValue(%p)::GetSummary => NULL", m_opaque_sp.get());
    }
    return cstr;
}

const char *
SBValue::GetLocation ()
{
    const char *cstr = NULL;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            cstr = m_opaque_sp->GetLocationAsCString();
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (cstr)
            log->Printf ("SBValue(%p)::GetSummary => \"%s\"", m_opaque_sp.get(), cstr);
        else
            log->Printf ("SBValue(%p)::GetSummary => NULL", m_opaque_sp.get());
    }
    return cstr;
}

bool
SBValue::SetValueFromCString (const char *value_str)
{
    bool success = false;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            success = m_opaque_sp->SetValueFromCString (value_str);
        }
    }
    return success;
}

lldb::SBValue
SBValue::CreateChildAtOffset (const char *name, uint32_t offset, SBType type)
{
    lldb::SBValue result;
    if (m_opaque_sp)
    {
        if (type.IsValid())
        {
            result = SBValue(m_opaque_sp->GetSyntheticChildAtOffset(offset, type.m_opaque_sp->GetClangASTType(), true));
            result.m_opaque_sp->SetName(ConstString(name));
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (result.IsValid())
            log->Printf ("SBValue(%p)::GetChildAtOffset => \"%s\"", m_opaque_sp.get(), result.m_opaque_sp->GetName().AsCString());
        else
            log->Printf ("SBValue(%p)::GetChildAtOffset => NULL", m_opaque_sp.get());
    }
    return result;
}

lldb::SBValue
SBValue::Cast (SBType type)
{
    return CreateChildAtOffset(m_opaque_sp->GetName().GetCString(), 0, type);
}

lldb::SBValue
SBValue::CreateValueFromExpression (const char *name, const char* expression)
{
    lldb::SBValue result;
    if (m_opaque_sp)
    {
        ValueObjectSP result_valobj_sp;
        m_opaque_sp->GetUpdatePoint().GetTargetSP()->EvaluateExpression (expression,
                                                                         m_opaque_sp->GetExecutionContextScope()->CalculateStackFrame(),
                                                                         eExecutionPolicyOnlyWhenNeeded,
                                                                         false, // coerce to id
                                                                         true, // unwind on error
                                                                         true, // keep in memory
                                                                         eNoDynamicValues,
                                                                         result_valobj_sp);
        if (result_valobj_sp)
        {
            result_valobj_sp->SetName(ConstString(name));
            result = SBValue(result_valobj_sp);
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (result.IsValid())
            log->Printf ("SBValue(%p)::GetChildFromExpression => \"%s\"", m_opaque_sp.get(), result.m_opaque_sp->GetName().AsCString());
        else
            log->Printf ("SBValue(%p)::GetChildFromExpression => NULL", m_opaque_sp.get());
    }
    return result;
}

lldb::SBValue
SBValue::CreateValueFromAddress(const char* name, lldb::addr_t address, SBType type)
{
    lldb::SBValue result;
    if (m_opaque_sp && type.IsValid() && type.GetPointerType().IsValid())
    {
        SBType real_type(type.GetPointerType());
        
        lldb::DataBufferSP buffer(new lldb_private::DataBufferHeap(&address,sizeof(lldb::addr_t)));
        
        ValueObjectSP ptr_result_valobj_sp(ValueObjectConstResult::Create (m_opaque_sp->GetExecutionContextScope(),
                                                                           real_type.m_opaque_sp->GetASTContext(),
                                                                           real_type.m_opaque_sp->GetOpaqueQualType(),
                                                                           ConstString(name),
                                                                           buffer,
                                                                           lldb::endian::InlHostByteOrder(), 
                                                                           GetTarget().GetProcess().GetAddressByteSize()));
        
        ValueObjectSP result_valobj_sp;
        
        ptr_result_valobj_sp->GetValue().SetValueType(Value::eValueTypeLoadAddress);
        if (ptr_result_valobj_sp)
        {
            Error err;
            result_valobj_sp = ptr_result_valobj_sp->Dereference(err);
            if (result_valobj_sp)
                result_valobj_sp->SetName(ConstString(name));
        }
        result = SBValue(result_valobj_sp);
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (result.IsValid())
            log->Printf ("SBValue(%p)::GetChildFromAddress => \"%s\"", m_opaque_sp.get(), result.m_opaque_sp->GetName().AsCString());
        else
            log->Printf ("SBValue(%p)::GetChildFromAddress => NULL", m_opaque_sp.get());
    }
    return result;
}

lldb::SBValue
SBValue::CreateValueFromData (const char* name, SBData data, SBType type)
{
    SBValue result;
    
    AddressType addr_of_children_priv = eAddressTypeLoad;
    
    if (m_opaque_sp)
    {
        ValueObjectSP valobj_sp;
        valobj_sp = ValueObjectConstResult::Create (m_opaque_sp->GetExecutionContextScope(), 
                                                    type.m_opaque_sp->GetASTContext() ,
                                                    type.m_opaque_sp->GetOpaqueQualType(),
                                                    ConstString(name),
                                                    *data.m_opaque_sp,
                                                    LLDB_INVALID_ADDRESS);
        valobj_sp->SetAddressTypeOfChildren(addr_of_children_priv);
        result = SBValue(valobj_sp);
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (result.IsValid())
            log->Printf ("SBValue(%p)::GetChildFromExpression => \"%s\"", m_opaque_sp.get(), result.m_opaque_sp->GetName().AsCString());
        else
            log->Printf ("SBValue(%p)::GetChildFromExpression => NULL", m_opaque_sp.get());
    }
    return result;
}

SBValue
SBValue::GetChildAtIndex (uint32_t idx)
{
    const bool can_create_synthetic = false;
    lldb::DynamicValueType use_dynamic = eNoDynamicValues;
    if (m_opaque_sp)
        use_dynamic = m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetPreferDynamicValue();
    return GetChildAtIndex (idx, use_dynamic, can_create_synthetic);
}

SBValue
SBValue::GetChildAtIndex (uint32_t idx, lldb::DynamicValueType use_dynamic, bool can_create_synthetic)
{
    lldb::ValueObjectSP child_sp;

    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            const bool can_create = true;
            child_sp = m_opaque_sp->GetChildAtIndex (idx, can_create);
            if (can_create_synthetic && !child_sp)
            {
                if (m_opaque_sp->IsPointerType())
                {
                    child_sp = m_opaque_sp->GetSyntheticArrayMemberFromPointer(idx, can_create);
                }
                else if (m_opaque_sp->IsArrayType())
                {
                    child_sp = m_opaque_sp->GetSyntheticArrayMemberFromArray(idx, can_create);
                }
            }
                
            if (child_sp)
            {
                if (use_dynamic != lldb::eNoDynamicValues)
                {
                    lldb::ValueObjectSP dynamic_sp(child_sp->GetDynamicValue (use_dynamic));
                    if (dynamic_sp)
                        child_sp = dynamic_sp;
                }
            }
        }
    }
    
    SBValue sb_value (child_sp);
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::GetChildAtIndex (%u) => SBValue(%p)", m_opaque_sp.get(), idx, sb_value.get());

    return sb_value;
}

uint32_t
SBValue::GetIndexOfChildWithName (const char *name)
{
    uint32_t idx = UINT32_MAX;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
        
            idx = m_opaque_sp->GetIndexOfChildWithName (ConstString(name));
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (idx == UINT32_MAX)
            log->Printf ("SBValue(%p)::GetIndexOfChildWithName (name=\"%s\") => NOT FOUND", m_opaque_sp.get(), name);
        else
            log->Printf ("SBValue(%p)::GetIndexOfChildWithName (name=\"%s\") => %u", m_opaque_sp.get(), name, idx);
    }
    return idx;
}

SBValue
SBValue::GetChildMemberWithName (const char *name)
{
    if (m_opaque_sp)
    {
        lldb::DynamicValueType use_dynamic_value = m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetPreferDynamicValue();
        return GetChildMemberWithName (name, use_dynamic_value);
    }
    else
        return GetChildMemberWithName (name, eNoDynamicValues);
}

SBValue
SBValue::GetChildMemberWithName (const char *name, lldb::DynamicValueType use_dynamic_value)
{
    lldb::ValueObjectSP child_sp;
    const ConstString str_name (name);


    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            child_sp = m_opaque_sp->GetChildMemberWithName (str_name, true);
            if (use_dynamic_value != lldb::eNoDynamicValues)
            {
                if (child_sp)
                {
                    lldb::ValueObjectSP dynamic_sp = child_sp->GetDynamicValue (use_dynamic_value);
                    if (dynamic_sp)
                        child_sp = dynamic_sp;
                }
            }
        }
    }
    
    SBValue sb_value (child_sp);

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::GetChildMemberWithName (name=\"%s\") => SBValue(%p)", m_opaque_sp.get(), name, sb_value.get());

    return sb_value;
}

lldb::SBValue
SBValue::GetDynamicValue (lldb::DynamicValueType use_dynamic)
{
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            return SBValue (m_opaque_sp->GetDynamicValue(use_dynamic));
        }
    }
    
    return SBValue();
}

lldb::SBValue
SBValue::GetStaticValue ()
{
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            return SBValue(m_opaque_sp->GetStaticValue());
        }
    }
    
    return SBValue();
}

bool
SBValue::IsDynamic()
{
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            return m_opaque_sp->IsDynamic();
        }
    }
    return false;
}

lldb::SBValue
SBValue::GetValueForExpressionPath(const char* expr_path)
{
    lldb::ValueObjectSP child_sp;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            // using default values for all the fancy options, just do it if you can
            child_sp = m_opaque_sp->GetValueForExpressionPath(expr_path);
        }
    }
    
    SBValue sb_value (child_sp);
    
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::GetValueForExpressionPath (expr_path=\"%s\") => SBValue(%p)", m_opaque_sp.get(), expr_path, sb_value.get());
    
    return sb_value;
}

int64_t
SBValue::GetValueAsSigned(SBError& error, int64_t fail_value)
{
    error.Clear();
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            Scalar scalar;
            if (m_opaque_sp->ResolveValue (scalar))
                return scalar.GetRawBits64(fail_value);
            else
                error.SetErrorString("could not get value");
        }
        else
            error.SetErrorString("could not get target");
    }
    error.SetErrorString("invalid SBValue");
    return fail_value;
}

uint64_t
SBValue::GetValueAsUnsigned(SBError& error, uint64_t fail_value)
{
    error.Clear();
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            Scalar scalar;
            if (m_opaque_sp->ResolveValue (scalar))
                return scalar.GetRawBits64(fail_value);
            else
                error.SetErrorString("could not get value");
        }
        else
            error.SetErrorString("could not get target");
    }
    error.SetErrorString("invalid SBValue");
    return fail_value;
}

int64_t
SBValue::GetValueAsSigned(int64_t fail_value)
{
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            Scalar scalar;
            if (m_opaque_sp->ResolveValue (scalar))
                return scalar.GetRawBits64(fail_value);
        }
    }
    return fail_value;
}

uint64_t
SBValue::GetValueAsUnsigned(uint64_t fail_value)
{
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());
            Scalar scalar;
            if (m_opaque_sp->ResolveValue (scalar))
                return scalar.GetRawBits64(fail_value);
        }
    }
    return fail_value;
}

uint32_t
SBValue::GetNumChildren ()
{
    uint32_t num_children = 0;

    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());

            num_children = m_opaque_sp->GetNumChildren();
        }
    }

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::GetNumChildren () => %u", m_opaque_sp.get(), num_children);

    return num_children;
}


SBValue
SBValue::Dereference ()
{
    SBValue sb_value;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());

            Error error;
            sb_value = m_opaque_sp->Dereference (error);
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::Dereference () => SBValue(%p)", m_opaque_sp.get(), sb_value.get());

    return sb_value;
}

bool
SBValue::TypeIsPointerType ()
{
    bool is_ptr_type = false;

    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());

            is_ptr_type = m_opaque_sp->IsPointerType();
        }
    }

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::TypeIsPointerType () => %i", m_opaque_sp.get(), is_ptr_type);


    return is_ptr_type;
}

void *
SBValue::GetOpaqueType()
{
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            Mutex::Locker api_locker (m_opaque_sp->GetUpdatePoint().GetTargetSP()->GetAPIMutex());

            return m_opaque_sp->GetClangType();
        }
    }
    return NULL;
}

lldb::SBTarget
SBValue::GetTarget()
{
    SBTarget result;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetUpdatePoint().GetTargetSP())
        {
            result = SBTarget(lldb::TargetSP(m_opaque_sp->GetUpdatePoint().GetTargetSP()));
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (result.get() == NULL)
            log->Printf ("SBValue(%p)::GetTarget () => NULL", m_opaque_sp.get());
        else
            log->Printf ("SBValue(%p)::GetTarget () => %p", m_opaque_sp.get(), result.get());
    }
    return result;
}

lldb::SBProcess
SBValue::GetProcess()
{
    SBProcess result;
    if (m_opaque_sp)
    {
        Target* target = m_opaque_sp->GetUpdatePoint().GetTargetSP().get();
        if (target)
        {
            result = SBProcess(lldb::ProcessSP(target->GetProcessSP()));
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (result.get() == NULL)
            log->Printf ("SBValue(%p)::GetProcess () => NULL", m_opaque_sp.get());
        else
            log->Printf ("SBValue(%p)::GetProcess () => %p", m_opaque_sp.get(), result.get());
    }
    return result;
}

lldb::SBThread
SBValue::GetThread()
{
    SBThread result;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetExecutionContextScope())
        {
            result = SBThread(m_opaque_sp->GetExecutionContextScope()->CalculateThread()->GetSP());
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (result.get() == NULL)
            log->Printf ("SBValue(%p)::GetThread () => NULL", m_opaque_sp.get());
        else
            log->Printf ("SBValue(%p)::GetThread () => %p", m_opaque_sp.get(), result.get());
    }
    return result;
}

lldb::SBFrame
SBValue::GetFrame()
{
    SBFrame result;
    if (m_opaque_sp)
    {
        if (m_opaque_sp->GetExecutionContextScope())
        {
            result.SetFrame (m_opaque_sp->GetExecutionContextScope()->CalculateStackFrame()->GetSP());
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        if (result.get() == NULL)
            log->Printf ("SBValue(%p)::GetFrame () => NULL", m_opaque_sp.get());
        else
            log->Printf ("SBValue(%p)::GetFrame () => %p", m_opaque_sp.get(), result.get());
    }
    return result;
}


// Mimic shared pointer...
lldb_private::ValueObject *
SBValue::get() const
{
    return m_opaque_sp.get();
}

lldb_private::ValueObject *
SBValue::operator->() const
{
    return m_opaque_sp.get();
}

lldb::ValueObjectSP &
SBValue::operator*()
{
    return m_opaque_sp;
}

const lldb::ValueObjectSP &
SBValue::operator*() const
{
    return m_opaque_sp;
}

bool
SBValue::GetExpressionPath (SBStream &description)
{
    if (m_opaque_sp)
    {
        m_opaque_sp->GetExpressionPath (description.ref(), false);
        return true;
    }
    return false;
}

bool
SBValue::GetExpressionPath (SBStream &description, bool qualify_cxx_base_classes)
{
    if (m_opaque_sp)
    {
        m_opaque_sp->GetExpressionPath (description.ref(), qualify_cxx_base_classes);
        return true;
    }
    return false;
}

bool
SBValue::GetDescription (SBStream &description)
{
    Stream &strm = description.ref();

    if (m_opaque_sp)
    {
        ValueObject::DumpValueObject (strm, m_opaque_sp.get());
    }
    else
        strm.PutCString ("No value");

    return true;
}

lldb::Format
SBValue::GetFormat ()
{
    if (m_opaque_sp)
        return m_opaque_sp->GetFormat();
    return eFormatDefault;
}

void
SBValue::SetFormat (lldb::Format format)
{
    if (m_opaque_sp)
        m_opaque_sp->SetFormat(format);
}

lldb::SBValue
SBValue::AddressOf()
{
    SBValue sb_value;
    if (m_opaque_sp)
    {
        Target* target = m_opaque_sp->GetUpdatePoint().GetTargetSP().get();
        if (target)
        {
            Mutex::Locker api_locker (target->GetAPIMutex());
            Error error;
            sb_value = m_opaque_sp->AddressOf (error);
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::GetPointerToObject () => SBValue(%p)", m_opaque_sp.get(), sb_value.get());
    
    return sb_value;
}

lldb::addr_t
SBValue::GetLoadAddress()
{
    lldb::addr_t value = LLDB_INVALID_ADDRESS;
    if (m_opaque_sp)
    {
        Target* target = m_opaque_sp->GetUpdatePoint().GetTargetSP().get();
        if (target)
        {
            Mutex::Locker api_locker (target->GetAPIMutex());
            const bool scalar_is_load_address = true;
            AddressType addr_type;
            value = m_opaque_sp->GetAddressOf(scalar_is_load_address, &addr_type);
            if (addr_type == eAddressTypeFile)
            {
                Module* module = m_opaque_sp->GetModule();
                if (!module)
                    value = LLDB_INVALID_ADDRESS;
                else
                {
                    Address addr;
                    module->ResolveFileAddress(value, addr);
                    value = addr.GetLoadAddress(m_opaque_sp->GetUpdatePoint().GetTargetSP().get());
                }
            }
            else if (addr_type == eAddressTypeHost || addr_type == eAddressTypeInvalid)
                value = LLDB_INVALID_ADDRESS;
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::GetLoadAddress () => (%llu)", m_opaque_sp.get(), value);
    
    return value;
}

lldb::SBAddress
SBValue::GetAddress()
{
    Address addr;
    if (m_opaque_sp)
    {
        Target* target = m_opaque_sp->GetUpdatePoint().GetTargetSP().get();
        if (target)
        {
            lldb::addr_t value = LLDB_INVALID_ADDRESS;
            Mutex::Locker api_locker (target->GetAPIMutex());
            const bool scalar_is_load_address = true;
            AddressType addr_type;
            value = m_opaque_sp->GetAddressOf(scalar_is_load_address, &addr_type);
            if (addr_type == eAddressTypeFile)
            {
                Module* module = m_opaque_sp->GetModule();
                if (module)
                    module->ResolveFileAddress(value, addr);
            }
            else if (addr_type == eAddressTypeLoad)
            {
                // no need to check the return value on this.. if it can actually do the resolve
                // addr will be in the form (section,offset), otherwise it will simply be returned
                // as (NULL, value)
                addr.SetLoadAddress(value, target);
            }
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::GetAddress () => (%s,%llu)", m_opaque_sp.get(), (addr.GetSection() ? addr.GetSection()->GetName().GetCString() : "NULL"), addr.GetOffset());
    return SBAddress(new Address(addr));
}

lldb::SBData
SBValue::GetPointeeData (uint32_t item_idx,
                         uint32_t item_count)
{
    lldb::SBData sb_data;
    if (m_opaque_sp)
    {
        Target* target = m_opaque_sp->GetUpdatePoint().GetTargetSP().get();
        if (target)
        {
			DataExtractorSP data_sp(new DataExtractor());
            Mutex::Locker api_locker (target->GetAPIMutex());
            m_opaque_sp->GetPointeeData(*data_sp, item_idx, item_count);
            if (data_sp->GetByteSize() > 0)
                *sb_data = data_sp;
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::GetPointeeData (%d, %d) => SBData(%p)",
                     m_opaque_sp.get(),
                     item_idx,
                     item_count,
                     sb_data.get());
    
    return sb_data;
}

lldb::SBData
SBValue::GetData ()
{
    lldb::SBData sb_data;
    if (m_opaque_sp)
    {
        Target* target = m_opaque_sp->GetUpdatePoint().GetTargetSP().get();
        if (target)
        {
			DataExtractorSP data_sp(new DataExtractor());
            Mutex::Locker api_locker (target->GetAPIMutex());
            m_opaque_sp->GetData(*data_sp);
            if (data_sp->GetByteSize() > 0)
                *sb_data = data_sp;
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::GetData () => SBData(%p)",
                     m_opaque_sp.get(),
                     sb_data.get());
    
    return sb_data;
}

lldb::SBWatchpoint
SBValue::Watch (bool resolve_location, bool read, bool write)
{
    lldb::SBWatchpoint sb_watchpoint;
    if (!m_opaque_sp)
        return sb_watchpoint;

    Target* target = m_opaque_sp->GetUpdatePoint().GetTargetSP().get();
    if (target)
    {
        Mutex::Locker api_locker (target->GetAPIMutex());
        sb_watchpoint = WatchValue(read, write, false);
    }
    LogSP log(GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::Watch (resolve_location=%i, read=%i, write=%i) => wp(%p)", 
                     m_opaque_sp.get(), resolve_location, read, write, sb_watchpoint.get());
    return sb_watchpoint;
}

lldb::SBWatchpoint
SBValue::WatchPointee (bool resolve_location, bool read, bool write)
{
    lldb::SBWatchpoint sb_watchpoint;
    if (!m_opaque_sp)
        return sb_watchpoint;

    Target* target = m_opaque_sp->GetUpdatePoint().GetTargetSP().get();
    if (target)
    {
        Mutex::Locker api_locker (target->GetAPIMutex());
        sb_watchpoint = WatchValue(read, write, true);
    }
    LogSP log(GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBValue(%p)::WatchPointee (resolve_location=%i, read=%i, write=%i) => wp(%p)", 
                     m_opaque_sp.get(), resolve_location, read, write, sb_watchpoint.get());
    return sb_watchpoint;
}

// Helper function for SBValue::Watch() and SBValue::WatchPointee().
SBWatchpoint
SBValue::WatchValue(bool read, bool write, bool watch_pointee)
{
    SBWatchpoint sb_wp_empty;

    // If the SBValue is not valid, there's no point in even trying to watch it.
    if (!IsValid() || !GetFrame().IsValid())
        return sb_wp_empty;

    // Read and Write cannot both be false.
    if (!read && !write)
        return sb_wp_empty;

    // If we are watching the pointee, check that the SBValue is a pointer type.
    if (watch_pointee && !GetType().IsPointerType())
        return sb_wp_empty;

    addr_t addr;
    size_t size;
    if (watch_pointee) {
        addr = GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
        size = GetType().GetPointeeType().GetByteSize();
    } else {
        addr = GetLoadAddress();
        size = GetByteSize();
    }

    // Sanity check the address and the size before calling Target::CreateWatchpoint().
    if (addr == LLDB_INVALID_ADDRESS || size == 0)
        return sb_wp_empty;

    uint32_t watch_type = (read ? LLDB_WATCH_TYPE_READ : 0) |
        (write ? LLDB_WATCH_TYPE_WRITE : 0);
    WatchpointSP wp_sp = GetFrame().m_opaque_sp->GetThread().GetProcess().GetTarget().
        CreateWatchpoint(addr, size, watch_type);

    if (wp_sp) {
        // StackFrame::GetInScopeVariableList(true) to get file globals as well.
        VariableListSP var_list_sp(GetFrame().m_opaque_sp->GetInScopeVariableList(true));
        VariableSP var_sp = var_list_sp->FindVariable(ConstString(GetName()));
        if (var_sp && var_sp->GetDeclaration().GetFile()) {
            StreamString ss;
            // True to show fullpath for declaration file.
            var_sp->GetDeclaration().DumpStopContext(&ss, true);
            wp_sp->SetDeclInfo(ss.GetString());
        }
    }
    return wp_sp;
}

