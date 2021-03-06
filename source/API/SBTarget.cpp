//===-- SBTarget.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTarget.h"

#include "lldb/lldb-public.h"

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBSourceManager.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBSymbolContextList.h"
#include "lldb/Breakpoint/BreakpointID.h"
#include "lldb/Breakpoint/BreakpointIDList.h"
#include "lldb/Breakpoint/BreakpointList.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/AddressResolver.h"
#include "lldb/Core/AddressResolverName.h"
#include "lldb/Core/ArchSpec.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/RegularExpression.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Core/STLUtils.h"
#include "lldb/Core/ValueObjectList.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Host/FileSpec.h"
#include "lldb/Host/Host.h"
#include "lldb/Interpreter/Args.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/TargetList.h"

#include "lldb/Interpreter/CommandReturnObject.h"
#include "../source/Commands/CommandObjectBreakpoint.h"


using namespace lldb;
using namespace lldb_private;

#define DEFAULT_DISASM_BYTE_SIZE 32

//----------------------------------------------------------------------
// SBTarget constructor
//----------------------------------------------------------------------
SBTarget::SBTarget () :
    m_opaque_sp ()
{
}

SBTarget::SBTarget (const SBTarget& rhs) :
    m_opaque_sp (rhs.m_opaque_sp)
{
}

SBTarget::SBTarget(const TargetSP& target_sp) :
    m_opaque_sp (target_sp)
{
}

const SBTarget&
SBTarget::operator = (const SBTarget& rhs)
{
    if (this != &rhs)
        m_opaque_sp = rhs.m_opaque_sp;
    return *this;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
SBTarget::~SBTarget()
{
}

bool
SBTarget::IsValid () const
{
    return m_opaque_sp.get() != NULL;
}

SBProcess
SBTarget::GetProcess ()
{

    SBProcess sb_process;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        sb_process.SetProcess (m_opaque_sp->GetProcessSP());
    }

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        log->Printf ("SBTarget(%p)::GetProcess () => SBProcess(%p)", 
                     m_opaque_sp.get(), sb_process.get());
    }

    return sb_process;
}

SBDebugger
SBTarget::GetDebugger () const
{
    SBDebugger debugger;
    if (m_opaque_sp)
        debugger.reset (m_opaque_sp->GetDebugger().GetSP());
    return debugger;
}

SBProcess
SBTarget::LaunchSimple
(
    char const **argv,
    char const **envp,
    const char *working_directory
)
{
    char *stdin_path = NULL;
    char *stdout_path = NULL;
    char *stderr_path = NULL;
    uint32_t launch_flags = 0;
    bool stop_at_entry = false;
    SBError error;
    SBListener listener = GetDebugger().GetListener();
    return Launch (listener,
                   argv,
                   envp,
                   stdin_path,
                   stdout_path,
                   stderr_path,
                   working_directory,
                   launch_flags,
                   stop_at_entry,
                   error);
}

