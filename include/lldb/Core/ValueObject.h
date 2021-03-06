//===-- ValueObject.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ValueObject_h_
#define liblldb_ValueObject_h_

// C Includes
// C++ Includes
#include <map>
#include <vector>
// Other libraries and framework includes
// Project includes

#include "lldb/lldb-private.h"
#include "lldb/Core/DataExtractor.h"
#include "lldb/Core/Error.h"
#include "lldb/Core/Flags.h"
#include "lldb/Core/ConstString.h"
#include "lldb/Core/UserID.h"
#include "lldb/Core/Value.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackID.h"
#include "lldb/Utility/PriorityPointerPair.h"
#include "lldb/Utility/SharedCluster.h"

namespace lldb_private {

/// ValueObject:
///
/// This abstract class provides an interface to a particular value, be it a register, a local or global variable,
/// that is evaluated in some particular scope.  The ValueObject also has the capibility of being the "child" of
/// some other variable object, and in turn of having children.  
/// If a ValueObject is a root variable object - having no parent - then it must be constructed with respect to some
/// particular ExecutionContextScope.  If it is a child, it inherits the ExecutionContextScope from its parent.
/// The ValueObject will update itself if necessary before fetching its value, summary, object description, etc.
/// But it will always update itself in the ExecutionContextScope with which it was originally created.

/// A brief note on life cycle management for ValueObjects.  This is a little tricky because a ValueObject can contain
/// various other ValueObjects - the Dynamic Value, its children, the dereference value, etc.  Any one of these can be
/// handed out as a shared pointer, but for that contained value object to be valid, the root object and potentially other
/// of the value objects need to stay around.  
/// We solve this problem by handing out shared pointers to the Value Object and any of its dependents using a shared
/// ClusterManager.  This treats each shared pointer handed out for the entire cluster as a reference to the whole
/// cluster.  The whole cluster will stay around until the last reference is released.
///
/// The ValueObject mostly handle this automatically, if a value object is made with a Parent ValueObject, then it adds
/// itself to the ClusterManager of the parent.

/// It does mean that external to the ValueObjects we should only ever make available ValueObjectSP's, never ValueObjects 
/// or pointers to them.  So all the "Root level" ValueObject derived constructors should be private, and 
/// should implement a Create function that new's up object and returns a Shared Pointer that it gets from the GetSP() method.
///
/// However, if you are making an derived ValueObject that will be contained in a parent value object, you should just
/// hold onto a pointer to it internally, and by virtue of passing the parent ValueObject into its constructor, it will
/// be added to the ClusterManager for the parent.  Then if you ever hand out a Shared Pointer to the contained ValueObject,
/// just do so by calling GetSP() on the contained object.

class ValueObject : public UserID
{
public:
    
    enum GetExpressionPathFormat
    {
        eDereferencePointers = 1,
        eHonorPointers
    };
    
    enum ValueObjectRepresentationStyle
    {
        eDisplayValue = 1,
        eDisplaySummary,
        eDisplayLanguageSpecific,
        eDisplayLocation,
        eDisplayChildrenCount,
        eDisplayType
    };
    
    enum ExpressionPathScanEndReason
    {
        eEndOfString = 1,           // out of data to parse
        eNoSuchChild,               // child element not found
        eEmptyRangeNotAllowed,      // [] only allowed for arrays
        eDotInsteadOfArrow,         // . used when -> should be used
        eArrowInsteadOfDot,         // -> used when . should be used
        eFragileIVarNotAllowed,     // ObjC ivar expansion not allowed
        eRangeOperatorNotAllowed,   // [] not allowed by options
        eRangeOperatorInvalid,      // [] not valid on objects other than scalars, pointers or arrays
        eArrayRangeOperatorMet,     // [] is good for arrays, but I cannot parse it
        eBitfieldRangeOperatorMet,  // [] is good for bitfields, but I cannot parse after it
        eUnexpectedSymbol,          // something is malformed in the expression
        eTakingAddressFailed,       // impossible to apply & operator
        eDereferencingFailed,       // impossible to apply * operator
        eRangeOperatorExpanded,     // [] was expanded into a VOList
        eUnknown = 0xFFFF
    };
    
