//===-- PlatformDarwin.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PlatformDarwin.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Error.h"
#include "lldb/Host/Host.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;
    

//------------------------------------------------------------------
/// Default Constructor
//------------------------------------------------------------------
PlatformDarwin::PlatformDarwin (bool is_host) :
    Platform(is_host),  // This is the local host platform
    m_remote_platform_sp ()
{
}

//------------------------------------------------------------------
/// Destructor.
///
/// The destructor is virtual since this class is designed to be
/// inherited from by the plug-in instance.
//------------------------------------------------------------------
PlatformDarwin::~PlatformDarwin()
{
}


Error
PlatformDarwin::ResolveExecutable (const FileSpec &exe_file,
                                   const ArchSpec &exe_arch,
                                   lldb::ModuleSP &exe_module_sp)
{
    Error error;
    // Nothing special to do here, just use the actual file and architecture

    char exe_path[PATH_MAX];
    FileSpec resolved_exe_file (exe_file);
    
    if (IsHost())
    {
        // If we have "ls" as the exe_file, resolve the executable loation based on
        // the current path variables
        if (!resolved_exe_file.Exists())
        {
            exe_file.GetPath (exe_path, sizeof(exe_path));
            resolved_exe_file.SetFile(exe_path, true);
        }

        if (!resolved_exe_file.Exists())
            resolved_exe_file.ResolveExecutableLocation ();

        // Resolve any executable within a bundle on MacOSX
        Host::ResolveExecutableInBundle (resolved_exe_file);
        
        if (resolved_exe_file.Exists())
            error.Clear();
        else
        {
            exe_file.GetPath (exe_path, sizeof(exe_path));
            error.SetErrorStringWithFormat ("unable to find executable for '%s'", exe_path);
        }
    }
    else
    {
        if (m_remote_platform_sp)
        {
            error = m_remote_platform_sp->ResolveExecutable (exe_file, 
                                                             exe_arch,
                                                             exe_module_sp);
        }
        else
        {
            // We may connect to a process and use the provided executable (Don't use local $PATH).

            // Resolve any executable within a bundle on MacOSX
            Host::ResolveExecutableInBundle (resolved_exe_file);

            if (resolved_exe_file.Exists())
                error.Clear();
            else
                error.SetErrorStringWithFormat("the platform is not currently connected, and '%s' doesn't exist in the system root.", resolved_exe_file.GetFilename().AsCString(""));
        }
    }
    

    if (error.Success())
    {
        if (exe_arch.IsValid())
        {
            error = ModuleList::GetSharedModule (resolved_exe_file, 
                                                 exe_arch, 
                                                 NULL,
                                                 NULL, 
                                                 0, 
                                                 exe_module_sp, 
                                                 NULL, 
                                                 NULL);
        
            if (exe_module_sp->GetObjectFile() == NULL)
            {
                exe_module_sp.reset();
                error.SetErrorStringWithFormat ("'%s%s%s' doesn't contain the architecture %s",
                                                exe_file.GetDirectory().AsCString(""),
                                                exe_file.GetDirectory() ? "/" : "",
                                                exe_file.GetFilename().AsCString(""),
                                                exe_arch.GetArchitectureName());
            }
        }
        else
        {
            // No valid architecture was specified, ask the platform for
            // the architectures that we should be using (in the correct order)
            // and see if we can find a match that way
            StreamString arch_names;
            ArchSpec platform_arch;
            for (uint32_t idx = 0; GetSupportedArchitectureAtIndex (idx, platform_arch); ++idx)
            {
                error = ModuleList::GetSharedModule (resolved_exe_file, 
                                                     platform_arch, 
                                                     NULL,
                                                     NULL, 
                                                     0, 
                                                     exe_module_sp, 
                                                     NULL, 
                                                     NULL);
                // Did we find an executable using one of the 
                if (error.Success())
                {
                    if (exe_module_sp && exe_module_sp->GetObjectFile())
                        break;
                    else
                        error.SetErrorToGenericError();
                }
                
                if (idx > 0)
                    arch_names.PutCString (", ");
                arch_names.PutCString (platform_arch.GetArchitectureName());
            }
            
            if (error.Fail() || !exe_module_sp)
            {
                error.SetErrorStringWithFormat ("'%s%s%s' doesn't contain any '%s' platform architectures: %s",
                                                exe_file.GetDirectory().AsCString(""),
                                                exe_file.GetDirectory() ? "/" : "",
                                                exe_file.GetFilename().AsCString(""),
                                                GetShortPluginName(),
                                                arch_names.GetString().c_str());
            }
        }
    }

    return error;
}