SBProcess
SBTarget::Launch 
(
    SBListener &listener, 
    char const **argv,
    char const **envp,
    const char *stdin_path,
    const char *stdout_path,
    const char *stderr_path,
    const char *working_directory,
    uint32_t launch_flags,   // See LaunchFlags
    bool stop_at_entry,
    lldb::SBError& error
)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
    {
        log->Printf ("SBTarget(%p)::Launch (argv=%p, envp=%p, stdin=%s, stdout=%s, stderr=%s, working-dir=%s, launch_flags=0x%x, stop_at_entry=%i, &error (%p))...",
                     m_opaque_sp.get(), 
                     argv, 
                     envp, 
                     stdin_path ? stdin_path : "NULL", 
                     stdout_path ? stdout_path : "NULL", 
                     stderr_path ? stderr_path : "NULL", 
                     working_directory ? working_directory : "NULL",
                     launch_flags, 
                     stop_at_entry, 
                     error.get());
    }
    SBProcess sb_process;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());

        if (getenv("LLDB_LAUNCH_FLAG_DISABLE_ASLR"))
            launch_flags |= eLaunchFlagDisableASLR;

        StateType state = eStateInvalid;
        sb_process.SetProcess (m_opaque_sp->GetProcessSP());
        if (sb_process.IsValid())
        {
            state = sb_process->GetState();
            
            if (sb_process->IsAlive() && state != eStateConnected)
            {       
                if (state == eStateAttaching)
                    error.SetErrorString ("process attach is in progress");
                else
                    error.SetErrorString ("a process is already being debugged");
                sb_process.Clear();
                return sb_process;
            }            
        }
        
        if (state == eStateConnected)
        {
            // If we are already connected, then we have already specified the
            // listener, so if a valid listener is supplied, we need to error out
            // to let the client know.
            if (listener.IsValid())
            {
                error.SetErrorString ("process is connected and already has a listener, pass empty listener");
                sb_process.Clear();
                return sb_process;
            }
        }
        else
        {
            if (listener.IsValid())
                sb_process.SetProcess (m_opaque_sp->CreateProcess (listener.ref()));
            else
                sb_process.SetProcess (m_opaque_sp->CreateProcess (m_opaque_sp->GetDebugger().GetListener()));
        }

        if (sb_process.IsValid())
        {
            if (getenv("LLDB_LAUNCH_FLAG_DISABLE_STDIO"))
                launch_flags |= eLaunchFlagDisableSTDIO;

            ProcessLaunchInfo launch_info (stdin_path, stdout_path, stderr_path, working_directory, launch_flags);
            
            Module *exe_module = m_opaque_sp->GetExecutableModulePointer();
            if (exe_module)
                launch_info.SetExecutableFile(exe_module->GetPlatformFileSpec(), true);
            if (argv)
                launch_info.GetArguments().AppendArguments (argv);
            if (envp)
                launch_info.GetEnvironmentEntries ().SetArguments (envp);

            error.SetError (sb_process->Launch (launch_info));
            if (error.Success())
            {
                // We we are stopping at the entry point, we can return now!
                if (stop_at_entry)
                    return sb_process;
                
                // Make sure we are stopped at the entry
                StateType state = sb_process->WaitForProcessToStop (NULL);
                if (state == eStateStopped)
                {
                    // resume the process to skip the entry point
                    error.SetError (sb_process->Resume());
                    if (error.Success())
                    {
                        // If we are doing synchronous mode, then wait for the
                        // process to stop yet again!
                        if (m_opaque_sp->GetDebugger().GetAsyncExecution () == false)
                            sb_process->WaitForProcessToStop (NULL);
                    }
                }
            }
        }
        else
        {
            error.SetErrorString ("unable to create lldb_private::Process");
        }
    }
    else
    {
        error.SetErrorString ("SBTarget is invalid");
    }

    log = lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API);
    if (log)
    {
        log->Printf ("SBTarget(%p)::Launch (...) => SBProceess(%p)", 
                     m_opaque_sp.get(), sb_process.get());
    }

    return sb_process;
}

#if defined(__APPLE__)

lldb::SBProcess
SBTarget::AttachToProcessWithID (SBListener &listener,
                                ::pid_t pid,
                                 lldb::SBError& error)
{
    return AttachToProcessWithID (listener, (lldb::pid_t)pid, error);
}

#endif // #if defined(__APPLE__)

lldb::SBProcess
SBTarget::AttachToProcessWithID 
(
    SBListener &listener, 
    lldb::pid_t pid,// The process ID to attach to
    SBError& error  // An error explaining what went wrong if attach fails
)
{
    SBProcess sb_process;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());

        StateType state = eStateInvalid;
        sb_process.SetProcess (m_opaque_sp->GetProcessSP());
        if (sb_process.IsValid())
        {
            state = sb_process->GetState();
            
            if (sb_process->IsAlive() && state != eStateConnected)
            {       
                if (state == eStateAttaching)
                    error.SetErrorString ("process attach is in progress");
                else
                    error.SetErrorString ("a process is already being debugged");
                sb_process.Clear();
                return sb_process;
            }            
        }

        if (state == eStateConnected)
        {
            // If we are already connected, then we have already specified the
            // listener, so if a valid listener is supplied, we need to error out
            // to let the client know.
            if (listener.IsValid())
            {
                error.SetErrorString ("process is connected and already has a listener, pass empty listener");
                sb_process.Clear();
                return sb_process;
            }
        }
        else
        {
            if (listener.IsValid())
                sb_process.SetProcess (m_opaque_sp->CreateProcess (listener.ref()));
            else
                sb_process.SetProcess (m_opaque_sp->CreateProcess (m_opaque_sp->GetDebugger().GetListener()));
        }

        if (sb_process.IsValid())
        {
            ProcessAttachInfo attach_info;
            attach_info.SetProcessID (pid);
            error.SetError (sb_process->Attach (attach_info));            
            // If we are doing synchronous mode, then wait for the
            // process to stop!
            if (m_opaque_sp->GetDebugger().GetAsyncExecution () == false)
                sb_process->WaitForProcessToStop (NULL);
        }
        else
        {
            error.SetErrorString ("unable to create lldb_private::Process");
        }
    }
    else
    {
        error.SetErrorString ("SBTarget is invalid");
    }
    return sb_process;

}

