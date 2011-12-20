//===-- ProcessFreeBSD.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// C Includes
#include <errno.h>

// C++ Includes
// Other libraries and framework includes
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/State.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/Target.h"

#include "ProcessFreeBSD.h"
#include "ProcessFreeBSDLog.h"
#include "Plugins/Process/Utility/InferiorCallPOSIX.h"
#include "ProcessMonitor.h"
#include "FreeBSDThread.h"

using namespace lldb;
using namespace lldb_private;

//------------------------------------------------------------------------------
// Static functions.

Process*
ProcessFreeBSD::CreateInstance(Target& target, Listener &listener)
{
    return new ProcessFreeBSD(target, listener);
}

void
ProcessFreeBSD::Initialize()
{
    static bool g_initialized = false;

    if (!g_initialized)
    {
        PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                      GetPluginDescriptionStatic(),
                                      CreateInstance);

        Log::Callbacks log_callbacks = {
            ProcessFreeBSDLog::DisableLog,
            ProcessFreeBSDLog::EnableLog,
            ProcessFreeBSDLog::ListLogCategories
        };

        Log::RegisterLogChannel (ProcessFreeBSD::GetPluginNameStatic(), log_callbacks);

        g_initialized = true;
    }
}

void
ProcessFreeBSD::Terminate()
{
}

const char *
ProcessFreeBSD::GetPluginNameStatic()
{
    return "plugin.process.freebsd";
}

const char *
ProcessFreeBSD::GetPluginDescriptionStatic()
{
    return "Process plugin for FreeBSD";
}


//------------------------------------------------------------------------------
// Constructors and destructors.

ProcessFreeBSD::ProcessFreeBSD(Target& target, Listener &listener)
    : Process(target, listener),
      m_monitor(NULL),
      m_module(NULL),
      m_in_limbo(false),
      m_exit_now(false)
{
    // FIXME: Putting this code in the ctor and saving the byte order in a
    // member variable is a hack to avoid const qual issues in GetByteOrder.
    ObjectFile *obj_file = GetTarget().GetExecutableModule()->GetObjectFile();
    m_byte_order = obj_file->GetByteOrder();
}

ProcessFreeBSD::~ProcessFreeBSD()
{
    delete m_monitor;
}

//------------------------------------------------------------------------------
// Process protocol.

bool
ProcessFreeBSD::CanDebug(Target &target, bool plugin_specified_by_name)
{
    // For now we are just making sure the file exists for a given module
    ModuleSP exe_module_sp(target.GetExecutableModule());
    if (exe_module_sp.get())
        return exe_module_sp->GetFileSpec().Exists();
    return false;
}

Error
ProcessFreeBSD::DoAttachToProcessWithID(lldb::pid_t pid)
{
    Error error;
    assert(m_monitor == NULL);

    m_monitor = new ProcessMonitor(this, pid, error);

    if (!error.Success())
        return error;

    SetID(pid);
    return error;
}

Error
ProcessFreeBSD::WillLaunch(Module* module)
{
    Error error;
    return error;
}

Error
ProcessFreeBSD::DoLaunch(Module *module,
			 const ProcessLaunchInfo &launch_info)
{
    Error error;
    assert(m_monitor == NULL);

    SetPrivateState(eStateLaunching);

    uint32_t launch_flags = launch_info.GetFlags().Get();
    char const **argv = launch_info.GetArguments().GetConstArgumentVector();
    char const **envp = launch_info.GetEnvironmentEntries().GetConstArgumentVector();
    const char *stdin_path = NULL;
    const char *stdout_path = NULL;
    const char *stderr_path = NULL;
    const char *working_directory = launch_info.GetWorkingDirectory();

    m_monitor = new ProcessMonitor(this, module,
                                   argv, envp,
                                   stdin_path, stdout_path, stderr_path,
                                   error);

    m_module = module;

    if (!error.Success())
        return error;

    SetID(m_monitor->GetPID());
    return error;
}

void
ProcessFreeBSD::DidLaunch()
{
}

Error
ProcessFreeBSD::DoResume()
{
    StateType state = GetPrivateState();

    assert(state == eStateStopped || state == eStateCrashed);

    // We are about to resume a thread that will cause the process to exit so
    // set our exit status now.  Do not change our state if the inferior
    // crashed.
    if (state == eStateStopped) 
    {
        if (m_in_limbo)
            SetExitStatus(m_exit_status, NULL);
        else
            SetPrivateState(eStateRunning);
    }

    bool did_resume = false;
    uint32_t thread_count = m_thread_list.GetSize(false);
    for (uint32_t i = 0; i < thread_count; ++i)
    {
        FreeBSDThread *thread = static_cast<FreeBSDThread*>(
            m_thread_list.GetThreadAtIndex(i, false).get());
        did_resume = thread->Resume() || did_resume;
    }
    assert(did_resume && "Process resume failed!");

    return Error();
}