    enum ExpressionPathEndResultType
    {
        ePlain = 1,                 // anything but...
        eBitfield,                  // a bitfield
        eBoundedRange,              // a range [low-high]
        eUnboundedRange,            // a range []
        eValueObjectList,           // several items in a VOList
        eInvalid = 0xFFFF
    };
    
    enum ExpressionPathAftermath
    {
        eNothing = 1,               // just return it
        eDereference,               // dereference the target
        eTakeAddress                // take target's address
    };
    
    struct GetValueForExpressionPathOptions
    {
        bool m_check_dot_vs_arrow_syntax;
        bool m_no_fragile_ivar;
        bool m_allow_bitfields_syntax;
        bool m_no_synthetic_children;
        
        GetValueForExpressionPathOptions(bool dot = false,
                                         bool no_ivar = false,
                                         bool bitfield = true,
                                         bool no_synth = false) :
            m_check_dot_vs_arrow_syntax(dot),
            m_no_fragile_ivar(no_ivar),
            m_allow_bitfields_syntax(bitfield),
            m_no_synthetic_children(no_synth)
        {
        }
        
        GetValueForExpressionPathOptions&
        DoCheckDotVsArrowSyntax()
        {
            m_check_dot_vs_arrow_syntax = true;
            return *this;
        }
        
        GetValueForExpressionPathOptions&
        DontCheckDotVsArrowSyntax()
        {
            m_check_dot_vs_arrow_syntax = false;
            return *this;
        }
        
        GetValueForExpressionPathOptions&
        DoAllowFragileIVar()
        {
            m_no_fragile_ivar = false;
            return *this;
        }
        
        GetValueForExpressionPathOptions&
        DontAllowFragileIVar()
        {
            m_no_fragile_ivar = true;
            return *this;
        }

        GetValueForExpressionPathOptions&
        DoAllowBitfieldSyntax()
        {
            m_allow_bitfields_syntax = true;
            return *this;
        }
        
        GetValueForExpressionPathOptions&
        DontAllowBitfieldSyntax()
        {
            m_allow_bitfields_syntax = false;
            return *this;
        }
        
        GetValueForExpressionPathOptions&
        DoAllowSyntheticChildren()
        {
            m_no_synthetic_children = false;
            return *this;
        }
        
        GetValueForExpressionPathOptions&
        DontAllowSyntheticChildren()
        {
            m_no_synthetic_children = true;
            return *this;
        }
        
        static const GetValueForExpressionPathOptions
        DefaultOptions()
        {
            static GetValueForExpressionPathOptions g_default_options;
            
            return g_default_options;
        }

    };
    
    struct DumpValueObjectOptions
    {
        uint32_t m_ptr_depth;
        uint32_t m_max_depth;
        bool m_show_types;
        bool m_show_location;
        bool m_use_objc;
        lldb::DynamicValueType m_use_dynamic;
        lldb::SyntheticValueType m_use_synthetic;
        bool m_scope_already_checked;
        bool m_flat_output;
        uint32_t m_omit_summary_depth;
        bool m_ignore_cap;
        
        DumpValueObjectOptions() :
        m_ptr_depth(0),
        m_max_depth(UINT32_MAX),
        m_show_types(false),
        m_show_location(false),
        m_use_objc(false),
        m_use_dynamic(lldb::eNoDynamicValues),
        m_use_synthetic(lldb::eUseSyntheticFilter),
        m_scope_already_checked(false),
        m_flat_output(false),
        m_omit_summary_depth(0),
        m_ignore_cap(false)
        {}
        
        static const DumpValueObjectOptions
        DefaultOptions()
        {
            static DumpValueObjectOptions g_default_options;
            
            return g_default_options;
        }
        
        DumpValueObjectOptions&
        SetPointerDepth(uint32_t depth = 0)
        {
            m_ptr_depth = depth;
            return *this;
        }
        
        DumpValueObjectOptions&
        SetMaximumDepth(uint32_t depth = 0)
        {
            m_max_depth = depth;
            return *this;
        }
        
        DumpValueObjectOptions&
        SetShowTypes(bool show = false)
        {
            m_show_types = show;
            return *this;
        }
        
        DumpValueObjectOptions&
        SetShowLocation(bool show = false)
        {
            m_show_location = show;
            return *this;
        }

        DumpValueObjectOptions&
        SetUseObjectiveC(bool use = false)
        {
            m_use_objc = use;
            return *this;
        }
        