lldb::SBProcess
SBTarget::AttachToProcessWithName 
(
    SBListener &listener, 
    const char *name,   // basename of process to attach to
    bool wait_for,      // if true wait for a new instance of "name" to be launched
    SBError& error      // An error explaining what went wrong if attach fails
)
{
    SBProcess sb_process;
    if (name && m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());

        StateType state = eStateInvalid;
        sb_process.SetProcess (m_opaque_sp->GetProcessSP());
        if (sb_process.IsValid())
        {
            state = sb_process->GetState();
            
            if (sb_process->IsAlive() && state != eStateConnected)
            {       
                if (state == eStateAttaching)
                    error.SetErrorString ("process attach is in progress");
                else
                    error.SetErrorString ("a process is already being debugged");
                sb_process.Clear();
                return sb_process;
            }            
        }
        
        if (state == eStateConnected)
        {
            // If we are already connected, then we have already specified the
            // listener, so if a valid listener is supplied, we need to error out
            // to let the client know.
            if (listener.IsValid())
            {
                error.SetErrorString ("process is connected and already has a listener, pass empty listener");
                sb_process.Clear();
                return sb_process;
            }
        }
        else
        {
            if (listener.IsValid())
                sb_process.SetProcess (m_opaque_sp->CreateProcess (listener.ref()));
            else
                sb_process.SetProcess (m_opaque_sp->CreateProcess (m_opaque_sp->GetDebugger().GetListener()));
        }

        if (sb_process.IsValid())
        {
            ProcessAttachInfo attach_info;
            attach_info.GetExecutableFile().SetFile(name, false);
            attach_info.SetWaitForLaunch(wait_for);
            error.SetError (sb_process->Attach (attach_info));
            // If we are doing synchronous mode, then wait for the
            // process to stop!
            if (m_opaque_sp->GetDebugger().GetAsyncExecution () == false)
                sb_process->WaitForProcessToStop (NULL);
        }
        else
        {
            error.SetErrorString ("unable to create lldb_private::Process");
        }
    }
    else
    {
        error.SetErrorString ("SBTarget is invalid");
    }
    return sb_process;

}

lldb::SBProcess
SBTarget::ConnectRemote
(
    SBListener &listener,
    const char *url,
    const char *plugin_name,
    SBError& error
)
{
    SBProcess sb_process;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        if (listener.IsValid())
            sb_process.SetProcess (m_opaque_sp->CreateProcess (listener.ref(), plugin_name));
        else
            sb_process.SetProcess (m_opaque_sp->CreateProcess (m_opaque_sp->GetDebugger().GetListener(), plugin_name));
        
        
        if (sb_process.IsValid())
        {
            error.SetError (sb_process->ConnectRemote (url));
        }
        else
        {
            error.SetErrorString ("unable to create lldb_private::Process");
        }
    }
    else
    {
        error.SetErrorString ("SBTarget is invalid");
    }
    return sb_process;
}

SBFileSpec
SBTarget::GetExecutable ()
{

    SBFileSpec exe_file_spec;
    if (m_opaque_sp)
    {
        Module *exe_module = m_opaque_sp->GetExecutableModulePointer();
        if (exe_module)
            exe_file_spec.SetFileSpec (exe_module->GetFileSpec());
    }

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        log->Printf ("SBTarget(%p)::GetExecutable () => SBFileSpec(%p)", 
                     m_opaque_sp.get(), exe_file_spec.get());
    }

    return exe_file_spec;
}

bool
SBTarget::operator == (const SBTarget &rhs) const
{
    return m_opaque_sp.get() == rhs.m_opaque_sp.get();
}

bool
SBTarget::operator != (const SBTarget &rhs) const
{
    return m_opaque_sp.get() != rhs.m_opaque_sp.get();
}

lldb_private::Target *
SBTarget::operator ->() const
{
    return m_opaque_sp.get();
}

lldb_private::Target *
SBTarget::get() const
{
    return m_opaque_sp.get();
}

const lldb::TargetSP &
SBTarget::get_sp () const
{
    return m_opaque_sp;
}

void
SBTarget::reset (const lldb::TargetSP& target_sp)
{
    m_opaque_sp = target_sp;
}

lldb::SBAddress
SBTarget::ResolveLoadAddress (lldb::addr_t vm_addr)
{
    lldb::SBAddress sb_addr;
    Address &addr = sb_addr.ref();
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        if (m_opaque_sp->GetSectionLoadList().ResolveLoadAddress (vm_addr, addr))
            return sb_addr;
    }

    // We have a load address that isn't in a section, just return an address
    // with the offset filled in (the address) and the section set to NULL
    addr.SetSection(NULL);
    addr.SetOffset(vm_addr);
    return sb_addr;
}