size_t
PlatformDarwin::GetSoftwareBreakpointTrapOpcode (Target &target, BreakpointSite *bp_site)
{
    const uint8_t *trap_opcode = NULL;
    uint32_t trap_opcode_size = 0;
    bool bp_is_thumb = false;
        
    llvm::Triple::ArchType machine = target.GetArchitecture().GetMachine();
    switch (machine)
    {
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
        {
            static const uint8_t g_i386_breakpoint_opcode[] = { 0xCC };
            trap_opcode = g_i386_breakpoint_opcode;
            trap_opcode_size = sizeof(g_i386_breakpoint_opcode);
        }
        break;

    case llvm::Triple::thumb:
        bp_is_thumb = true; // Fall through...
    case llvm::Triple::arm:
        {
            static const uint8_t g_arm_breakpoint_opcode[] = { 0xFE, 0xDE, 0xFF, 0xE7 };
            static const uint8_t g_thumb_breakpooint_opcode[] = { 0xFE, 0xDE };

            // Auto detect arm/thumb if it wasn't explicitly specified
            if (!bp_is_thumb)
            {
                lldb::BreakpointLocationSP bp_loc_sp (bp_site->GetOwnerAtIndex (0));
                if (bp_loc_sp)
                    bp_is_thumb = bp_loc_sp->GetAddress().GetAddressClass () == eAddressClassCodeAlternateISA;
            }
            if (bp_is_thumb)
            {
                trap_opcode = g_thumb_breakpooint_opcode;
                trap_opcode_size = sizeof(g_thumb_breakpooint_opcode);
                break;
            }
            trap_opcode = g_arm_breakpoint_opcode;
            trap_opcode_size = sizeof(g_arm_breakpoint_opcode);
        }
        break;
        
    case llvm::Triple::ppc:
    case llvm::Triple::ppc64:
        {
            static const uint8_t g_ppc_breakpoint_opcode[] = { 0x7F, 0xC0, 0x00, 0x08 };
            trap_opcode = g_ppc_breakpoint_opcode;
            trap_opcode_size = sizeof(g_ppc_breakpoint_opcode);
        }
        break;
        
    default:
        assert(!"Unhandled architecture in PlatformDarwin::GetSoftwareBreakpointTrapOpcode()");
        break;
    }
    
    if (trap_opcode && trap_opcode_size)
    {
        if (bp_site->SetTrapOpcode(trap_opcode, trap_opcode_size))
            return trap_opcode_size;
    }
    return 0;

}

bool
PlatformDarwin::GetRemoteOSVersion ()
{
    if (m_remote_platform_sp)
        return m_remote_platform_sp->GetOSVersion (m_major_os_version, 
                                                   m_minor_os_version, 
                                                   m_update_os_version);
    return false;
}

bool
PlatformDarwin::GetRemoteOSBuildString (std::string &s)
{
    if (m_remote_platform_sp)
        return m_remote_platform_sp->GetRemoteOSBuildString (s);
    s.clear();
    return false;
}

bool
PlatformDarwin::GetRemoteOSKernelDescription (std::string &s)
{
    if (m_remote_platform_sp)
        return m_remote_platform_sp->GetRemoteOSKernelDescription (s);
    s.clear();
    return false;
}

// Remote Platform subclasses need to override this function
ArchSpec
PlatformDarwin::GetRemoteSystemArchitecture ()
{
    if (m_remote_platform_sp)
        return m_remote_platform_sp->GetRemoteSystemArchitecture ();
    return ArchSpec();
}


const char *
PlatformDarwin::GetHostname ()
{
    if (IsHost())
        return Platform::GetHostname();

    if (m_remote_platform_sp)
        return m_remote_platform_sp->GetHostname ();
    return NULL;
}

bool
PlatformDarwin::IsConnected () const
{
    if (IsHost())
        return true;
    else if (m_remote_platform_sp)
        return m_remote_platform_sp->IsConnected();
    return false;
}

Error
PlatformDarwin::ConnectRemote (Args& args)
{
    Error error;
    if (IsHost())
    {
        error.SetErrorStringWithFormat ("can't connect to the host platform '%s', always connected", GetShortPluginName());
    }
    else
    {
        if (!m_remote_platform_sp)
            m_remote_platform_sp = Platform::Create ("remote-gdb-server", error);

        if (m_remote_platform_sp)
        {
            if (error.Success())
            {
                if (m_remote_platform_sp)
                {
                    error = m_remote_platform_sp->ConnectRemote (args);
                }
                else
                {
                    error.SetErrorString ("\"platform connect\" takes a single argument: <connect-url>");
                }
            }
        }
        else
            error.SetErrorString ("failed to create a 'remote-gdb-server' platform");
        
        if (error.Fail())
            m_remote_platform_sp.reset();
    }

    return error;
}

