//===-- ValueObjectRegister.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "lldb/Core/ValueObjectRegister.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/Module.h"
#include "lldb/Symbol/ClangASTType.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"

using namespace lldb;
using namespace lldb_private;

#pragma mark ValueObjectRegisterContext

ValueObjectRegisterContext::ValueObjectRegisterContext (ValueObject &parent, RegisterContextSP &reg_ctx) :
    ValueObject (parent),
    m_reg_ctx_sp (reg_ctx)
{
    assert (reg_ctx);
    m_name.SetCString("Registers");
    SetValueIsValid (true);
}

ValueObjectRegisterContext::~ValueObjectRegisterContext()
{
}

lldb::clang_type_t
ValueObjectRegisterContext::GetClangType ()
{
    return NULL;
}

ConstString
ValueObjectRegisterContext::GetTypeName()
{
    ConstString empty_type_name;
    return empty_type_name;
}

uint32_t
ValueObjectRegisterContext::CalculateNumChildren()
{
    return m_reg_ctx_sp->GetRegisterSetCount();
}

clang::ASTContext *
ValueObjectRegisterContext::GetClangAST ()
{
    return NULL;
}

size_t
ValueObjectRegisterContext::GetByteSize()
{
    return 0;
}

bool
ValueObjectRegisterContext::UpdateValue ()
{
    m_error.Clear();
    ExecutionContextScope *exe_scope = GetExecutionContextScope();
    StackFrame *frame = exe_scope->CalculateStackFrame();
    if (frame)
        m_reg_ctx_sp = frame->GetRegisterContext();
    else
        m_reg_ctx_sp.reset();

    if (m_reg_ctx_sp.get() == NULL)
    {
        SetValueIsValid (false);
        m_error.SetErrorToGenericError();
    }
    else
        SetValueIsValid (true);
        
    return m_error.Success();
}

ValueObject *
ValueObjectRegisterContext::CreateChildAtIndex (uint32_t idx, bool synthetic_array_member, int32_t synthetic_index)
{
    ValueObject *new_valobj = NULL;
    
    const uint32_t num_children = GetNumChildren();
    if (idx < num_children)
        new_valobj = new ValueObjectRegisterSet(GetExecutionContextScope(), m_reg_ctx_sp, idx);
    
    return new_valobj;
}


#pragma mark -
#pragma mark ValueObjectRegisterSet

ValueObjectSP
ValueObjectRegisterSet::Create (ExecutionContextScope *exe_scope, lldb::RegisterContextSP &reg_ctx_sp, uint32_t set_idx)
{
    return (new ValueObjectRegisterSet (exe_scope, reg_ctx_sp, set_idx))->GetSP();
}


ValueObjectRegisterSet::ValueObjectRegisterSet (ExecutionContextScope *exe_scope, lldb::RegisterContextSP &reg_ctx, uint32_t reg_set_idx) :
    ValueObject (exe_scope),
    m_reg_ctx_sp (reg_ctx),
    m_reg_set (NULL),
    m_reg_set_idx (reg_set_idx)
{
    assert (reg_ctx);
    m_reg_set = reg_ctx->GetRegisterSet(m_reg_set_idx);
    if (m_reg_set)
    {
        m_name.SetCString (m_reg_set->name);
    }
}

ValueObjectRegisterSet::~ValueObjectRegisterSet()
{
}

lldb::clang_type_t
ValueObjectRegisterSet::GetClangType ()
{
    return NULL;
}

ConstString
ValueObjectRegisterSet::GetTypeName()
{
    return ConstString();
}

uint32_t
ValueObjectRegisterSet::CalculateNumChildren()
{
    const RegisterSet *reg_set = m_reg_ctx_sp->GetRegisterSet(m_reg_set_idx);
    if (reg_set)
        return reg_set->num_registers;
    return 0;
}

clang::ASTContext *
ValueObjectRegisterSet::GetClangAST ()
{
    return NULL;
}

size_t
ValueObjectRegisterSet::GetByteSize()
{
    return 0;
}

bool
ValueObjectRegisterSet::UpdateValue ()
{
    m_error.Clear();
    SetValueDidChange (false);
    ExecutionContextScope *exe_scope = GetExecutionContextScope();
    StackFrame *frame = exe_scope->CalculateStackFrame();
    if (frame == NULL)
        m_reg_ctx_sp.reset();
    else
    {
        m_reg_ctx_sp = frame->GetRegisterContext ();
        if (m_reg_ctx_sp)
        {
            const RegisterSet *reg_set = m_reg_ctx_sp->GetRegisterSet (m_reg_set_idx);
            if (reg_set == NULL)
                m_reg_ctx_sp.reset();
            else if (m_reg_set != reg_set)
            {
                SetValueDidChange (true);
                m_name.SetCString(reg_set->name);
            }
        }
    }
    if (m_reg_ctx_sp)
    {
        SetValueIsValid (true);
    }
    else
    {
        SetValueIsValid (false);
        m_error.SetErrorToGenericError ();
        m_children.clear();
    }
    return m_error.Success();
}


ValueObject *
ValueObjectRegisterSet::CreateChildAtIndex (uint32_t idx, bool synthetic_array_member, int32_t synthetic_index)
{
    ValueObject *valobj = NULL;
    if (m_reg_ctx_sp && m_reg_set)
    {
        const uint32_t num_children = GetNumChildren();
        if (idx < num_children)
            valobj = new ValueObjectRegister(*this, m_reg_ctx_sp, m_reg_set->registers[idx]);
    }
    return valobj;
}