SBSymbolContext
SBTarget::ResolveSymbolContextForAddress (const SBAddress& addr, uint32_t resolve_scope)
{
    SBSymbolContext sc;
    if (m_opaque_sp && addr.IsValid())
        m_opaque_sp->GetImages().ResolveSymbolContextForAddress (addr.ref(), resolve_scope, sc.ref());
    return sc;
}


SBBreakpoint
SBTarget::BreakpointCreateByLocation (const char *file, uint32_t line)
{
    return SBBreakpoint(BreakpointCreateByLocation (SBFileSpec (file, false), line));
}

SBBreakpoint
SBTarget::BreakpointCreateByLocation (const SBFileSpec &sb_file_spec, uint32_t line)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get() && line != 0)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        *sb_bp = m_opaque_sp->CreateBreakpoint (NULL, *sb_file_spec, line, true, false);
    }

    if (log)
    {
        SBStream sstr;
        sb_bp.GetDescription (sstr);
        char path[PATH_MAX];
        sb_file_spec->GetPath (path, sizeof(path));
        log->Printf ("SBTarget(%p)::BreakpointCreateByLocation ( %s:%u ) => SBBreakpoint(%p): %s", 
                     m_opaque_sp.get(), 
                     path,
                     line, 
                     sb_bp.get(),
                     sstr.GetData());
    }

    return sb_bp;
}

SBBreakpoint
SBTarget::BreakpointCreateByName (const char *symbol_name, const char *module_name)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get())
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        if (module_name && module_name[0])
        {
            FileSpecList module_spec_list;
            module_spec_list.Append (FileSpec (module_name, false));
            *sb_bp = m_opaque_sp->CreateBreakpoint (&module_spec_list, NULL, symbol_name, eFunctionNameTypeAuto, false);
        }
        else
        {
            *sb_bp = m_opaque_sp->CreateBreakpoint (NULL, NULL, symbol_name, eFunctionNameTypeAuto, false);
        }
    }
    
    if (log)
    {
        log->Printf ("SBTarget(%p)::BreakpointCreateByName (symbol=\"%s\", module=\"%s\") => SBBreakpoint(%p)", 
                     m_opaque_sp.get(), symbol_name, module_name, sb_bp.get());
    }

    return sb_bp;
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateByName (const char *symbol_name, 
                            const SBFileSpecList &module_list, 
                            const SBFileSpecList &comp_unit_list)
{
    uint32_t name_type_mask = eFunctionNameTypeAuto;
    return BreakpointCreateByName (symbol_name, name_type_mask, module_list, comp_unit_list);
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateByName (const char *symbol_name,
                            uint32_t name_type_mask,
                            const SBFileSpecList &module_list, 
                            const SBFileSpecList &comp_unit_list)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get() && symbol_name && symbol_name[0])
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        *sb_bp = m_opaque_sp->CreateBreakpoint (module_list.get(), 
                                                comp_unit_list.get(), 
                                                symbol_name, 
                                                name_type_mask, 
                                                false);
    }
    
    if (log)
    {
        log->Printf ("SBTarget(%p)::BreakpointCreateByName (symbol=\"%s\", name_type: %d) => SBBreakpoint(%p)", 
                     m_opaque_sp.get(), symbol_name, name_type_mask, sb_bp.get());
    }

    return sb_bp;
}


SBBreakpoint
SBTarget::BreakpointCreateByRegex (const char *symbol_name_regex, const char *module_name)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get() && symbol_name_regex && symbol_name_regex[0])
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        RegularExpression regexp(symbol_name_regex);
        
        if (module_name && module_name[0])
        {
            FileSpecList module_spec_list;
            module_spec_list.Append (FileSpec (module_name, false));
            
            *sb_bp = m_opaque_sp->CreateFuncRegexBreakpoint (&module_spec_list, NULL, regexp, false);
        }
        else
        {
            *sb_bp = m_opaque_sp->CreateFuncRegexBreakpoint (NULL, NULL, regexp, false);
        }
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::BreakpointCreateByRegex (symbol_regex=\"%s\", module_name=\"%s\") => SBBreakpoint(%p)", 
                     m_opaque_sp.get(), symbol_name_regex, module_name, sb_bp.get());
    }

    return sb_bp;
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateByRegex (const char *symbol_name_regex, 
                         const SBFileSpecList &module_list, 
                         const SBFileSpecList &comp_unit_list)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get() && symbol_name_regex && symbol_name_regex[0])
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        RegularExpression regexp(symbol_name_regex);
        
        *sb_bp = m_opaque_sp->CreateFuncRegexBreakpoint (module_list.get(), comp_unit_list.get(), regexp, false);
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::BreakpointCreateByRegex (symbol_regex=\"%s\") => SBBreakpoint(%p)", 
                     m_opaque_sp.get(), symbol_name_regex, sb_bp.get());
    }

    return sb_bp;
}