        DumpValueObjectOptions&
        SetUseDynamicType(lldb::DynamicValueType dyn = lldb::eNoDynamicValues)
        {
            m_use_dynamic = dyn;
            return *this;
        }
        
        DumpValueObjectOptions&
        SetUseSyntheticValue(lldb::SyntheticValueType syn = lldb::eUseSyntheticFilter)
        {
            m_use_synthetic = syn;
            return *this;
        }

        DumpValueObjectOptions&
        SetScopeChecked(bool check = true)
        {
            m_scope_already_checked = check;
            return *this;
        }
        
        DumpValueObjectOptions&
        SetFlatOutput(bool flat = false)
        {
            m_flat_output = flat;
            return *this;
        }
        
        DumpValueObjectOptions&
        SetOmitSummaryDepth(uint32_t depth = 0)
        {
            m_omit_summary_depth = depth;
            return *this;
        }
        
        DumpValueObjectOptions&
        SetIgnoreCap(bool ignore = false)
        {
            m_ignore_cap = ignore;
            return *this;
        }

        DumpValueObjectOptions&
        SetRawDisplay(bool raw = false)
        {
            if (raw)
            {
                SetUseSyntheticValue(lldb::eNoSyntheticFilter);
                SetOmitSummaryDepth(UINT32_MAX);
                SetIgnoreCap(true);
            }
            else
            {
                SetUseSyntheticValue(lldb::eUseSyntheticFilter);
                SetOmitSummaryDepth(0);
                SetIgnoreCap(false);
            }
            return *this;
        }

    };

    class EvaluationPoint : public ExecutionContextScope
    {
    public:
        
        EvaluationPoint ();
        
        EvaluationPoint (ExecutionContextScope *exe_scope, bool use_selected = false);
        
        EvaluationPoint (const EvaluationPoint &rhs);
        
        ~EvaluationPoint ();
        
        const lldb::TargetSP &
        GetTargetSP () const
        {
            return m_target_sp;
        }
        
        const lldb::ProcessSP &
        GetProcessSP () const
        {
            return m_process_sp;
        }
                
        // Set the EvaluationPoint to the values in exe_scope,
        // Return true if the Evaluation Point changed.
        // Since the ExecutionContextScope is always going to be valid currently, 
        // the Updated Context will also always be valid.
        
        bool
        SetContext (ExecutionContextScope *exe_scope);
        
        void
        SetIsConstant ()
        {
            SetUpdated();
            m_mod_id.SetInvalid();
        }
        
        bool
        IsConstant () const
        {
            return !m_mod_id.IsValid();
        }
        
        ProcessModID
        GetModID () const
        {
            return m_mod_id;
        }

        void
        SetUpdateID (ProcessModID new_id)
        {
            m_mod_id = new_id;
        }
        
        bool
        IsFirstEvaluation () const
        {
            return m_first_update;
        }
        
        void
        SetNeedsUpdate ()
        {
            m_needs_update = true;
        }
        
        void
        SetUpdated ();
        
        bool
        NeedsUpdating()
        {
            SyncWithProcessState();
            return m_needs_update;
        }
        
        bool
        IsValid ()
        {
            if (!m_mod_id.IsValid())
                return false;
            else if (SyncWithProcessState ())
            {
                if (!m_mod_id.IsValid())
                    return false;
            }
            return true;
        }
        
        void
        SetInvalid ()
        {
            // Use the stop id to mark us as invalid, leave the thread id and the stack id around for logging and
            // history purposes.
            m_mod_id.SetInvalid();
            
            // Can't update an invalid state.
            m_needs_update = false;
            
        }
        
        // If this EvaluationPoint is created without a target, then we could have it
        // hand out a NULL ExecutionContextScope.  But then everybody would have to check that before
        // calling through it, which is annoying.  So instead, we make the EvaluationPoint BE an
        // ExecutionContextScope, and it hands out the right things.
        virtual Target *CalculateTarget ();
        
        virtual Process *CalculateProcess ();
        
        virtual Thread *CalculateThread ();
        
        virtual StackFrame *CalculateStackFrame ();
        
        virtual void CalculateExecutionContext (ExecutionContext &exe_ctx);
        
    private:
        bool
        SyncWithProcessState ()
        {
            ExecutionContextScope *exe_scope;
            return SyncWithProcessState(exe_scope);
        }
        