addr_t
ProcessFreeBSD::GetImageInfoAddress()
{
    Target *target = &GetTarget();
    ObjectFile *obj_file = target->GetExecutableModule()->GetObjectFile();
    Address addr = obj_file->GetImageInfoAddress();

    if (addr.IsValid()) 
        return addr.GetLoadAddress(target);
    else
        return LLDB_INVALID_ADDRESS;
}

Error
ProcessFreeBSD::DoHalt(bool &caused_stop)
{
    Error error;

    if (IsStopped())
    {
        caused_stop = false;
    }
    else if (kill(GetID(), SIGSTOP))
    {
        caused_stop = false;
        error.SetErrorToErrno();
    }
    else
    {
        caused_stop = true;
    }

    return error;
}

Error
ProcessFreeBSD::DoDetach()
{
    Error error;

    error = m_monitor->Detach();
    if (error.Success())
        SetPrivateState(eStateDetached);

    return error;
}

Error
ProcessFreeBSD::DoSignal(int signal)
{
    Error error;

    if (kill(GetID(), signal))
        error.SetErrorToErrno();

    return error;
}

Error
ProcessFreeBSD::DoDestroy()
{
    Error error;

    if (!HasExited())
    {
        // Drive the exit event to completion (do not keep the inferior in
        // limbo).
        m_exit_now = true;

        if (kill(m_monitor->GetPID(), SIGKILL) && error.Success())
        {
            error.SetErrorToErrno();
            return error;
        }

        SetPrivateState(eStateExited);
    }

    return error;
}

void
ProcessFreeBSD::SendMessage(const ProcessMessage &message)
{
    Mutex::Locker lock(m_message_mutex);

    switch (message.GetKind())
    {
    default:
        assert(false && "Unexpected process message!");
        break;

    case ProcessMessage::eInvalidMessage:
        return;

    case ProcessMessage::eLimboMessage:
        m_in_limbo = true;
        m_exit_status = message.GetExitStatus();
        if (m_exit_now)
        {
            SetPrivateState(eStateExited);
            m_monitor->Detach();
        }
        else
            SetPrivateState(eStateStopped);
        break;

    case ProcessMessage::eExitMessage:
        m_exit_status = message.GetExitStatus();
        SetExitStatus(m_exit_status, NULL);
        break;

    case ProcessMessage::eTraceMessage:
    case ProcessMessage::eBreakpointMessage:
        SetPrivateState(eStateStopped);
        break;

    case ProcessMessage::eSignalMessage:
    case ProcessMessage::eSignalDeliveredMessage:
        SetPrivateState(eStateStopped);
        break;

    case ProcessMessage::eCrashMessage:
        SetPrivateState(eStateCrashed);
        break;
    }

    m_message_queue.push(message);
}

void
ProcessFreeBSD::RefreshStateAfterStop()
{
    Mutex::Locker lock(m_message_mutex);
    if (m_message_queue.empty())
        return;

    ProcessMessage &message = m_message_queue.front();

    // Resolve the thread this message corresponds to and pass it along.
    lldb::tid_t tid = message.GetTID();
    FreeBSDThread *thread = static_cast<FreeBSDThread*>(
        GetThreadList().FindThreadByID(tid, false).get());

    thread->Notify(message);

    m_message_queue.pop();
}

bool
ProcessFreeBSD::IsAlive()
{
    StateType state = GetPrivateState();
    return state != eStateDetached && state != eStateExited && state != eStateInvalid;
}

size_t
ProcessFreeBSD::DoReadMemory(addr_t vm_addr,
                           void *buf, size_t size, Error &error)
{
    return m_monitor->ReadMemory(vm_addr, buf, size, error);
}

size_t
ProcessFreeBSD::DoWriteMemory(addr_t vm_addr, const void *buf, size_t size,
                            Error &error)
{
    return m_monitor->WriteMemory(vm_addr, buf, size, error);
}

addr_t
ProcessFreeBSD::DoAllocateMemory(size_t size, uint32_t permissions,
                               Error &error)
{
    addr_t allocated_addr = LLDB_INVALID_ADDRESS;

    unsigned prot = 0;
    if (permissions & lldb::ePermissionsReadable)
        prot |= eMmapProtRead;
    if (permissions & lldb::ePermissionsWritable)
        prot |= eMmapProtWrite;
    if (permissions & lldb::ePermissionsExecutable)
        prot |= eMmapProtExec;

    if (InferiorCallMmap(this, allocated_addr, 0, size, prot,
                         eMmapFlagsAnon | eMmapFlagsPrivate, -1, 0)) {
        m_addr_to_mmap_size[allocated_addr] = size;
        error.Clear();
    } else {
        allocated_addr = LLDB_INVALID_ADDRESS;
        error.SetErrorStringWithFormat("unable to allocate %zu bytes of memory with permissions %s", size, GetPermissionsAsCString (permissions));
    }

    return allocated_addr;
}