SBBreakpoint
SBTarget::BreakpointCreateByAddress (addr_t address)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get())
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        *sb_bp = m_opaque_sp->CreateBreakpoint (address, false);
    }
    
    if (log)
    {
        log->Printf ("SBTarget(%p)::BreakpointCreateByAddress (address=%llu) => SBBreakpoint(%p)", m_opaque_sp.get(), (uint64_t) address, sb_bp.get());
    }

    return sb_bp;
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateBySourceRegex (const char *source_regex, const lldb::SBFileSpec &source_file, const char *module_name)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get() && source_regex && source_regex[0])
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        RegularExpression regexp(source_regex);
        FileSpecList source_file_spec_list;
        source_file_spec_list.Append (source_file.ref());
        
        if (module_name && module_name[0])
        {
            FileSpecList module_spec_list;
            module_spec_list.Append (FileSpec (module_name, false));
            
            *sb_bp = m_opaque_sp->CreateSourceRegexBreakpoint (&module_spec_list, &source_file_spec_list, regexp, false);
        }
        else
        {
            *sb_bp = m_opaque_sp->CreateSourceRegexBreakpoint (NULL, &source_file_spec_list, regexp, false);
        }
    }

    if (log)
    {
        char path[PATH_MAX];
        source_file->GetPath (path, sizeof(path));
        log->Printf ("SBTarget(%p)::BreakpointCreateByRegex (source_regex=\"%s\", file=\"%s\", module_name=\"%s\") => SBBreakpoint(%p)", 
                     m_opaque_sp.get(), source_regex, path, module_name, sb_bp.get());
    }

    return sb_bp;
}

lldb::SBBreakpoint
SBTarget::BreakpointCreateBySourceRegex (const char *source_regex, 
                               const SBFileSpecList &module_list, 
                               const lldb::SBFileSpecList &source_file_list)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get() && source_regex && source_regex[0])
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        RegularExpression regexp(source_regex);
        *sb_bp = m_opaque_sp->CreateSourceRegexBreakpoint (module_list.get(), source_file_list.get(), regexp, false);
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::BreakpointCreateByRegex (source_regex=\"%s\") => SBBreakpoint(%p)", 
                     m_opaque_sp.get(), source_regex, sb_bp.get());
    }

    return sb_bp;
}

uint32_t
SBTarget::GetNumBreakpoints () const
{
    if (m_opaque_sp)
    {
        // The breakpoint list is thread safe, no need to lock
        return m_opaque_sp->GetBreakpointList().GetSize();
    }
    return 0;
}

SBBreakpoint
SBTarget::GetBreakpointAtIndex (uint32_t idx) const
{
    SBBreakpoint sb_breakpoint;
    if (m_opaque_sp)
    {
        // The breakpoint list is thread safe, no need to lock
        *sb_breakpoint = m_opaque_sp->GetBreakpointList().GetBreakpointAtIndex(idx);
    }
    return sb_breakpoint;
}

bool
SBTarget::BreakpointDelete (break_id_t bp_id)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    bool result = false;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        result = m_opaque_sp->RemoveBreakpointByID (bp_id);
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::BreakpointDelete (bp_id=%d) => %i", m_opaque_sp.get(), (uint32_t) bp_id, result);
    }

    return result;
}

SBBreakpoint
SBTarget::FindBreakpointByID (break_id_t bp_id)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_breakpoint;
    if (m_opaque_sp && bp_id != LLDB_INVALID_BREAK_ID)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        *sb_breakpoint = m_opaque_sp->GetBreakpointByID (bp_id);
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::FindBreakpointByID (bp_id=%d) => SBBreakpoint(%p)", 
                     m_opaque_sp.get(), (uint32_t) bp_id, sb_breakpoint.get());
    }

    return sb_breakpoint;
}

bool
SBTarget::EnableAllBreakpoints ()
{
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        m_opaque_sp->EnableAllBreakpoints ();
        return true;
    }
    return false;
}

bool
SBTarget::DisableAllBreakpoints ()
{
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        m_opaque_sp->DisableAllBreakpoints ();
        return true;
    }
    return false;
}