        bool
        SyncWithProcessState (ExecutionContextScope *&exe_scope);
                
        bool             m_needs_update;
        bool             m_first_update;

        lldb::TargetSP   m_target_sp;
        lldb::ProcessSP  m_process_sp;
        lldb::user_id_t  m_thread_id;
        StackID          m_stack_id;
        ProcessModID     m_mod_id; // This is the stop id when this ValueObject was last evaluated.
    };

    const EvaluationPoint &
    GetUpdatePoint () const
    {
        return m_update_point;
    }
    
    EvaluationPoint &
    GetUpdatePoint ()
    {
        return m_update_point;
    }
    
    ExecutionContextScope *
    GetExecutionContextScope ()
    {
        return &m_update_point;
    }
    
    void
    SetNeedsUpdate ();
    
    virtual ~ValueObject();

    //------------------------------------------------------------------
    // Sublasses must implement the functions below.
    //------------------------------------------------------------------
    virtual size_t
    GetByteSize() = 0;

    virtual clang::ASTContext *
    GetClangAST () = 0;

    virtual lldb::clang_type_t
    GetClangType () = 0;

    virtual lldb::ValueType
    GetValueType() const = 0;

    virtual ConstString
    GetTypeName() = 0;

    virtual lldb::LanguageType
    GetObjectRuntimeLanguage();

    virtual bool
    IsPointerType ();
    
    virtual bool
    IsArrayType ();
    
    virtual bool
    IsScalarType ();

    virtual bool
    IsPointerOrReferenceType ();
    
    virtual bool
    IsPossibleCPlusPlusDynamicType ();
    
    virtual bool
    IsPossibleDynamicType ();

    virtual bool
    IsBaseClass ()
    {
        return false;
    }
    
    virtual bool
    IsDereferenceOfParent ()
    {
        return false;
    }
    
    bool
    IsIntegerType (bool &is_signed);
    
    virtual bool
    GetBaseClassPath (Stream &s);

    virtual void
    GetExpressionPath (Stream &s, bool qualify_cxx_base_classes, GetExpressionPathFormat = eDereferencePointers);
    
    lldb::ValueObjectSP
    GetValueForExpressionPath(const char* expression,
                              const char** first_unparsed = NULL,
                              ExpressionPathScanEndReason* reason_to_stop = NULL,
                              ExpressionPathEndResultType* final_value_type = NULL,
                              const GetValueForExpressionPathOptions& options = GetValueForExpressionPathOptions::DefaultOptions(),
                              ExpressionPathAftermath* final_task_on_target = NULL);
    
    int
    GetValuesForExpressionPath(const char* expression,
                               lldb::ValueObjectListSP& list,
                               const char** first_unparsed = NULL,
                               ExpressionPathScanEndReason* reason_to_stop = NULL,
                               ExpressionPathEndResultType* final_value_type = NULL,
                               const GetValueForExpressionPathOptions& options = GetValueForExpressionPathOptions::DefaultOptions(),
                               ExpressionPathAftermath* final_task_on_target = NULL);
    
    virtual bool
    IsInScope ()
    {
        return true;
    }

    virtual off_t
    GetByteOffset()
    {
        return 0;
    }

    virtual uint32_t
    GetBitfieldBitSize()
    {
        return 0;
    }

    virtual uint32_t
    GetBitfieldBitOffset()
    {
        return 0;
    }
    
    virtual bool
    IsArrayItemForPointer()
    {
        return m_is_array_item_for_pointer;
    }
    
    virtual bool
    SetClangAST (clang::ASTContext *ast)
    {
        return false;
    }

    virtual const char *
    GetValueAsCString ();
    
    virtual uint64_t
    GetValueAsUnsigned (uint64_t fail_value);

    virtual bool
    SetValueFromCString (const char *value_str);

    // Return the module associated with this value object in case the
    // value is from an executable file and might have its data in
    // sections of the file. This can be used for variables.
    virtual Module *
    GetModule()
    {
        if (m_parent)
            return m_parent->GetModule();
        return NULL;
    }
    //------------------------------------------------------------------
    // The functions below should NOT be modified by sublasses
    //------------------------------------------------------------------
    const Error &
    GetError();

    const ConstString &
    GetName() const;