Error
PlatformDarwin::DisconnectRemote ()
{
    Error error;
    
    if (IsHost())
    {
        error.SetErrorStringWithFormat ("can't disconnect from the host platform '%s', always connected", GetShortPluginName());
    }
    else
    {
        if (m_remote_platform_sp)
            error = m_remote_platform_sp->DisconnectRemote ();
        else
            error.SetErrorString ("the platform is not currently connected");
    }
    return error;
}


bool
PlatformDarwin::GetProcessInfo (lldb::pid_t pid, ProcessInstanceInfo &process_info)
{
    bool sucess = false;
    if (IsHost())
    {
        sucess = Platform::GetProcessInfo (pid, process_info);
    }
    else
    {
        if (m_remote_platform_sp)
            sucess = m_remote_platform_sp->GetProcessInfo (pid, process_info);
    }
    return sucess;
}



uint32_t
PlatformDarwin::FindProcesses (const ProcessInstanceInfoMatch &match_info,
                               ProcessInstanceInfoList &process_infos)
{
    uint32_t match_count = 0;
    if (IsHost())
    {
        // Let the base class figure out the host details
        match_count = Platform::FindProcesses (match_info, process_infos);
    }
    else
    {
        // If we are remote, we can only return results if we are connected
        if (m_remote_platform_sp)
            match_count = m_remote_platform_sp->FindProcesses (match_info, process_infos);
    }
    return match_count;    
}

Error
PlatformDarwin::LaunchProcess (ProcessLaunchInfo &launch_info)
{
    Error error;
    
    if (IsHost())
    {
        if (launch_info.GetFlags().Test (eLaunchFlagLaunchInShell))
        {
            const bool is_localhost = true;
            if (!launch_info.ConvertArgumentsForLaunchingInShell (error, is_localhost))
                return error;
        }
        error = Platform::LaunchProcess (launch_info);
    }
    else
    {
        if (m_remote_platform_sp)
            error = m_remote_platform_sp->LaunchProcess (launch_info);
        else
            error.SetErrorString ("the platform is not currently connected");
    }
    return error;
}

lldb::ProcessSP
PlatformDarwin::Attach (ProcessAttachInfo &attach_info,
                        Debugger &debugger,
                        Target *target,
                        Listener &listener, 
                        Error &error)
{
    lldb::ProcessSP process_sp;
    
    if (IsHost())
    {
        if (target == NULL)
        {
            TargetSP new_target_sp;
            FileSpec emptyFileSpec;
            
            error = debugger.GetTargetList().CreateTarget (debugger,
                                                           emptyFileSpec,
                                                           NULL, 
                                                           false,
                                                           NULL,
                                                           new_target_sp);
            target = new_target_sp.get();
        }
        else
            error.Clear();
    
        if (target && error.Success())
        {
            debugger.GetTargetList().SetSelectedTarget(target);

            process_sp = target->CreateProcess (listener, attach_info.GetProcessPluginName());
            
            if (process_sp)
                error = process_sp->Attach (attach_info);
        }
    }
    else
    {
        if (m_remote_platform_sp)
            process_sp = m_remote_platform_sp->Attach (attach_info, debugger, target, listener, error);
        else
            error.SetErrorString ("the platform is not currently connected");
    }
    return process_sp;
}

const char *
PlatformDarwin::GetUserName (uint32_t uid)
{
    // Check the cache in Platform in case we have already looked this uid up
    const char *user_name = Platform::GetUserName(uid);
    if (user_name)
        return user_name;

    if (IsRemote() && m_remote_platform_sp)
        return m_remote_platform_sp->GetUserName(uid);
    return NULL;
}

const char *
PlatformDarwin::GetGroupName (uint32_t gid)
{
    const char *group_name = Platform::GetGroupName(gid);
    if (group_name)
        return group_name;

    if (IsRemote() && m_remote_platform_sp)
        return m_remote_platform_sp->GetGroupName(gid);
    return NULL;
}

bool
PlatformDarwin::ModuleIsExcludedForNonModuleSpecificSearches (lldb_private::Target &target, const lldb::ModuleSP &module_sp)
{
    ObjectFile *obj_file = module_sp->GetObjectFile();
    if (!obj_file)
        return false;
    
    ObjectFile::Type obj_type = obj_file->GetType();
    if (obj_type == ObjectFile::eTypeDynamicLinker)
        return true;
    else
        return false;
}


// The architecture selection rules for arm processors
// These cpu subtypes have distinct names (e.g. armv7f) but armv7 binaries run fine on an armv7f processor.