bool
SBTarget::DeleteAllBreakpoints ()
{
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        m_opaque_sp->RemoveAllBreakpoints ();
        return true;
    }
    return false;
}

uint32_t
SBTarget::GetNumWatchpoints () const
{
    if (m_opaque_sp)
    {
        // The watchpoint list is thread safe, no need to lock
        return m_opaque_sp->GetWatchpointList().GetSize();
    }
    return 0;
}

SBWatchpoint
SBTarget::GetWatchpointAtIndex (uint32_t idx) const
{
    SBWatchpoint sb_watchpoint;
    if (m_opaque_sp)
    {
        // The watchpoint list is thread safe, no need to lock
        *sb_watchpoint = m_opaque_sp->GetWatchpointList().GetByIndex(idx);
    }
    return sb_watchpoint;
}

bool
SBTarget::DeleteWatchpoint (watch_id_t wp_id)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    bool result = false;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        result = m_opaque_sp->RemoveWatchpointByID (wp_id);
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::WatchpointDelete (wp_id=%d) => %i", m_opaque_sp.get(), (uint32_t) wp_id, result);
    }

    return result;
}

SBWatchpoint
SBTarget::FindWatchpointByID (lldb::watch_id_t wp_id)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBWatchpoint sb_watchpoint;
    if (m_opaque_sp && wp_id != LLDB_INVALID_WATCH_ID)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        *sb_watchpoint = m_opaque_sp->GetWatchpointList().FindByID(wp_id);
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::FindWatchpointByID (bp_id=%d) => SBWatchpoint(%p)", 
                     m_opaque_sp.get(), (uint32_t) wp_id, sb_watchpoint.get());
    }

    return sb_watchpoint;
}

lldb::SBWatchpoint
SBTarget::WatchAddress (lldb::addr_t addr, size_t size, bool read, bool write)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    
    SBWatchpoint sb_watchpoint;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        uint32_t watch_type = (read ? LLDB_WATCH_TYPE_READ : 0) |
            (write ? LLDB_WATCH_TYPE_WRITE : 0);
        sb_watchpoint = m_opaque_sp->CreateWatchpoint(addr, size, watch_type);
    }
    
    if (log)
    {
        log->Printf ("SBTarget(%p)::WatchAddress (addr=0x%llx, 0x%u) => SBWatchpoint(%p)", 
                     m_opaque_sp.get(), addr, (uint32_t) size, sb_watchpoint.get());
    }
    
    return sb_watchpoint;
}

bool
SBTarget::EnableAllWatchpoints ()
{
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        m_opaque_sp->EnableAllWatchpoints ();
        return true;
    }
    return false;
}

bool
SBTarget::DisableAllWatchpoints ()
{
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        m_opaque_sp->DisableAllWatchpoints ();
        return true;
    }
    return false;
}

bool
SBTarget::DeleteAllWatchpoints ()
{
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        m_opaque_sp->RemoveAllWatchpoints ();
        return true;
    }
    return false;
}


lldb::SBModule
SBTarget::AddModule (const char *path,
                     const char *triple,
                     const char *uuid_cstr)
{
    lldb::SBModule sb_module;
    if (m_opaque_sp)
    {
        FileSpec module_file_spec;
        UUID module_uuid;
        ArchSpec module_arch;

        if (path)
            module_file_spec.SetFile(path, false);

        if (uuid_cstr)
            module_uuid.SetfromCString(uuid_cstr);

        if (triple)
            module_arch.SetTriple (triple, m_opaque_sp->GetPlatform ().get());

        sb_module.SetModule(m_opaque_sp->GetSharedModule (module_file_spec,
                                                          module_arch,
                                                          uuid_cstr ? &module_uuid : NULL));
    }
    return sb_module;
}

bool
SBTarget::AddModule (lldb::SBModule &module)
{
    if (m_opaque_sp)
    {
        m_opaque_sp->GetImages().AppendIfNeeded (module.get_sp());
        return true;
    }
    return false;
}

uint32_t
SBTarget::GetNumModules () const
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    uint32_t num = 0;
    if (m_opaque_sp)
    {
        // The module list is thread safe, no need to lock
        num = m_opaque_sp->GetImages().GetSize();
    }

    if (log)
        log->Printf ("SBTarget(%p)::GetNumModules () => %d", m_opaque_sp.get(), num);

    return num;
}

void
SBTarget::Clear ()
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBTarget(%p)::Clear ()", m_opaque_sp.get());

    m_opaque_sp.reset();
}