    virtual lldb::ValueObjectSP
    GetChildAtIndex (uint32_t idx, bool can_create);

    virtual lldb::ValueObjectSP
    GetChildMemberWithName (const ConstString &name, bool can_create);

    virtual uint32_t
    GetIndexOfChildWithName (const ConstString &name);

    uint32_t
    GetNumChildren ();

    const Value &
    GetValue() const;

    Value &
    GetValue();

    virtual bool
    ResolveValue (Scalar &scalar);
    
    const char *
    GetLocationAsCString ();

    const char *
    GetSummaryAsCString ();
    
    const char *
    GetObjectDescription ();
    
    bool
    GetPrintableRepresentation(Stream& s,
                               ValueObjectRepresentationStyle val_obj_display = eDisplaySummary,
                               lldb::Format custom_format = lldb::eFormatInvalid);

    bool
    HasSpecialCasesForPrintableRepresentation(ValueObjectRepresentationStyle val_obj_display,
                                              lldb::Format custom_format);
    
    bool
    DumpPrintableRepresentation(Stream& s,
                                ValueObjectRepresentationStyle val_obj_display = eDisplaySummary,
                                lldb::Format custom_format = lldb::eFormatInvalid,
                                bool only_special = false);
    bool
    GetValueIsValid () const;

    bool
    GetValueDidChange ();

    bool
    UpdateValueIfNeeded (bool update_format = true);
    
    bool
    UpdateValueIfNeeded (lldb::DynamicValueType use_dynamic, bool update_format = true);
    
    bool
    UpdateFormatsIfNeeded(lldb::DynamicValueType use_dynamic = lldb::eNoDynamicValues);

    lldb::ValueObjectSP
    GetSP ()
    {
        return m_manager->GetSharedPointer(this);
    }
    
    void
    SetName (const ConstString &name);
    
    virtual lldb::addr_t
    GetAddressOf (bool scalar_is_load_address = true,
                  AddressType *address_type = NULL);
    
    lldb::addr_t
    GetPointerValue (AddressType *address_type = NULL);
    
    lldb::ValueObjectSP
    GetSyntheticChild (const ConstString &key) const;
    
    lldb::ValueObjectSP
    GetSyntheticArrayMember (int32_t index, bool can_create);

    lldb::ValueObjectSP
    GetSyntheticArrayMemberFromPointer (int32_t index, bool can_create);
    
    lldb::ValueObjectSP
    GetSyntheticArrayMemberFromArray (int32_t index, bool can_create);
    
    lldb::ValueObjectSP
    GetSyntheticBitFieldChild (uint32_t from, uint32_t to, bool can_create);
    
    lldb::ValueObjectSP
    GetSyntheticArrayRangeChild (uint32_t from, uint32_t to, bool can_create);
    
    lldb::ValueObjectSP
    GetSyntheticExpressionPathChild(const char* expression, bool can_create);
    
    virtual lldb::ValueObjectSP
    GetSyntheticChildAtOffset(uint32_t offset, const ClangASTType& type, bool can_create);
    
    lldb::ValueObjectSP
    GetDynamicValue (lldb::DynamicValueType valueType);
    
    virtual lldb::ValueObjectSP
    GetStaticValue ();
    
    lldb::ValueObjectSP
    GetSyntheticValue (lldb::SyntheticValueType use_synthetic);
    
    virtual bool
    HasSyntheticValue();
    
    virtual lldb::ValueObjectSP
    CreateConstantValue (const ConstString &name);

    virtual lldb::ValueObjectSP
    Dereference (Error &error);
    
    virtual lldb::ValueObjectSP
    AddressOf (Error &error);
    
    virtual lldb::addr_t
    GetLiveAddress()
    {
        return LLDB_INVALID_ADDRESS;
    }
    
    virtual void
    SetLiveAddress(lldb::addr_t addr = LLDB_INVALID_ADDRESS,
                   AddressType address_type = eAddressTypeLoad)
    {
    }

    virtual lldb::ValueObjectSP
    CastPointerType (const char *name,
                     ClangASTType &ast_type);

    virtual lldb::ValueObjectSP
    CastPointerType (const char *name,
                     lldb::TypeSP &type_sp);

    // The backing bits of this value object were updated, clear any
    // descriptive string, so we know we have to refetch them
    virtual void
    ValueUpdated ()
    {
        m_value_str.clear();
        m_summary_str.clear();
        m_object_desc_str.clear();
    }