Error
ProcessFreeBSD::DoDeallocateMemory(lldb::addr_t addr)
{
    Error error;
    MMapMap::iterator pos = m_addr_to_mmap_size.find(addr);
    if (pos != m_addr_to_mmap_size.end() &&
        InferiorCallMunmap(this, addr, pos->second))
        m_addr_to_mmap_size.erase (pos);
    else
        error.SetErrorStringWithFormat("unable to deallocate memory at 0x%lux", addr);

    return error;
}

size_t
ProcessFreeBSD::GetSoftwareBreakpointTrapOpcode(BreakpointSite* bp_site)
{
    static const uint8_t g_i386_opcode[] = { 0xCC };

    ArchSpec arch = GetTarget().GetArchitecture();
    const uint8_t *opcode = NULL;
    size_t opcode_size = 0;

    switch (arch.GetCore())
    {
    default:
        assert(false && "CPU type not supported!");
        break;

    case ArchSpec::eCore_x86_32_i386:
    case ArchSpec::eCore_x86_64_x86_64:
        opcode = g_i386_opcode;
        opcode_size = sizeof(g_i386_opcode);
        break;
    }

    bp_site->SetTrapOpcode(opcode, opcode_size);
    return opcode_size;
}

Error
ProcessFreeBSD::EnableBreakpoint(BreakpointSite *bp_site)
{
    return EnableSoftwareBreakpoint(bp_site);
}

Error
ProcessFreeBSD::DisableBreakpoint(BreakpointSite *bp_site)
{
    return DisableSoftwareBreakpoint(bp_site);
}

uint32_t
ProcessFreeBSD::UpdateThreadListIfNeeded()
{
    // Do not allow recursive updates.
    return m_thread_list.GetSize(false);
}

uint32_t
ProcessFreeBSD::UpdateThreadList(ThreadList &old_thread_list, ThreadList &new_thread_list)
{
  // XXX haxx
  new_thread_list = old_thread_list;
  
	return 0;
}

ByteOrder
ProcessFreeBSD::GetByteOrder() const
{
    // FIXME: We should be able to extract this value directly.  See comment in
    // ProcessFreeBSD().
    return m_byte_order;
}

size_t
ProcessFreeBSD::PutSTDIN(const char *buf, size_t len, Error &error)
{
    ssize_t status;
    if ((status = write(m_monitor->GetTerminalFD(), buf, len)) < 0) 
    {
        error.SetErrorToErrno();
        return 0;
    }
    return status;
}

size_t
ProcessFreeBSD::GetSTDOUT(char *buf, size_t len, Error &error)
{
    ssize_t bytes_read;

    // The terminal file descriptor is always in non-block mode.
    if ((bytes_read = read(m_monitor->GetTerminalFD(), buf, len)) < 0) 
    {
        if (errno != EAGAIN)
            error.SetErrorToErrno();
        return 0;
    }
    return bytes_read;
}

size_t
ProcessFreeBSD::GetSTDERR(char *buf, size_t len, Error &error)
{
    return GetSTDOUT(buf, len, error);
}

UnixSignals &
ProcessFreeBSD::GetUnixSignals()
{
    return m_freebsd_signals;
}

//------------------------------------------------------------------------------
// ProcessInterface protocol.

const char *
ProcessFreeBSD::GetPluginName()
{
    return "process.freebsd";
}

const char *
ProcessFreeBSD::GetShortPluginName()
{
    return "process.freebsd";
}

uint32_t
ProcessFreeBSD::GetPluginVersion()
{
    return 1;
}

void
ProcessFreeBSD::GetPluginCommandHelp(const char *command, Stream *strm)
{
}

Error
ProcessFreeBSD::ExecutePluginCommand(Args &command, Stream *strm)
{
    return Error(1, eErrorTypeGeneric);
}

Log *
ProcessFreeBSD::EnablePluginLogging(Stream *strm, Args &command)
{
    return NULL;
}

//------------------------------------------------------------------------------
// Utility functions.

bool
ProcessFreeBSD::HasExited()
{
    switch (GetPrivateState())
    {
    default:
        break;

    case eStateDetached:
    case eStateExited:
        return true;
    }

    return false;
}

bool
ProcessFreeBSD::IsStopped()
{
    switch (GetPrivateState())
    {
    default:
        break;

    case eStateStopped:
    case eStateCrashed:
    case eStateSuspended:
        return true;
    }

    return false;
}
