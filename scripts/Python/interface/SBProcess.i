//===-- SWIG Interface for SBProcess ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

namespace lldb {

%feature("docstring",
"Represents the process associated with the target program.

SBProcess supports thread iteration. For example (from test/lldbutil.py),

# ==================================================
# Utility functions related to Threads and Processes
# ==================================================

def get_stopped_threads(process, reason):
    '''Returns the thread(s) with the specified stop reason in a list.

    The list can be empty if no such thread exists.
    '''
    threads = []
    for t in process:
        if t.GetStopReason() == reason:
            threads.append(t)
    return threads

...
"
) SBProcess;
class SBProcess
{
public:
    //------------------------------------------------------------------
    /// Broadcaster event bits definitions.
    //------------------------------------------------------------------
    enum
    {
        eBroadcastBitStateChanged   = (1 << 0),
        eBroadcastBitInterrupt      = (1 << 1),
        eBroadcastBitSTDOUT         = (1 << 2),
        eBroadcastBitSTDERR         = (1 << 3)
    };

    SBProcess ();

    SBProcess (const lldb::SBProcess& rhs);

    ~SBProcess();

    void
    Clear ();

    bool
    IsValid() const;

    lldb::SBTarget
    GetTarget() const;

    lldb::ByteOrder
    GetByteOrder() const;