    virtual bool
    IsDynamic ()
    {
        return false;
    }
    
    virtual SymbolContextScope *
    GetSymbolContextScope();
    
    static void
    DumpValueObject (Stream &s,
                     ValueObject *valobj)
    {
        
        if (!valobj)
            return;
        
        ValueObject::DumpValueObject(s,
                                     valobj,
                                     DumpValueObjectOptions::DefaultOptions());
    }
    
    static void
    DumpValueObject (Stream &s,
                     ValueObject *valobj,
                     const char *root_valobj_name)
    {
        
        if (!valobj)
            return;
        
        ValueObject::DumpValueObject(s,
                                     valobj,
                                     root_valobj_name,
                                     DumpValueObjectOptions::DefaultOptions());
    }

    static void
    DumpValueObject (Stream &s,
                     ValueObject *valobj,
                     const DumpValueObjectOptions& options)
    {
        
        if (!valobj)
            return;
        
        ValueObject::DumpValueObject(s,
                                     valobj,
                                     valobj->GetName().AsCString(),
                                     options.m_ptr_depth,
                                     0,
                                     options.m_max_depth,
                                     options.m_show_types,
                                     options.m_show_location,
                                     options.m_use_objc,
                                     options.m_use_dynamic,
                                     options.m_use_synthetic,
                                     options.m_scope_already_checked,
                                     options.m_flat_output,
                                     options.m_omit_summary_depth,
                                     options.m_ignore_cap);
    }
                     
    static void
    DumpValueObject (Stream &s,
                     ValueObject *valobj,
                     const char *root_valobj_name,
                     const DumpValueObjectOptions& options)
    {
        
        if (!valobj)
            return;
        
        ValueObject::DumpValueObject(s,
                                     valobj,
                                     root_valobj_name,
                                     options.m_ptr_depth,
                                     0,
                                     options.m_max_depth,
                                     options.m_show_types,
                                     options.m_show_location,
                                     options.m_use_objc,
                                     options.m_use_dynamic,
                                     options.m_use_synthetic,
                                     options.m_scope_already_checked,
                                     options.m_flat_output,
                                     options.m_omit_summary_depth,
                                     options.m_ignore_cap);
    }
    
    static void
    DumpValueObject (Stream &s,
                     ValueObject *valobj,
                     const char *root_valobj_name,
                     uint32_t ptr_depth,
                     uint32_t curr_depth,
                     uint32_t max_depth,
                     bool show_types,
                     bool show_location,
                     bool use_objc,
                     lldb::DynamicValueType use_dynamic,
                     bool use_synthetic,
                     bool scope_already_checked,
                     bool flat_output,
                     uint32_t omit_summary_depth,
                     bool ignore_cap);
    
    // returns true if this is a char* or a char[]
    // if it is a char* and check_pointer is true,
    // it also checks that the pointer is valid
    bool
    IsCStringContainer (bool check_pointer = false);
    
    void
    ReadPointedString (Stream& s,
                       Error& error,
                       uint32_t max_length = 0,
                       bool honor_array = true,
                       lldb::Format item_format = lldb::eFormatCharArray);
    
    virtual size_t
    GetPointeeData (DataExtractor& data,
                    uint32_t item_idx = 0,
					uint32_t item_count = 1);
    
    virtual size_t
    GetData (DataExtractor& data);

    bool
    GetIsConstant () const
    {
        return m_update_point.IsConstant();
    }
    
    void
    SetIsConstant ()
    {
        m_update_point.SetIsConstant();
    }

    lldb::Format
    GetFormat () const
    {
        if (m_parent && m_format == lldb::eFormatDefault)
            return m_parent->GetFormat();
        return m_format;
    }
    
    void
    SetFormat (lldb::Format format)
    {
        if (format != m_format)
            m_value_str.clear();
        m_format = format;
    }
    
    void
    SetCustomSummaryFormat(lldb::SummaryFormatSP format)
    {
        m_forced_summary_format = format;
        m_user_id_of_forced_summary = m_update_point.GetModID();
        m_summary_str.clear();
        m_trying_summary_already = false;
    }
    
    lldb::SummaryFormatSP
    GetCustomSummaryFormat()
    {
        return m_forced_summary_format;
    }
    