SBModule
SBTarget::FindModule (const SBFileSpec &sb_file_spec)
{
    SBModule sb_module;
    if (m_opaque_sp && sb_file_spec.IsValid())
    {
        // The module list is thread safe, no need to lock
        sb_module.SetModule (m_opaque_sp->GetImages().FindFirstModuleForFileSpec (*sb_file_spec, NULL, NULL));
    }
    return sb_module;
}

SBModule
SBTarget::GetModuleAtIndex (uint32_t idx)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBModule sb_module;
    if (m_opaque_sp)
    {
        // The module list is thread safe, no need to lock
        sb_module.SetModule(m_opaque_sp->GetImages().GetModuleAtIndex(idx));
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::GetModuleAtIndex (idx=%d) => SBModule(%p)", 
                     m_opaque_sp.get(), idx, sb_module.get());
    }

    return sb_module;
}

bool
SBTarget::RemoveModule (lldb::SBModule module)
{
    if (m_opaque_sp)
        return m_opaque_sp->GetImages().Remove(module.get_sp());
    return false;
}


SBBroadcaster
SBTarget::GetBroadcaster () const
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBroadcaster broadcaster(m_opaque_sp.get(), false);
    
    if (log)
        log->Printf ("SBTarget(%p)::GetBroadcaster () => SBBroadcaster(%p)", 
                     m_opaque_sp.get(), broadcaster.get());

    return broadcaster;
}


bool
SBTarget::GetDescription (SBStream &description, lldb::DescriptionLevel description_level)
{
    Stream &strm = description.ref();

    if (m_opaque_sp)
    {
        m_opaque_sp->Dump (&strm, description_level);
    }
    else
        strm.PutCString ("No value");
    
    return true;
}

uint32_t
SBTarget::FindFunctions (const char *name, 
                         uint32_t name_type_mask, 
                         bool append, 
                         lldb::SBSymbolContextList& sc_list)
{
    if (!append)
        sc_list.Clear();
    if (name && m_opaque_sp)
    {
        const bool symbols_ok = true;
        return m_opaque_sp->GetImages().FindFunctions (ConstString(name), 
                                                       name_type_mask, 
                                                       symbols_ok, 
                                                       append, 
                                                       *sc_list);
    }
    return 0;
}

lldb::SBType
SBTarget::FindFirstType (const char* type)
{
    if (type && m_opaque_sp)
    {
        size_t count = m_opaque_sp->GetImages().GetSize();
        for (size_t idx = 0; idx < count; idx++)
        {
            SBType found_at_idx = GetModuleAtIndex(idx).FindFirstType(type);
            
            if (found_at_idx.IsValid())
                return found_at_idx;
        }
    }
    return SBType();
}

lldb::SBTypeList
SBTarget::FindTypes (const char* type)
{
    
    SBTypeList retval;
    
    if (type && m_opaque_sp)
    {
        ModuleList& images = m_opaque_sp->GetImages();
        ConstString name_const(type);
        SymbolContext sc;
        TypeList type_list;
        
        uint32_t num_matches = images.FindTypes(sc,
                                                name_const,
                                                true,
                                                UINT32_MAX,
                                                type_list);
        
        for (size_t idx = 0; idx < num_matches; idx++)
        {
            TypeSP type_sp (type_list.GetTypeAtIndex(idx));
            if (type_sp)
                retval.Append(SBType(type_sp));
        }
    }
    return retval;
}

SBValueList
SBTarget::FindGlobalVariables (const char *name, uint32_t max_matches)
{
    SBValueList sb_value_list;
    
    if (name && m_opaque_sp)
    {
        VariableList variable_list;
        const bool append = true;
        const uint32_t match_count = m_opaque_sp->GetImages().FindGlobalVariables (ConstString (name), 
                                                                                   append, 
                                                                                   max_matches,
                                                                                   variable_list);
        
        if (match_count > 0)
        {
            ExecutionContextScope *exe_scope = m_opaque_sp->GetProcessSP().get();
            if (exe_scope == NULL)
                exe_scope = m_opaque_sp.get();
            ValueObjectList &value_object_list = sb_value_list.ref();
            for (uint32_t i=0; i<match_count; ++i)
            {
                lldb::ValueObjectSP valobj_sp (ValueObjectVariable::Create (exe_scope, variable_list.GetVariableAtIndex(i)));
                if (valobj_sp)
                    value_object_list.Append(valobj_sp);
            }
        }
    }

    return sb_value_list;
}

SBSourceManager
SBTarget::GetSourceManager()
{
    SBSourceManager source_manager (*this);
    return source_manager;
}