    %feature("autodoc", "
    Writes data into the current process's stdin. API client specifies a Python
    string as the only argument.
    ") PutSTDIN;
    size_t
    PutSTDIN (const char *src, size_t src_len);

    %feature("autodoc", "
    Reads data from the current process's stdout stream. API client specifies
    the size of the buffer to read data into. It returns the byte buffer in a
    Python string.
    ") GetSTDOUT;
    size_t
    GetSTDOUT (char *dst, size_t dst_len) const;

    %feature("autodoc", "
    Reads data from the current process's stderr stream. API client specifies
    the size of the buffer to read data into. It returns the byte buffer in a
    Python string.
    ") GetSTDERR;
    size_t
    GetSTDERR (char *dst, size_t dst_len) const;

    void
    ReportEventState (const lldb::SBEvent &event, FILE *out) const;

    void
    AppendEventStateReport (const lldb::SBEvent &event, lldb::SBCommandReturnObject &result);

    %feature("docstring", "
    //------------------------------------------------------------------
    /// Remote connection related functions. These will fail if the
    /// process is not in eStateConnected. They are intended for use
    /// when connecting to an externally managed debugserver instance.
    //------------------------------------------------------------------
    ") RemoteAttachToProcessWithID;
    bool
    RemoteAttachToProcessWithID (lldb::pid_t pid,
                                 lldb::SBError& error);
    
    %feature("docstring",
    "See SBTarget.Launch for argument description and usage."
    ) RemoteLaunch;
    bool
    RemoteLaunch (char const **argv,
                  char const **envp,
                  const char *stdin_path,
                  const char *stdout_path,
                  const char *stderr_path,
                  const char *working_directory,
                  uint32_t launch_flags,
                  bool stop_at_entry,
                  lldb::SBError& error);
    
    //------------------------------------------------------------------
    // Thread related functions
    //------------------------------------------------------------------
    uint32_t
    GetNumThreads ();

    lldb::SBThread
    GetThreadAtIndex (size_t index);

    lldb::SBThread
    GetThreadByID (lldb::tid_t sb_thread_id);

    lldb::SBThread
    GetSelectedThread () const;

    bool
    SetSelectedThread (const lldb::SBThread &thread);

    bool
    SetSelectedThreadByID (uint32_t tid);

    //------------------------------------------------------------------
    // Stepping related functions
    //------------------------------------------------------------------

    lldb::StateType
    GetState ();

    int
    GetExitStatus ();

    const char *
    GetExitDescription ();

    lldb::pid_t
    GetProcessID ();

    uint32_t
    GetAddressByteSize() const;

    %feature("docstring", "
    Kills the process and shuts down all threads that were spawned to
    track and monitor process.
    ") Destroy;
    lldb::SBError
    Destroy ();

    lldb::SBError
    Continue ();

    lldb::SBError
    Stop ();

    %feature("docstring", "Same as Destroy(self).") Destroy;
    lldb::SBError
    Kill ();

    lldb::SBError
    Detach ();

    %feature("docstring", "Sends the process a unix signal.") Signal;
    lldb::SBError
    Signal (int signal);

    %feature("autodoc", "
    Reads memory from the current process's address space and removes any
    traps that may have been inserted into the memory. It returns the byte
    buffer in a Python string. Example:

    # Read 4 bytes from address 'addr' and assume error.Success() is True.
    content = process.ReadMemory(addr, 4, error)
    # Use 'ascii' encoding as each byte of 'content' is within [0..255].
    new_bytes = bytearray(content, 'ascii')
    ") ReadMemory;
    size_t
    ReadMemory (addr_t addr, void *buf, size_t size, lldb::SBError &error);

    %feature("autodoc", "
    Writes memory to the current process's address space and maintains any
    traps that might be present due to software breakpoints. Example:

    # Create a Python string from the byte array.
    new_value = str(bytes)
    result = process.WriteMemory(addr, new_value, error)
    if not error.Success() or result != len(bytes):
        print 'SBProcess.WriteMemory() failed!'
    ") WriteMemory;
    size_t
    WriteMemory (addr_t addr, const void *buf, size_t size, lldb::SBError &error);

    %feature("autodoc", "
    Reads a NULL terminated C string from the current process's address space.
    It returns a python string of the exact length, or truncates the string if
    the maximum character limit is reached. Example:
    
    # Read a C string of at most 256 bytes from address '0x1000' 
    error = lldb.SBError()
    cstring = process.ReadCStringFromMemory(0x1000, 256, error)
    if error.Success():
        print 'cstring: ', cstring
    else
        print 'error: ', error
    ") ReadCStringFromMemory;

    size_t
    ReadCStringFromMemory (addr_t addr, void *buf, size_t size, lldb::SBError &error);

    %feature("autodoc", "
    Reads an unsigned integer from memory given a byte size and an address. 
    Returns the unsigned integer that was read. Example:
    
    # Read a 4 byte unsigned integer from address 0x1000
    error = lldb.SBError()
    uint = ReadUnsignedFromMemory(0x1000, 4, error)
    if error.Success():
        print 'integer: %u' % uint
    else
        print 'error: ', error

    ") ReadUnsignedFromMemory;

    uint64_t
    ReadUnsignedFromMemory (addr_t addr, uint32_t byte_size, lldb::SBError &error);
    
    %feature("autodoc", "
    Reads a pointer from memory from an address and returns the value. Example:
    
    # Read a pointer from address 0x1000
    error = lldb.SBError()
    ptr = ReadPointerFromMemory(0x1000, error)
    if error.Success():
        print 'pointer: 0x%x' % ptr
    else
        print 'error: ', error
    
    ") ReadPointerFromMemory;
    
    lldb::addr_t
    ReadPointerFromMemory (addr_t addr, lldb::SBError &error);
    

    // Events
    static lldb::StateType
    GetStateFromEvent (const lldb::SBEvent &event);

    static bool
    GetRestartedFromEvent (const lldb::SBEvent &event);

    static lldb::SBProcess
    GetProcessFromEvent (const lldb::SBEvent &event);

    lldb::SBBroadcaster
    GetBroadcaster () const;

    bool
    GetDescription (lldb::SBStream &description);

    uint32_t
    LoadImage (lldb::SBFileSpec &image_spec, lldb::SBError &error);
    
    lldb::SBError
    UnloadImage (uint32_t image_token);

    lldb::SBData
    GetDataFromCString(const char* data);

    lldb::SBData
    GetDataFromUnsignedInt64Array(uint64_t* array, size_t array_len);

    lldb::SBData
    GetDataFromUnsignedInt32Array(uint32_t* array, size_t array_len);

    lldb::SBData
    GetDataFromSignedInt64Array(int64_t* array, size_t array_len);

    lldb::SBData
    GetDataFromSignedInt32Array(int32_t* array, size_t array_len);

    lldb::SBData
    GetDataFromDoubleArray(double* array, size_t array_len);

};

}  // namespace lldb