    void
    ClearCustomSummaryFormat()
    {
        m_forced_summary_format.reset();
        m_summary_str.clear();
    }
    
    bool
    HasCustomSummaryFormat()
    {
        return (m_forced_summary_format.get());
    }
    
    lldb::SummaryFormatSP
    GetSummaryFormat()
    {
        UpdateFormatsIfNeeded(m_last_format_mgr_dynamic);
        if (HasCustomSummaryFormat())
            return m_forced_summary_format;
        return m_last_summary_format;
    }
    
    void
    SetSummaryFormat(lldb::SummaryFormatSP format)
    {
        m_last_summary_format = format;
        m_summary_str.clear();
        m_trying_summary_already = false;
    }
    
    void
    SetValueFormat(lldb::ValueFormatSP format)
    {
        m_last_value_format = format;
        m_value_str.clear();
    }
    
    lldb::ValueFormatSP
    GetValueFormat()
    {
        UpdateFormatsIfNeeded(m_last_format_mgr_dynamic);
        return m_last_value_format;
    }
    
    void
    SetSyntheticChildren(lldb::SyntheticChildrenSP synth)
    {
        m_last_synthetic_filter = synth;
        m_synthetic_value = NULL;
    }
    
    lldb::SyntheticChildrenSP
    GetSyntheticChildren()
    {
        UpdateFormatsIfNeeded(m_last_format_mgr_dynamic);
        return m_last_synthetic_filter;
    }

    // Use GetParent for display purposes, but if you want to tell the parent to update itself
    // then use m_parent.  The ValueObjectDynamicValue's parent is not the correct parent for
    // displaying, they are really siblings, so for display it needs to route through to its grandparent.
    virtual ValueObject *
    GetParent()
    {
        return m_parent;
    }

    virtual const ValueObject *
    GetParent() const
    {
        return m_parent;
    }

    ValueObject *
    GetNonBaseClassParent();

    void
    SetAddressTypeOfChildren(AddressType at)
    {
        m_address_type_of_ptr_or_ref_children = at;
    }
    
    AddressType
    GetAddressTypeOfChildren()
    {
        if (m_address_type_of_ptr_or_ref_children == eAddressTypeInvalid)
        {
            if (m_parent)
                return m_parent->GetAddressTypeOfChildren();
        }
        return m_address_type_of_ptr_or_ref_children;
    }
    
protected:
    typedef ClusterManager<ValueObject> ValueObjectManager;

    //------------------------------------------------------------------
    // Classes that inherit from ValueObject can see and modify these
    //------------------------------------------------------------------
    ValueObject  *      m_parent;       // The parent value object, or NULL if this has no parent
    EvaluationPoint     m_update_point; // Stores both the stop id and the full context at which this value was last 
                                        // updated.  When we are asked to update the value object, we check whether
                                        // the context & stop id are the same before updating.
    ConstString         m_name;         // The name of this object
    DataExtractor       m_data;         // A data extractor that can be used to extract the value.
    Value               m_value;
    Error               m_error;        // An error object that can describe any errors that occur when updating values.
    std::string         m_value_str;    // Cached value string that will get cleared if/when the value is updated.
    std::string         m_old_value_str;// Cached old value string from the last time the value was gotten
    std::string         m_location_str; // Cached location string that will get cleared if/when the value is updated.
    std::string         m_summary_str;  // Cached summary string that will get cleared if/when the value is updated.
    std::string         m_object_desc_str; // Cached result of the "object printer".  This differs from the summary
                                              // in that the summary is consed up by us, the object_desc_string is builtin.

    ValueObjectManager *m_manager;      // This object is managed by the root object (any ValueObject that gets created
                                        // without a parent.)  The manager gets passed through all the generations of
                                        // dependent objects, and will keep the whole cluster of objects alive as long
                                        // as a shared pointer to any of them has been handed out.  Shared pointers to
                                        // value objects must always be made with the GetSP method.

    std::vector<ValueObject *>           m_children;
    std::map<ConstString, ValueObject *> m_synthetic_children;
    
    ValueObject*                         m_dynamic_value;
    ValueObject*                         m_synthetic_value;
    ValueObject*                         m_deref_valobj;
    
    lldb::ValueObjectSP m_addr_of_valobj_sp; // We have to hold onto a shared pointer to this one because it is created
                                             // as an independent ValueObjectConstResult, which isn't managed by us.