lldb::SBInstructionList
SBTarget::GetInstructions (lldb::SBAddress base_addr, const void *buf, size_t size)
{
    SBInstructionList sb_instructions;
    
    if (m_opaque_sp)
    {
        Address addr;
        
        if (base_addr.get())
            addr = *base_addr.get();
        
        sb_instructions.SetDisassembler (Disassembler::DisassembleBytes (m_opaque_sp->GetArchitecture(),
                                                                         NULL,
                                                                         addr,
                                                                         buf,
                                                                         size));
    }

    return sb_instructions;
}

lldb::SBInstructionList
SBTarget::GetInstructions (lldb::addr_t base_addr, const void *buf, size_t size)
{
    return GetInstructions (ResolveLoadAddress(base_addr), buf, size);
}

SBError
SBTarget::SetSectionLoadAddress (lldb::SBSection section,
                                 lldb::addr_t section_base_addr)
{
    SBError sb_error;
    
    if (IsValid())
    {
        if (!section.IsValid())
        {
            sb_error.SetErrorStringWithFormat ("invalid section");
        }
        else
        {
            m_opaque_sp->GetSectionLoadList().SetSectionLoadAddress (section.GetSection(), section_base_addr);
        }
    }
    else
    {
        sb_error.SetErrorStringWithFormat ("invalid target");
    }
    return sb_error;
}

SBError
SBTarget::ClearSectionLoadAddress (lldb::SBSection section)
{
    SBError sb_error;
    
    if (IsValid())
    {
        if (!section.IsValid())
        {
            sb_error.SetErrorStringWithFormat ("invalid section");
        }
        else
        {
            m_opaque_sp->GetSectionLoadList().SetSectionUnloaded (section.GetSection());
        }
    }
    else
    {
        sb_error.SetErrorStringWithFormat ("invalid target");
    }
    return sb_error;
}

SBError
SBTarget::SetModuleLoadAddress (lldb::SBModule module, int64_t slide_offset)
{
    SBError sb_error;
    
    char path[PATH_MAX];
    if (IsValid())
    {
        if (!module.IsValid())
        {
            sb_error.SetErrorStringWithFormat ("invalid module");
        }
        else
        {
            ObjectFile *objfile = module->GetObjectFile();
            if (objfile)
            {
                SectionList *section_list = objfile->GetSectionList();
                if (section_list)
                {
                    const size_t num_sections = section_list->GetSize();
                    for (size_t sect_idx = 0; sect_idx < num_sections; ++sect_idx)
                    {
                        SectionSP section_sp (section_list->GetSectionAtIndex(sect_idx));
                        if (section_sp)
                            m_opaque_sp->GetSectionLoadList().SetSectionLoadAddress (section_sp.get(), section_sp->GetFileAddress() + slide_offset);
                    }
                }
                else
                {
                    module->GetFileSpec().GetPath (path, sizeof(path));
                    sb_error.SetErrorStringWithFormat ("no sections in object file '%s'", path);
                }
            }
            else
            {
                module->GetFileSpec().GetPath (path, sizeof(path));
                sb_error.SetErrorStringWithFormat ("no object file for module '%s'", path);
            }
        }
    }
    else
    {
        sb_error.SetErrorStringWithFormat ("invalid target");
    }
    return sb_error;
}

SBError
SBTarget::ClearModuleLoadAddress (lldb::SBModule module)
{
    SBError sb_error;
    
    char path[PATH_MAX];
    if (IsValid())
    {
        if (!module.IsValid())
        {
            sb_error.SetErrorStringWithFormat ("invalid module");
        }
        else
        {
            ObjectFile *objfile = module->GetObjectFile();
            if (objfile)
            {
                SectionList *section_list = objfile->GetSectionList();
                if (section_list)
                {
                    const size_t num_sections = section_list->GetSize();
                    for (size_t sect_idx = 0; sect_idx < num_sections; ++sect_idx)
                    {
                        SectionSP section_sp (section_list->GetSectionAtIndex(sect_idx));
                        if (section_sp)
                            m_opaque_sp->GetSectionLoadList().SetSectionUnloaded (section_sp.get());
                    }
                }
                else
                {
                    module->GetFileSpec().GetPath (path, sizeof(path));
                    sb_error.SetErrorStringWithFormat ("no sections in object file '%s'", path);
                }
            }
            else
            {
                module->GetFileSpec().GetPath (path, sizeof(path));
                sb_error.SetErrorStringWithFormat ("no object file for module '%s'", path);
            }
        }
    }
    else
    {
        sb_error.SetErrorStringWithFormat ("invalid target");
    }
    return sb_error;
}