bool
PlatformDarwin::ARMGetSupportedArchitectureAtIndex (uint32_t idx, ArchSpec &arch)
{
    ArchSpec system_arch (GetSystemArchitecture());
    const ArchSpec::Core system_core = system_arch.GetCore();
    switch (system_core)
    {
    default:
        switch (idx)
        {
        case 0: arch.SetTriple ("armv7-apple-darwin", NULL);  return true;
        case 1: arch.SetTriple ("armv7f-apple-darwin", NULL); return true;
        case 2: arch.SetTriple ("armv7k-apple-darwin", NULL); return true;
        case 3: arch.SetTriple ("armv7s-apple-darwin", NULL); return true;
        case 4: arch.SetTriple ("armv6-apple-darwin", NULL);  return true;
        case 5: arch.SetTriple ("armv5-apple-darwin", NULL);  return true;
        case 6: arch.SetTriple ("armv4-apple-darwin", NULL);  return true;
        case 7: arch.SetTriple ("arm-apple-darwin", NULL);    return true;
        default: break;
        }
        break;

    case ArchSpec::eCore_arm_armv7f:
        switch (idx)
        {
        case 0: arch.SetTriple ("armv7f-apple-darwin", NULL); return true;
        case 1: arch.SetTriple ("armv7-apple-darwin", NULL);  return true;
        case 2: arch.SetTriple ("armv6-apple-darwin", NULL);  return true;
        case 3: arch.SetTriple ("armv5-apple-darwin", NULL);  return true;
        case 4: arch.SetTriple ("armv4-apple-darwin", NULL);  return true;
        case 5: arch.SetTriple ("arm-apple-darwin", NULL);    return true;
        default: break;
        }
        break;

    case ArchSpec::eCore_arm_armv7k:
        switch (idx)
        {
        case 0: arch.SetTriple ("armv7k-apple-darwin", NULL); return true;
        case 1: arch.SetTriple ("armv7-apple-darwin", NULL);  return true;
        case 2: arch.SetTriple ("armv6-apple-darwin", NULL);  return true;
        case 3: arch.SetTriple ("armv5-apple-darwin", NULL);  return true;
        case 4: arch.SetTriple ("armv4-apple-darwin", NULL);  return true;
        case 5: arch.SetTriple ("arm-apple-darwin", NULL);    return true;
        default: break;
        }
        break;

    case ArchSpec::eCore_arm_armv7s:
        switch (idx)
        {
        case 0: arch.SetTriple ("armv7s-apple-darwin", NULL); return true;
        case 1: arch.SetTriple ("armv7-apple-darwin", NULL);  return true;
        case 2: arch.SetTriple ("armv6-apple-darwin", NULL);  return true;
        case 3: arch.SetTriple ("armv5-apple-darwin", NULL);  return true;
        case 4: arch.SetTriple ("armv4-apple-darwin", NULL);  return true;
        case 5: arch.SetTriple ("arm-apple-darwin", NULL);    return true;
        default: break;
        }
        break;

    case ArchSpec::eCore_arm_armv7:
        switch (idx)
        {
        case 0: arch.SetTriple ("armv7-apple-darwin", NULL);  return true;
        case 1: arch.SetTriple ("armv6-apple-darwin", NULL);  return true;
        case 2: arch.SetTriple ("armv5-apple-darwin", NULL);  return true;
        case 3: arch.SetTriple ("armv4-apple-darwin", NULL);  return true;
        case 4: arch.SetTriple ("arm-apple-darwin", NULL);    return true;
        default: break;
        }
        break;

    case ArchSpec::eCore_arm_armv6:
        switch (idx)
        {
        case 0: arch.SetTriple ("armv6-apple-darwin", NULL);  return true;
        case 1: arch.SetTriple ("armv5-apple-darwin", NULL);  return true;
        case 2: arch.SetTriple ("armv4-apple-darwin", NULL);  return true;
        case 3: arch.SetTriple ("arm-apple-darwin", NULL);    return true;
        default: break;
        }
        break;

    case ArchSpec::eCore_arm_armv5:
        switch (idx)
        {
        case 0: arch.SetTriple ("armv5-apple-darwin", NULL);  return true;
        case 1: arch.SetTriple ("armv4-apple-darwin", NULL);  return true;
        case 2: arch.SetTriple ("arm-apple-darwin", NULL);    return true;
        default: break;
        }
        break;

    case ArchSpec::eCore_arm_armv4:
        switch (idx)
        {
        case 0: arch.SetTriple ("armv4-apple-darwin", NULL);  return true;
        case 1: arch.SetTriple ("arm-apple-darwin", NULL);    return true;
        default: break;
        }
        break;
    }
    arch.Clear();
    return false;
}