    lldb::Format                m_format;
    uint32_t                    m_last_format_mgr_revision;
    lldb::DynamicValueType      m_last_format_mgr_dynamic;
    lldb::SummaryFormatSP       m_last_summary_format;
    lldb::SummaryFormatSP       m_forced_summary_format;
    lldb::ValueFormatSP         m_last_value_format;
    lldb::SyntheticChildrenSP   m_last_synthetic_filter;
    ProcessModID                m_user_id_of_forced_summary;
    AddressType                 m_address_type_of_ptr_or_ref_children;
    
    bool                m_value_is_valid:1,
                        m_value_did_change:1,
                        m_children_count_valid:1,
                        m_old_value_valid:1,
                        m_is_deref_of_parent:1,
                        m_is_array_item_for_pointer:1,
                        m_is_bitfield_for_scalar:1,
                        m_is_expression_path_child:1,
                        m_is_child_at_offset:1,
                        m_trying_summary_already:1; // used to prevent endless recursion in printing summaries
    
    friend class ClangExpressionDeclMap;  // For GetValue
    friend class ClangExpressionVariable; // For SetName
    friend class Target;                  // For SetName
    friend class ValueObjectConstResultImpl;

    //------------------------------------------------------------------
    // Constructors and Destructors
    //------------------------------------------------------------------
    
    // Use the no-argument constructor to make a constant variable object (with no ExecutionContextScope.)
    
    ValueObject();
    
    // Use this constructor to create a "root variable object".  The ValueObject will be locked to this context
    // through-out its lifespan.
    
    ValueObject (ExecutionContextScope *exe_scope,
                 AddressType child_ptr_or_ref_addr_type = eAddressTypeLoad);
    
    // Use this constructor to create a ValueObject owned by another ValueObject.  It will inherit the ExecutionContext
    // of its parent.
    
    ValueObject (ValueObject &parent);

    ValueObjectManager *
    GetManager()
    {
        return m_manager;
    }
    
    virtual bool
    UpdateValue () = 0;

    virtual void
    CalculateDynamicValue (lldb::DynamicValueType use_dynamic);
    
    virtual void
    CalculateSyntheticValue (lldb::SyntheticValueType use_synthetic);
    
    // Should only be called by ValueObject::GetChildAtIndex()
    // Returns a ValueObject managed by this ValueObject's manager.
    virtual ValueObject *
    CreateChildAtIndex (uint32_t idx, bool synthetic_array_member, int32_t synthetic_index);

    // Should only be called by ValueObject::GetNumChildren()
    virtual uint32_t
    CalculateNumChildren() = 0;

    void
    SetNumChildren (uint32_t num_children);

    void
    SetValueDidChange (bool value_changed);

    void
    SetValueIsValid (bool valid);
    
    void
    ClearUserVisibleData();
    
    void
    AddSyntheticChild (const ConstString &key,
                       ValueObject *valobj);
    
    DataExtractor &
    GetDataExtractor ();
    
private:
    //------------------------------------------------------------------
    // For ValueObject only
    //------------------------------------------------------------------
    
    lldb::ValueObjectSP
    GetValueForExpressionPath_Impl(const char* expression_cstr,
                                   const char** first_unparsed,
                                   ExpressionPathScanEndReason* reason_to_stop,
                                   ExpressionPathEndResultType* final_value_type,
                                   const GetValueForExpressionPathOptions& options,
                                   ExpressionPathAftermath* final_task_on_target);
        
    // this method will ONLY expand [] expressions into a VOList and return
    // the number of elements it added to the VOList
    // it will NOT loop through expanding the follow-up of the expression_cstr
    // for all objects in the list
    int
    ExpandArraySliceExpression(const char* expression_cstr,
                               const char** first_unparsed,
                               lldb::ValueObjectSP root,
                               lldb::ValueObjectListSP& list,
                               ExpressionPathScanEndReason* reason_to_stop,
                               ExpressionPathEndResultType* final_value_type,
                               const GetValueForExpressionPathOptions& options,
                               ExpressionPathAftermath* final_task_on_target);
                               
    
    DISALLOW_COPY_AND_ASSIGN (ValueObject);

};

} // namespace lldb_private

#endif  // liblldb_ValueObject_h_