lldb::ValueObjectSP
ValueObjectRegisterSet::GetChildMemberWithName (const ConstString &name, bool can_create)
{
    ValueObject *valobj = NULL;
    if (m_reg_ctx_sp && m_reg_set)
    {
        const RegisterInfo *reg_info = m_reg_ctx_sp->GetRegisterInfoByName (name.AsCString());
        if (reg_info != NULL)
            valobj = new ValueObjectRegister(*this, m_reg_ctx_sp, reg_info->kinds[eRegisterKindLLDB]);
    }
    if (valobj)
        return valobj->GetSP();
    else
        return ValueObjectSP();
}

uint32_t
ValueObjectRegisterSet::GetIndexOfChildWithName (const ConstString &name)
{
    if (m_reg_ctx_sp && m_reg_set)
    {
        const RegisterInfo *reg_info = m_reg_ctx_sp->GetRegisterInfoByName (name.AsCString());
        if (reg_info != NULL)
            return reg_info->kinds[eRegisterKindLLDB];
    }
    return UINT32_MAX;
}

#pragma mark -
#pragma mark ValueObjectRegister

void
ValueObjectRegister::ConstructObject (uint32_t reg_num)
{
    const RegisterInfo *reg_info = m_reg_ctx_sp->GetRegisterInfoAtIndex (reg_num);
    if (reg_info)
    {
        m_reg_info = *reg_info;
        if (reg_info->name)
            m_name.SetCString(reg_info->name);
        else if (reg_info->alt_name)
            m_name.SetCString(reg_info->alt_name);
    }
}

ValueObjectRegister::ValueObjectRegister (ValueObject &parent, lldb::RegisterContextSP &reg_ctx_sp, uint32_t reg_num) :
    ValueObject (parent),
    m_reg_ctx_sp (reg_ctx_sp),
    m_reg_info (),
    m_reg_value (),
    m_type_name (),
    m_clang_type (NULL)
{
    assert (reg_ctx_sp.get());
    ConstructObject(reg_num);
}

ValueObjectSP
ValueObjectRegister::Create (ExecutionContextScope *exe_scope, lldb::RegisterContextSP &reg_ctx_sp, uint32_t reg_num)
{
    return (new ValueObjectRegister (exe_scope, reg_ctx_sp, reg_num))->GetSP();
}

ValueObjectRegister::ValueObjectRegister (ExecutionContextScope *exe_scope, lldb::RegisterContextSP &reg_ctx, uint32_t reg_num) :
    ValueObject (exe_scope),
    m_reg_ctx_sp (reg_ctx),
    m_reg_info (),
    m_reg_value (),
    m_type_name (),
    m_clang_type (NULL)
{
    assert (reg_ctx);
    ConstructObject(reg_num);
}

ValueObjectRegister::~ValueObjectRegister()
{
}

lldb::clang_type_t
ValueObjectRegister::GetClangType ()
{
    if (m_clang_type == NULL)
    {
        Process *process = m_reg_ctx_sp->CalculateProcess ();
        if (process)
        {
            Module *exe_module = process->GetTarget().GetExecutableModulePointer();
            if (exe_module)
            {
                m_clang_type = exe_module->GetClangASTContext().GetBuiltinTypeForEncodingAndBitSize (m_reg_info.encoding, 
                                                                                                     m_reg_info.byte_size * 8);
            }
        }
    }
    return m_clang_type;
}

ConstString
ValueObjectRegister::GetTypeName()
{
    if (m_type_name.IsEmpty())
        m_type_name = ClangASTType::GetConstTypeName (GetClangType());
    return m_type_name;
}

uint32_t
ValueObjectRegister::CalculateNumChildren()
{
    return 0;
}

clang::ASTContext *
ValueObjectRegister::GetClangAST ()
{
    Process *process = m_reg_ctx_sp->CalculateProcess ();
    if (process)
    {
        Module *exe_module = process->GetTarget().GetExecutableModulePointer();
        if (exe_module)
            return exe_module->GetClangASTContext().getASTContext();
    }
    return NULL;
}

size_t
ValueObjectRegister::GetByteSize()
{
    return m_reg_info.byte_size;
}

bool
ValueObjectRegister::UpdateValue ()
{
    m_error.Clear();
    ExecutionContextScope *exe_scope = GetExecutionContextScope();
    StackFrame *frame = exe_scope->CalculateStackFrame();
    if (frame == NULL)
    {
        m_reg_ctx_sp.reset();
        m_reg_value.Clear();
    }


    if (m_reg_ctx_sp)
    {
        if (m_reg_ctx_sp->ReadRegister (&m_reg_info, m_reg_value))
        {
            if (m_reg_value.GetData (m_data))
            {
                m_data.SetAddressByteSize(m_reg_ctx_sp->GetThread().GetProcess().GetAddressByteSize());
                m_value.SetContext(Value::eContextTypeRegisterInfo, (void *)&m_reg_info);
                m_value.SetValueType(Value::eValueTypeHostAddress);
                m_value.GetScalar() = (uintptr_t)m_data.GetDataStart();
                SetValueIsValid (true);
                return true;
            }
        }
    }
    
    SetValueIsValid (false);
    m_error.SetErrorToGenericError ();
    return false;
}

bool
ValueObjectRegister::SetValueFromCString (const char *value_str)
{
    // The new value will be in the m_data.  Copy that into our register value.
    Error error = m_reg_value.SetValueFromCString (&m_reg_info, value_str); 
    if (error.Success())
    {
        if (m_reg_ctx_sp->WriteRegister (&m_reg_info, m_reg_value))
        {
            SetNeedsUpdate();
            return true;
        }
        else
            return false;
    }
    else
        return false;
}

bool
ValueObjectRegister::ResolveValue (Scalar &scalar)
{
    if (UpdateValueIfNeeded(false)) // make sure that you are up to date before returning anything
        return m_reg_value.GetScalarValue(scalar);
    return false;
}


