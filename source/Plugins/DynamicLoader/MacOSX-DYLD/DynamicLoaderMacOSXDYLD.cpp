//===-- DynamicLoaderMacOSXDYLD.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/DataBuffer.h"
#include "lldb/Core/DataBufferHeap.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/State.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Target/StackFrame.h"

#include "DynamicLoaderMacOSXDYLD.h"

//#define ENABLE_DEBUG_PRINTF // COMMENT THIS LINE OUT PRIOR TO CHECKIN
#ifdef ENABLE_DEBUG_PRINTF
#include <stdio.h>
#define DEBUG_PRINTF(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...)
#endif

using namespace lldb;
using namespace lldb_private;

/// FIXME - The ObjC Runtime trampoline handler doesn't really belong here.
/// I am putting it here so I can invoke it in the Trampoline code here, but
/// it should be moved to the ObjC Runtime support when it is set up.


DynamicLoaderMacOSXDYLD::DYLDImageInfo *
DynamicLoaderMacOSXDYLD::GetImageInfo (Module *module)
{
    const UUID &module_uuid = module->GetUUID();
    DYLDImageInfo::collection::iterator pos, end = m_dyld_image_infos.end();

    // First try just by UUID as it is the safest.
    if (module_uuid.IsValid())
    {
        for (pos = m_dyld_image_infos.begin(); pos != end; ++pos)
        {
            if (pos->uuid == module_uuid)
                return &(*pos);
        }
        
        if (m_dyld.uuid == module_uuid)
            return &m_dyld;
    }

    // Next try by platform path only for things that don't have a valid UUID
    // since if a file has a valid UUID in real life it should also in the
    // dyld info. This is the next safest because the paths in the dyld info
    // are platform paths, not local paths. For local debugging platform == local
    // paths.
    const FileSpec &platform_file_spec = module->GetPlatformFileSpec();
    for (pos = m_dyld_image_infos.begin(); pos != end; ++pos)
    {
        if (pos->file_spec == platform_file_spec && pos->uuid.IsValid() == false)
            return &(*pos);
    }
    
    if (m_dyld.file_spec == platform_file_spec && m_dyld.uuid.IsValid() == false)
        return &m_dyld;

    return NULL;
}

//----------------------------------------------------------------------
// Create an instance of this class. This function is filled into
// the plugin info class that gets handed out by the plugin factory and
// allows the lldb to instantiate an instance of this class.
//----------------------------------------------------------------------
DynamicLoader *
DynamicLoaderMacOSXDYLD::CreateInstance (Process* process, bool force)
{
    bool create = force;
    if (!create)
    {
        create = true;
        Module* exe_module = process->GetTarget().GetExecutableModulePointer();
        if (exe_module)
        {
            ObjectFile *object_file = exe_module->GetObjectFile();
            if (object_file)
            {
                SectionList *section_list = object_file->GetSectionList();
                if (section_list)
                {
                    static ConstString g_kld_section_name ("__KLD");
                    if (section_list->FindSectionByName (g_kld_section_name))
                    {
                        create = false;
                    }
                }
            }
        }
        
        if (create)
        {
            const llvm::Triple &triple_ref = process->GetTarget().GetArchitecture().GetTriple();
            create = triple_ref.getOS() == llvm::Triple::Darwin && triple_ref.getVendor() == llvm::Triple::Apple;
        }
    }
    
    if (create)
        return new DynamicLoaderMacOSXDYLD (process);
    return NULL;
}

//----------------------------------------------------------------------
// Constructor
//----------------------------------------------------------------------
DynamicLoaderMacOSXDYLD::DynamicLoaderMacOSXDYLD (Process* process) :
    DynamicLoader(process),
    m_dyld(),
    m_dyld_all_image_infos_addr(LLDB_INVALID_ADDRESS),
    m_dyld_all_image_infos(),
    m_dyld_all_image_infos_stop_id (UINT32_MAX),
    m_break_id(LLDB_INVALID_BREAK_ID),
    m_dyld_image_infos(),
    m_dyld_image_infos_stop_id (UINT32_MAX),
    m_mutex(Mutex::eMutexTypeRecursive)
{
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
DynamicLoaderMacOSXDYLD::~DynamicLoaderMacOSXDYLD()
{
    Clear(true);
}

//------------------------------------------------------------------
/// Called after attaching a process.
///
/// Allow DynamicLoader plug-ins to execute some code after
/// attaching to a process.
//------------------------------------------------------------------
void
DynamicLoaderMacOSXDYLD::DidAttach ()
{
    PrivateInitialize(m_process);
    LocateDYLD ();
    SetNotificationBreakpoint ();
}

//------------------------------------------------------------------
/// Called after attaching a process.
///
/// Allow DynamicLoader plug-ins to execute some code after
/// attaching to a process.
//------------------------------------------------------------------
void
DynamicLoaderMacOSXDYLD::DidLaunch ()
{
    PrivateInitialize(m_process);
    LocateDYLD ();
    SetNotificationBreakpoint ();
}


//----------------------------------------------------------------------
// Clear out the state of this class.
//----------------------------------------------------------------------
void
DynamicLoaderMacOSXDYLD::Clear (bool clear_process)
{
    Mutex::Locker locker(m_mutex);

    if (m_process->IsAlive() && LLDB_BREAK_ID_IS_VALID(m_break_id))
        m_process->ClearBreakpointSiteByID(m_break_id);

    if (clear_process)
        m_process = NULL;
    m_dyld.Clear(false);
    m_dyld_all_image_infos_addr = LLDB_INVALID_ADDRESS;
    m_dyld_all_image_infos.Clear();
    m_break_id = LLDB_INVALID_BREAK_ID;
    m_dyld_image_infos.clear();
}

//----------------------------------------------------------------------
// Check if we have found DYLD yet
//----------------------------------------------------------------------
bool
DynamicLoaderMacOSXDYLD::DidSetNotificationBreakpoint() const
{
    return LLDB_BREAK_ID_IS_VALID (m_break_id);
}

//----------------------------------------------------------------------
// Try and figure out where dyld is by first asking the Process
// if it knows (which currently calls down in the the lldb::Process
// to get the DYLD info (available on SnowLeopard only). If that fails,
// then check in the default addresses.
//----------------------------------------------------------------------
bool
DynamicLoaderMacOSXDYLD::LocateDYLD()
{
    if (m_dyld_all_image_infos_addr == LLDB_INVALID_ADDRESS)
        m_dyld_all_image_infos_addr = m_process->GetImageInfoAddress ();

    if (m_dyld_all_image_infos_addr != LLDB_INVALID_ADDRESS)
    {
        if (ReadAllImageInfosStructure ())
        {
            if (m_dyld_all_image_infos.dyldImageLoadAddress != LLDB_INVALID_ADDRESS)
                return ReadDYLDInfoFromMemoryAndSetNotificationCallback (m_dyld_all_image_infos.dyldImageLoadAddress);
            else
                return ReadDYLDInfoFromMemoryAndSetNotificationCallback (m_dyld_all_image_infos_addr & 0xfffffffffff00000ull);
        }
    }

    // Check some default values
    Module *executable = m_process->GetTarget().GetExecutableModulePointer();

    if (executable)
    {
        const ArchSpec &exe_arch = executable->GetArchitecture();
        if (exe_arch.GetAddressByteSize() == 8)
        {
            return ReadDYLDInfoFromMemoryAndSetNotificationCallback(0x7fff5fc00000ull);
        }
        else if (exe_arch.GetMachine() == llvm::Triple::arm || exe_arch.GetMachine() == llvm::Triple::thumb)
        {
            return ReadDYLDInfoFromMemoryAndSetNotificationCallback(0x2fe00000);
        }
        else
        {
            return ReadDYLDInfoFromMemoryAndSetNotificationCallback(0x8fe00000);
        }
    }
    return false;
}

ModuleSP
DynamicLoaderMacOSXDYLD::FindTargetModuleForDYLDImageInfo (const DYLDImageInfo &image_info, bool can_create, bool *did_create_ptr)
{
    if (did_create_ptr)
        *did_create_ptr = false;
    ModuleSP module_sp;
    ModuleList &target_images = m_process->GetTarget().GetImages();
    const bool image_info_uuid_is_valid = image_info.uuid.IsValid();
    if (image_info_uuid_is_valid)
        module_sp = target_images.FindModule(image_info.uuid);
    
    if (!module_sp)
    {
        ArchSpec arch(image_info.GetArchitecture ());

        module_sp = target_images.FindFirstModuleForFileSpec (image_info.file_spec, &arch, NULL);

        if (can_create)
        {
            if (module_sp)
            {
                if (image_info_uuid_is_valid)
                {
                    if (module_sp->GetUUID() != image_info.uuid)
                        module_sp.reset();
                }
                else
                {
                    // No UUID, we must rely upon the cached module modification 
                    // time and the modification time of the file on disk
                    if (module_sp->GetModificationTime() != module_sp->GetFileSpec().GetModificationTime())
                        module_sp.reset();
                }
            }
            
            if (!module_sp)
            {
                module_sp = m_process->GetTarget().GetSharedModule (image_info.file_spec,
                                                                    arch,
                                                                    image_info_uuid_is_valid ? &image_info.uuid : NULL);
                if (did_create_ptr)
                    *did_create_ptr = module_sp;
            }
        }
    }
    return module_sp;
}

//----------------------------------------------------------------------
// Assume that dyld is in memory at ADDR and try to parse it's load
// commands
//----------------------------------------------------------------------
bool
DynamicLoaderMacOSXDYLD::ReadDYLDInfoFromMemoryAndSetNotificationCallback(lldb::addr_t addr)
{
    DataExtractor data; // Load command data
    if (ReadMachHeader (addr, &m_dyld.header, &data))
    {
        if (m_dyld.header.filetype == llvm::MachO::HeaderFileTypeDynamicLinkEditor)
        {
            m_dyld.address = addr;
            ModuleSP dyld_module_sp;
            if (ParseLoadCommands (data, m_dyld, &m_dyld.file_spec))
            {
                if (m_dyld.file_spec)
                {
                    dyld_module_sp = FindTargetModuleForDYLDImageInfo (m_dyld, true, NULL);

                    if (dyld_module_sp)
                        UpdateImageLoadAddress (dyld_module_sp.get(), m_dyld);
                }
            }

            if (m_dyld_all_image_infos_addr == LLDB_INVALID_ADDRESS && dyld_module_sp.get())
            {
                static ConstString g_dyld_all_image_infos ("dyld_all_image_infos");
                const Symbol *symbol = dyld_module_sp->FindFirstSymbolWithNameAndType (g_dyld_all_image_infos, eSymbolTypeData);
                if (symbol)
                    m_dyld_all_image_infos_addr = symbol->GetValue().GetLoadAddress(&m_process->GetTarget());
            }

            // Update all image infos
            InitializeFromAllImageInfos ();

            // If we didn't have an executable before, but now we do, then the
            // dyld module shared pointer might be unique and we may need to add
            // it again (since Target::SetExecutableModule() will clear the
            // images). So append the dyld module back to the list if it is
            /// unique!
            if (dyld_module_sp && m_process->GetTarget().GetImages().AppendIfNeeded (dyld_module_sp))
                UpdateImageLoadAddress(dyld_module_sp.get(), m_dyld);

            return true;
        }
    }
    return false;
}

bool
DynamicLoaderMacOSXDYLD::NeedToLocateDYLD () const
{
    return m_dyld_all_image_infos_addr == LLDB_INVALID_ADDRESS;
}

bool
DynamicLoaderMacOSXDYLD::UpdateCommPageLoadAddress(Module *module)
{
    bool changed = false;
    if (module)
    {
        ObjectFile *image_object_file = module->GetObjectFile();
        if (image_object_file)
        {
            SectionList *section_list = image_object_file->GetSectionList ();
            if (section_list)
            {
                uint32_t num_sections = section_list->GetSize();
                for (uint32_t i=0; i<num_sections; ++i)
                {
                    Section* section = section_list->GetSectionAtIndex (i).get();
                    if (section)
                    {
                        const addr_t new_section_load_addr = section->GetFileAddress ();
                        const addr_t old_section_load_addr = m_process->GetTarget().GetSectionLoadList().GetSectionLoadAddress (section);
                        if (old_section_load_addr == LLDB_INVALID_ADDRESS ||
                            old_section_load_addr != new_section_load_addr)
                        {
                            if (m_process->GetTarget().GetSectionLoadList().SetSectionLoadAddress (section, section->GetFileAddress ()))
                                changed = true;
                        }
                    }
                }
            }
        }
    }
    return changed;
}

//----------------------------------------------------------------------
// Update the load addresses for all segments in MODULE using the
// updated INFO that is passed in.
//----------------------------------------------------------------------
bool
DynamicLoaderMacOSXDYLD::UpdateImageLoadAddress (Module *module, DYLDImageInfo& info)
{
    bool changed = false;
    if (module)
    {
        ObjectFile *image_object_file = module->GetObjectFile();
        if (image_object_file)
        {
            SectionList *section_list = image_object_file->GetSectionList ();
            if (section_list)
            {
                // We now know the slide amount, so go through all sections
                // and update the load addresses with the correct values.
                uint32_t num_segments = info.segments.size();
                for (uint32_t i=0; i<num_segments; ++i)
                {
                    SectionSP section_sp(section_list->FindSectionByName(info.segments[i].name));
                    const addr_t new_section_load_addr = info.segments[i].vmaddr + info.slide;
                    if (section_sp)
                    {
                        const addr_t old_section_load_addr = m_process->GetTarget().GetSectionLoadList().GetSectionLoadAddress (section_sp.get());
                        if (old_section_load_addr == LLDB_INVALID_ADDRESS ||
                            old_section_load_addr != new_section_load_addr)
                        {
                            if (m_process->GetTarget().GetSectionLoadList().SetSectionLoadAddress (section_sp.get(), new_section_load_addr))
                                changed = true;
                        }
                    }
                    else
                    {
                        Host::SystemLog (Host::eSystemLogWarning, 
                                         "warning: unable to find and load segment named '%s' at 0x%llx in '%s/%s' in macosx dynamic loader plug-in.\n",
                                         info.segments[i].name.AsCString("<invalid>"),
                                         (uint64_t)new_section_load_addr,
                                         image_object_file->GetFileSpec().GetDirectory().AsCString(),
                                         image_object_file->GetFileSpec().GetFilename().AsCString());
                    }
                }
            }
        }
    }
    return changed;
}

//----------------------------------------------------------------------
// Update the load addresses for all segments in MODULE using the
// updated INFO that is passed in.
//----------------------------------------------------------------------
bool
DynamicLoaderMacOSXDYLD::UnloadImageLoadAddress (Module *module, DYLDImageInfo& info)
{
    bool changed = false;
    if (module)
    {
        ObjectFile *image_object_file = module->GetObjectFile();
        if (image_object_file)
        {
            SectionList *section_list = image_object_file->GetSectionList ();
            if (section_list)
            {
                uint32_t num_segments = info.segments.size();
                for (uint32_t i=0; i<num_segments; ++i)
                {
                    SectionSP section_sp(section_list->FindSectionByName(info.segments[i].name));
                    if (section_sp)
                    {
                        const addr_t old_section_load_addr = info.segments[i].vmaddr + info.slide;
                        if (m_process->GetTarget().GetSectionLoadList().SetSectionUnloaded (section_sp.get(), old_section_load_addr))
                            changed = true;
                    }
                    else
                    {
                        Host::SystemLog (Host::eSystemLogWarning, 
                                         "warning: unable to find and unload segment named '%s' in '%s/%s' in macosx dynamic loader plug-in.\n",
                                         info.segments[i].name.AsCString("<invalid>"),
                                         image_object_file->GetFileSpec().GetDirectory().AsCString(),
                                         image_object_file->GetFileSpec().GetFilename().AsCString());
                    }
                }
            }
        }
    }
    return changed;
}


//----------------------------------------------------------------------
// Static callback function that gets called when our DYLD notification
// breakpoint gets hit. We update all of our image infos and then
// let our super class DynamicLoader class decide if we should stop
// or not (based on global preference).
//----------------------------------------------------------------------
bool
DynamicLoaderMacOSXDYLD::NotifyBreakpointHit (void *baton, 
                                              StoppointCallbackContext *context, 
                                              lldb::user_id_t break_id, 
                                              lldb::user_id_t break_loc_id)
{
    // Let the event know that the images have changed
    // DYLD passes three arguments to the notification breakpoint.
    // Arg1: enum dyld_image_mode mode - 0 = adding, 1 = removing 
    // Arg2: uint32_t infoCount        - Number of shared libraries added  
    // Arg3: dyld_image_info info[]    - Array of structs of the form:
    //                                     const struct mach_header *imageLoadAddress
    //                                     const char               *imageFilePath
    //                                     uintptr_t                 imageFileModDate (a time_t)
    
    DynamicLoaderMacOSXDYLD* dyld_instance = (DynamicLoaderMacOSXDYLD*) baton;
    
    // First step is to see if we've already initialized the all image infos.  If we haven't then this function
    // will do so and return true.  In the course of initializing the all_image_infos it will read the complete
    // current state, so we don't need to figure out what has changed from the data passed in to us.
    
    if (dyld_instance->InitializeFromAllImageInfos())
        return dyld_instance->GetStopWhenImagesChange(); 

    Process *process = context->exe_ctx.GetProcessPtr();
    const lldb::ABISP &abi = process->GetABI();
    if (abi != NULL)
    {
        // Build up the value array to store the three arguments given above, then get the values from the ABI:
        
        ClangASTContext *clang_ast_context = process->GetTarget().GetScratchClangASTContext();
        ValueList argument_values;
        Value input_value;
        
        void *clang_void_ptr_type = clang_ast_context->GetVoidPtrType(false);
        void *clang_uint32_type   = clang_ast_context->GetBuiltinTypeForEncodingAndBitSize(lldb::eEncodingUint, 32);
        input_value.SetValueType (Value::eValueTypeScalar);
        input_value.SetContext (Value::eContextTypeClangType, clang_uint32_type);
        argument_values.PushValue(input_value);
        argument_values.PushValue(input_value);
        input_value.SetContext (Value::eContextTypeClangType, clang_void_ptr_type);
        argument_values.PushValue (input_value);
        
        if (abi->GetArgumentValues (context->exe_ctx.GetThreadRef(), argument_values))
        {
            uint32_t dyld_mode = argument_values.GetValueAtIndex(0)->GetScalar().UInt (-1);
            if (dyld_mode != -1)
            {
                // Okay the mode was right, now get the number of elements, and the array of new elements...
                uint32_t image_infos_count = argument_values.GetValueAtIndex(1)->GetScalar().UInt (-1);
                if (image_infos_count != -1)
                {
                    // Got the number added, now go through the array of added elements, putting out the mach header 
                    // address, and adding the image.
                    // Note, I'm not putting in logging here, since the AddModules & RemoveModules functions do
                    // all the logging internally.
                    
                    lldb::addr_t image_infos_addr = argument_values.GetValueAtIndex(2)->GetScalar().ULongLong();
                    if (dyld_mode == 0)
                    {
                        // This is add:
                        dyld_instance->AddModulesUsingImageInfosAddress (image_infos_addr, image_infos_count);
                    }
                    else
                    {
                        // This is remove:
                        dyld_instance->RemoveModulesUsingImageInfosAddress (image_infos_addr, image_infos_count);
                    }
                    
                }
            }
        }
    }
    
    // Return true to stop the target, false to just let the target run
    return dyld_instance->GetStopWhenImagesChange();
}

bool
DynamicLoaderMacOSXDYLD::ReadAllImageInfosStructure ()
{
    Mutex::Locker locker(m_mutex);

    // the all image infos is already valid for this process stop ID
    if (m_process->GetStopID() == m_dyld_all_image_infos_stop_id)
        return true;

    m_dyld_all_image_infos.Clear();
    if (m_dyld_all_image_infos_addr != LLDB_INVALID_ADDRESS)
    {
        ByteOrder byte_order = m_process->GetTarget().GetArchitecture().GetByteOrder();
        uint32_t addr_size = 4;
        if (m_dyld_all_image_infos_addr > UINT32_MAX)
            addr_size = 8;

        uint8_t buf[256];
        DataExtractor data (buf, sizeof(buf), byte_order, addr_size);
        uint32_t offset = 0;

        const size_t count_v2 =  sizeof (uint32_t) + // version
                                 sizeof (uint32_t) + // infoArrayCount
                                 addr_size +         // infoArray
                                 addr_size +         // notification
                                 addr_size +         // processDetachedFromSharedRegion + libSystemInitialized + pad
                                 addr_size;          // dyldImageLoadAddress
        const size_t count_v11 = count_v2 +
                                 addr_size +         // jitInfo
                                 addr_size +         // dyldVersion
                                 addr_size +         // errorMessage
                                 addr_size +         // terminationFlags
                                 addr_size +         // coreSymbolicationShmPage
                                 addr_size +         // systemOrderFlag
                                 addr_size +         // uuidArrayCount
                                 addr_size +         // uuidArray
                                 addr_size +         // dyldAllImageInfosAddress
                                 addr_size +         // initialImageCount
                                 addr_size +         // errorKind
                                 addr_size +         // errorClientOfDylibPath
                                 addr_size +         // errorTargetDylibPath
                                 addr_size;          // errorSymbol
        assert (sizeof (buf) >= count_v11);

        int count;
        Error error;
        if (m_process->ReadMemory (m_dyld_all_image_infos_addr, buf, 4, error) == 4)
        {
            m_dyld_all_image_infos.version = data.GetU32(&offset);
            // If anything in the high byte is set, we probably got the byte 
            // order incorrect (the process might not have it set correctly 
            // yet due to attaching to a program without a specified file).
            if (m_dyld_all_image_infos.version & 0xff000000)
            {
                // We have guessed the wrong byte order. Swap it and try
                // reading the version again.
                if (byte_order == eByteOrderLittle)
                    byte_order = eByteOrderBig;
                else
                    byte_order = eByteOrderLittle;

                data.SetByteOrder (byte_order);
                offset = 0;
                m_dyld_all_image_infos.version = data.GetU32(&offset);
            }
        }
        else
        {
            return false;
        }

        if (m_dyld_all_image_infos.version >= 11)
            count = count_v11;
        else
            count = count_v2;

        const size_t bytes_read = m_process->ReadMemory (m_dyld_all_image_infos_addr, buf, count, error);
        if (bytes_read == count)
        {
            offset = 0;
            m_dyld_all_image_infos.version = data.GetU32(&offset);
            m_dyld_all_image_infos.dylib_info_count = data.GetU32(&offset);
            m_dyld_all_image_infos.dylib_info_addr = data.GetPointer(&offset);
            m_dyld_all_image_infos.notification = data.GetPointer(&offset);
            m_dyld_all_image_infos.processDetachedFromSharedRegion = data.GetU8(&offset);
            m_dyld_all_image_infos.libSystemInitialized = data.GetU8(&offset);
            // Adjust for padding.
            offset += addr_size - 2;
            m_dyld_all_image_infos.dyldImageLoadAddress = data.GetPointer(&offset);
            if (m_dyld_all_image_infos.version >= 11)
            {
                offset += addr_size * 8;
                uint64_t dyld_all_image_infos_addr = data.GetPointer(&offset);

                // When we started, we were given the actual address of the all_image_infos
                // struct (probably via TASK_DYLD_INFO) in memory - this address is stored in
                // m_dyld_all_image_infos_addr and is the most accurate address we have.

                // We read the dyld_all_image_infos struct from memory; it contains its own address.
                // If the address in the struct does not match the actual address,
                // the dyld we're looking at has been loaded at a different location (slid) from
                // where it intended to load.  The addresses in the dyld_all_image_infos struct
                // are the original, non-slid addresses, and need to be adjusted.  Most importantly
                // the address of dyld and the notification address need to be adjusted.

                if (dyld_all_image_infos_addr != m_dyld_all_image_infos_addr)
                {
                    uint64_t image_infos_offset = dyld_all_image_infos_addr - m_dyld_all_image_infos.dyldImageLoadAddress;
                    uint64_t notification_offset = m_dyld_all_image_infos.notification - m_dyld_all_image_infos.dyldImageLoadAddress;
                    m_dyld_all_image_infos.dyldImageLoadAddress = m_dyld_all_image_infos_addr - image_infos_offset;
                    m_dyld_all_image_infos.notification = m_dyld_all_image_infos.dyldImageLoadAddress + notification_offset;
                }
            }
            m_dyld_all_image_infos_stop_id = m_process->GetStopID();
            return true;
        }
    }
    return false;
}


bool
DynamicLoaderMacOSXDYLD::AddModulesUsingImageInfosAddress (lldb::addr_t image_infos_addr, uint32_t image_infos_count)
{
    DYLDImageInfo::collection image_infos;
    LogSP log(lldb_private::GetLogIfAnyCategoriesSet (LIBLLDB_LOG_DYNAMIC_LOADER));
    if (log)
        log->Printf ("Adding %d modules.\n", image_infos_count);
        
    Mutex::Locker locker(m_mutex);
    if (m_process->GetStopID() == m_dyld_image_infos_stop_id)
        return true;

    if (!ReadImageInfos (image_infos_addr, image_infos_count, image_infos))
        return false;
        
    UpdateImageInfosHeaderAndLoadCommands (image_infos, image_infos_count, false);
    bool return_value = AddModulesUsingImageInfos (image_infos);
    m_dyld_image_infos_stop_id = m_process->GetStopID();
    return return_value;
}

// Adds the modules in image_infos to m_dyld_image_infos.  
// NB don't call this passing in m_dyld_image_infos.

bool
DynamicLoaderMacOSXDYLD::AddModulesUsingImageInfos (DYLDImageInfo::collection &image_infos)
{
    // Now add these images to the main list.
    ModuleList loaded_module_list;
    LogSP log(lldb_private::GetLogIfAnyCategoriesSet (LIBLLDB_LOG_DYNAMIC_LOADER));
    
    for (uint32_t idx = 0; idx < image_infos.size(); ++idx)
    {
        if (log)
        {
            log->Printf ("Adding new image at address=0x%16.16llx.", image_infos[idx].address);
            image_infos[idx].PutToLog (log.get());
        }
        
        m_dyld_image_infos.push_back(image_infos[idx]);
        
        ModuleSP image_module_sp (FindTargetModuleForDYLDImageInfo (image_infos[idx], true, NULL));

        if (image_module_sp)
        {
            if (image_infos[idx].header.filetype == llvm::MachO::HeaderFileTypeDynamicLinkEditor)
                image_module_sp->SetIsDynamicLinkEditor (true);

            ObjectFile *objfile = image_module_sp->GetObjectFile ();
            if (objfile)
            {
                SectionList *sections = objfile->GetSectionList();
                if (sections)
                {
                    ConstString commpage_dbstr("__commpage");
                    Section *commpage_section = sections->FindSectionByName(commpage_dbstr).get();
                    if (commpage_section)
                    {
                        ModuleList& target_images = m_process->GetTarget().GetImages();
                        const FileSpec objfile_file_spec = objfile->GetFileSpec();
                        ArchSpec arch (image_infos[idx].GetArchitecture ());
                        ModuleSP commpage_image_module_sp(target_images.FindFirstModuleForFileSpec (objfile_file_spec, 
                                                                                                    &arch, 
                                                                                                    &commpage_dbstr));
                        if (!commpage_image_module_sp)
                        {
                            commpage_image_module_sp 
                                    = m_process->GetTarget().GetSharedModule (image_infos[idx].file_spec,
                                                                              arch,
                                                                              NULL,
                                                                              &commpage_dbstr,
                                                                              objfile->GetOffset() + commpage_section->GetFileOffset());
                        }
                        if (commpage_image_module_sp)
                            UpdateCommPageLoadAddress (commpage_image_module_sp.get());
                    }
                }
            }

            // UpdateImageLoadAddress will return true if any segments
            // change load address. We need to check this so we don't
            // mention that all loaded shared libraries are newly loaded
            // each time we hit out dyld breakpoint since dyld will list all
            // shared libraries each time.
            if (UpdateImageLoadAddress (image_module_sp.get(), image_infos[idx]))
            {
                loaded_module_list.AppendIfNeeded (image_module_sp);
            }
        }
    }
    
    if (loaded_module_list.GetSize() > 0)
    {
        // FIXME: This should really be in the Runtime handlers class, which should get
        // called by the target's ModulesDidLoad, but we're doing it all locally for now 
        // to save time.
        // Also, I'm assuming there can be only one libobjc dylib loaded...
        
        ObjCLanguageRuntime *objc_runtime = m_process->GetObjCLanguageRuntime();
        if (objc_runtime != NULL && !objc_runtime->HasReadObjCLibrary())
        {
            size_t num_modules = loaded_module_list.GetSize();
            for (int i = 0; i < num_modules; i++)
            {
                if (objc_runtime->IsModuleObjCLibrary (loaded_module_list.GetModuleAtIndex (i)))
                {
                    objc_runtime->ReadObjCLibrary (loaded_module_list.GetModuleAtIndex (i));
                    break;
                }
            }
        }
        if (log)
            loaded_module_list.LogUUIDAndPaths (log, "DynamicLoaderMacOSXDYLD::ModulesDidLoad");
        m_process->GetTarget().ModulesDidLoad (loaded_module_list);
    }
    return true;
}

bool
DynamicLoaderMacOSXDYLD::RemoveModulesUsingImageInfosAddress (lldb::addr_t image_infos_addr, uint32_t image_infos_count)
{
    DYLDImageInfo::collection image_infos;
    LogSP log(lldb_private::GetLogIfAnyCategoriesSet (LIBLLDB_LOG_DYNAMIC_LOADER));
    
    Mutex::Locker locker(m_mutex);
    if (m_process->GetStopID() == m_dyld_image_infos_stop_id)
        return true;

    // First read in the image_infos for the removed modules, and their headers & load commands.
    if (!ReadImageInfos (image_infos_addr, image_infos_count, image_infos))
    {
        if (log)
            log->PutCString ("Failed reading image infos array.");
        return false;
    }
    
    if (log)
        log->Printf ("Removing %d modules.", image_infos_count);
    
    ModuleList unloaded_module_list;
    for (uint32_t idx = 0; idx < image_infos.size(); ++idx)
    {        
        if (log)
        {
            log->Printf ("Removing module at address=0x%16.16llx.", image_infos[idx].address);
            image_infos[idx].PutToLog (log.get());
        }
            
        // Remove this image_infos from the m_all_image_infos.  We do the comparision by address
        // rather than by file spec because we can have many modules with the same "file spec" in the
        // case that they are modules loaded from memory.
        //
        // Also copy over the uuid from the old entry to the removed entry so we can 
        // use it to lookup the module in the module list.
        
        DYLDImageInfo::collection::iterator pos, end = m_dyld_image_infos.end();
        for (pos = m_dyld_image_infos.begin(); pos != end; pos++)
        {
            if (image_infos[idx].address == (*pos).address)
            {
                image_infos[idx].uuid = (*pos).uuid;

                // Add the module from this image_info to the "unloaded_module_list".  We'll remove them all at
                // one go later on.
                
                ModuleSP unload_image_module_sp (FindTargetModuleForDYLDImageInfo (image_infos[idx], false, NULL));
                if (unload_image_module_sp.get())
                {
                    // When we unload, be sure to use the image info from the old list,
                    // since that has sections correctly filled in.
                    UnloadImageLoadAddress (unload_image_module_sp.get(), *pos);
                    unloaded_module_list.AppendIfNeeded (unload_image_module_sp);
                }
                else
                {
                    if (log)
                    {
                        log->Printf ("Could not find module for unloading info entry:");
                        image_infos[idx].PutToLog(log.get());
                    }
                }
                
                // Then remove it from the m_dyld_image_infos:
                
                m_dyld_image_infos.erase(pos);
                break;
            }
        }
        
        if (pos == end)
        {
            if (log)
            {
                log->Printf ("Could not find image_info entry for unloading image:");
                image_infos[idx].PutToLog(log.get());            
            }
        }
    }
    if (unloaded_module_list.GetSize() > 0)
    {
        if (log)
        {
            log->PutCString("Unloaded:");
            unloaded_module_list.LogUUIDAndPaths (log, "DynamicLoaderMacOSXDYLD::ModulesDidUnload");
        }
        m_process->GetTarget().ModulesDidUnload (unloaded_module_list);
    }
    m_dyld_image_infos_stop_id = m_process->GetStopID();
    return true;
}

bool
DynamicLoaderMacOSXDYLD::ReadImageInfos (lldb::addr_t image_infos_addr, 
                                         uint32_t image_infos_count, 
                                         DYLDImageInfo::collection &image_infos)
{
    const ByteOrder endian = m_dyld.GetByteOrder();
    const uint32_t addr_size = m_dyld.GetAddressByteSize();

    image_infos.resize(image_infos_count);
    const size_t count = image_infos.size() * 3 * addr_size;
    DataBufferHeap info_data(count, 0);
    Error error;
    const size_t bytes_read = m_process->ReadMemory (image_infos_addr, 
                                                     info_data.GetBytes(), 
                                                     info_data.GetByteSize(),
                                                     error);
    if (bytes_read == count)
    {
        uint32_t info_data_offset = 0;
        DataExtractor info_data_ref(info_data.GetBytes(), info_data.GetByteSize(), endian, addr_size);
        for (int i = 0; i < image_infos.size() && info_data_ref.ValidOffset(info_data_offset); i++)
        {
            image_infos[i].address = info_data_ref.GetPointer(&info_data_offset);
            lldb::addr_t path_addr = info_data_ref.GetPointer(&info_data_offset);
            image_infos[i].mod_date = info_data_ref.GetPointer(&info_data_offset);

            char raw_path[PATH_MAX];
            m_process->ReadCStringFromMemory (path_addr, raw_path, sizeof(raw_path), error);
            // don't resolve the path
            if (error.Success())
            {
                const bool resolve_path = false;
                image_infos[i].file_spec.SetFile(raw_path, resolve_path);
            }
        }
        return true;
    }
    else
    {
        return false;
    }
}

//----------------------------------------------------------------------
// If we have found where the "_dyld_all_image_infos" lives in memory,
// read the current info from it, and then update all image load
// addresses (or lack thereof).  Only do this if this is the first time
// we're reading the dyld infos.  Return true if we actually read anything,
// and false otherwise.
//----------------------------------------------------------------------
bool
DynamicLoaderMacOSXDYLD::InitializeFromAllImageInfos ()
{
    LogSP log(lldb_private::GetLogIfAnyCategoriesSet (LIBLLDB_LOG_DYNAMIC_LOADER));
    
    Mutex::Locker locker(m_mutex);
    if (m_process->GetStopID() == m_dyld_image_infos_stop_id
          || m_dyld_image_infos.size() != 0)
        return false;

    if (ReadAllImageInfosStructure ())
    {
        // Nothing to load or unload?
        if (m_dyld_all_image_infos.dylib_info_count == 0)
            return true;
        
        if (m_dyld_all_image_infos.dylib_info_addr == 0)
        {
            // DYLD is updating the images now.  So we should say we have no images, and then we'll 
            // figure it out when we hit the added breakpoint.
            return false;
        }
        else
        {
            if (!AddModulesUsingImageInfosAddress (m_dyld_all_image_infos.dylib_info_addr, 
                                                   m_dyld_all_image_infos.dylib_info_count))
            {
                DEBUG_PRINTF( "unable to read all data for all_dylib_infos.");
                m_dyld_image_infos.clear();
            }
        }

        // Now we have one more bit of business.  If there is a library left in the images for our target that
        // doesn't have a load address, then it must be something that we were expecting to load (for instance we
        // read a load command for it) but it didn't in fact load - probably because DYLD_*_PATH pointed
        // to an equivalent version.  We don't want it to stay in the target's module list or it will confuse
        // us, so unload it here.
        Target &target = m_process->GetTarget();
        ModuleList &modules = target.GetImages();
        ModuleList not_loaded_modules;
        size_t num_modules = modules.GetSize();
        for (size_t i = 0; i < num_modules; i++)
        {
            ModuleSP module_sp = modules.GetModuleAtIndex(i);
            if (!module_sp->IsLoadedInTarget (&target))
            {
                if (log)
                {
                    StreamString s;
                    module_sp->GetDescription (&s);
                    log->Printf ("Unloading pre-run module: %s.", s.GetData ());
                }
                not_loaded_modules.Append (module_sp);
            }
        }
        
        if (not_loaded_modules.GetSize() != 0)
        {
            target.ModulesDidUnload(not_loaded_modules);
        }

        return true;
    }
    else
        return false;
}

//----------------------------------------------------------------------
// Read a mach_header at ADDR into HEADER, and also fill in the load
// command data into LOAD_COMMAND_DATA if it is non-NULL.
//
// Returns true if we succeed, false if we fail for any reason.
//----------------------------------------------------------------------
bool
DynamicLoaderMacOSXDYLD::ReadMachHeader (lldb::addr_t addr, llvm::MachO::mach_header *header, DataExtractor *load_command_data)
{
    DataBufferHeap header_bytes(sizeof(llvm::MachO::mach_header), 0);
    Error error;
    size_t bytes_read = m_process->ReadMemory (addr, 
                                               header_bytes.GetBytes(), 
                                               header_bytes.GetByteSize(), 
                                               error);
    if (bytes_read == sizeof(llvm::MachO::mach_header))
    {
        uint32_t offset = 0;
        ::memset (header, 0, sizeof(llvm::MachO::mach_header));

        // Get the magic byte unswapped so we can figure out what we are dealing with
        DataExtractor data(header_bytes.GetBytes(), header_bytes.GetByteSize(), lldb::endian::InlHostByteOrder(), 4);
        header->magic = data.GetU32(&offset);
        lldb::addr_t load_cmd_addr = addr;
        data.SetByteOrder(DynamicLoaderMacOSXDYLD::GetByteOrderFromMagic(header->magic));
        switch (header->magic)
        {
        case llvm::MachO::HeaderMagic32:
        case llvm::MachO::HeaderMagic32Swapped:
            data.SetAddressByteSize(4);
            load_cmd_addr += sizeof(llvm::MachO::mach_header);
            break;

        case llvm::MachO::HeaderMagic64:
        case llvm::MachO::HeaderMagic64Swapped:
            data.SetAddressByteSize(8);
            load_cmd_addr += sizeof(llvm::MachO::mach_header_64);
            break;

        default:
            return false;
        }

        // Read the rest of dyld's mach header
        if (data.GetU32(&offset, &header->cputype, (sizeof(llvm::MachO::mach_header)/sizeof(uint32_t)) - 1))
        {
            if (load_command_data == NULL)
                return true; // We were able to read the mach_header and weren't asked to read the load command bytes

            DataBufferSP load_cmd_data_sp(new DataBufferHeap(header->sizeofcmds, 0));

            size_t load_cmd_bytes_read = m_process->ReadMemory (load_cmd_addr, 
                                                                load_cmd_data_sp->GetBytes(), 
                                                                load_cmd_data_sp->GetByteSize(),
                                                                error);
            
            if (load_cmd_bytes_read == header->sizeofcmds)
            {
                // Set the load command data and also set the correct endian
                // swap settings and the correct address size
                load_command_data->SetData(load_cmd_data_sp, 0, header->sizeofcmds);
                load_command_data->SetByteOrder(data.GetByteOrder());
                load_command_data->SetAddressByteSize(data.GetAddressByteSize());
                return true; // We successfully read the mach_header and the load command data
            }

            return false; // We weren't able to read the load command data
        }
    }
    return false; // We failed the read the mach_header
}


//----------------------------------------------------------------------
// Parse the load commands for an image
//----------------------------------------------------------------------
uint32_t
DynamicLoaderMacOSXDYLD::ParseLoadCommands (const DataExtractor& data, DYLDImageInfo& dylib_info, FileSpec *lc_id_dylinker)
{
    uint32_t offset = 0;
    uint32_t cmd_idx;
    Segment segment;
    dylib_info.Clear (true);

    for (cmd_idx = 0; cmd_idx < dylib_info.header.ncmds; cmd_idx++)
    {
        // Clear out any load command specific data from DYLIB_INFO since
        // we are about to read it.

        if (data.ValidOffsetForDataOfSize (offset, sizeof(llvm::MachO::load_command)))
        {
            llvm::MachO::load_command load_cmd;
            uint32_t load_cmd_offset = offset;
            load_cmd.cmd = data.GetU32 (&offset);
            load_cmd.cmdsize = data.GetU32 (&offset);
            switch (load_cmd.cmd)
            {
            case llvm::MachO::LoadCommandSegment32:
                {
                    segment.name.SetTrimmedCStringWithLength ((const char *)data.GetData(&offset, 16), 16);
                    // We are putting 4 uint32_t values 4 uint64_t values so
                    // we have to use multiple 32 bit gets below.
                    segment.vmaddr = data.GetU32 (&offset);
                    segment.vmsize = data.GetU32 (&offset);
                    segment.fileoff = data.GetU32 (&offset);
                    segment.filesize = data.GetU32 (&offset);
                    // Extract maxprot, initprot, nsects and flags all at once
                    data.GetU32(&offset, &segment.maxprot, 4);
                    dylib_info.segments.push_back (segment);
                }
                break;

            case llvm::MachO::LoadCommandSegment64:
                {
                    segment.name.SetTrimmedCStringWithLength ((const char *)data.GetData(&offset, 16), 16);
                    // Extract vmaddr, vmsize, fileoff, and filesize all at once
                    data.GetU64(&offset, &segment.vmaddr, 4);
                    // Extract maxprot, initprot, nsects and flags all at once
                    data.GetU32(&offset, &segment.maxprot, 4);
                    dylib_info.segments.push_back (segment);
                }
                break;

            case llvm::MachO::LoadCommandDynamicLinkerIdent:
                if (lc_id_dylinker)
                {
                    uint32_t name_offset = load_cmd_offset + data.GetU32 (&offset);
                    const char *path = data.PeekCStr (name_offset);
                    lc_id_dylinker->SetFile (path, true);
                }
                break;

            case llvm::MachO::LoadCommandUUID:
                dylib_info.uuid.SetBytes(data.GetData (&offset, 16));
                break;

            default:
                break;
            }
            // Set offset to be the beginning of the next load command.
            offset = load_cmd_offset + load_cmd.cmdsize;
        }
    }
    
    // All sections listed in the dyld image info structure will all
    // either be fixed up already, or they will all be off by a single
    // slide amount that is determined by finding the first segment
    // that is at file offset zero which also has bytes (a file size
    // that is greater than zero) in the object file.
    
    // Determine the slide amount (if any)
    const size_t num_sections = dylib_info.segments.size();
    for (size_t i = 0; i < num_sections; ++i)
    {
        // Iterate through the object file sections to find the
        // first section that starts of file offset zero and that
        // has bytes in the file...
        if (dylib_info.segments[i].fileoff == 0 && dylib_info.segments[i].filesize > 0)
        {
            dylib_info.slide = dylib_info.address - dylib_info.segments[i].vmaddr;
            // We have found the slide amount, so we can exit
            // this for loop.
            break;
        }
    }
    return cmd_idx;
}

//----------------------------------------------------------------------
// Read the mach_header and load commands for each image that the
// _dyld_all_image_infos structure points to and cache the results.
//----------------------------------------------------------------------

void
DynamicLoaderMacOSXDYLD::UpdateImageInfosHeaderAndLoadCommands(DYLDImageInfo::collection &image_infos, 
                                                               uint32_t infos_count, 
                                                               bool update_executable)
{
    uint32_t exe_idx = UINT32_MAX;
    LogSP log(lldb_private::GetLogIfAnyCategoriesSet (LIBLLDB_LOG_DYNAMIC_LOADER));
    // Read any UUID values that we can get
    for (uint32_t i = 0; i < infos_count; i++)
    {
        if (!image_infos[i].UUIDValid())
        {
            DataExtractor data; // Load command data
            if (!ReadMachHeader (image_infos[i].address, &image_infos[i].header, &data))
                continue;

            ParseLoadCommands (data, image_infos[i], NULL);

            if (image_infos[i].header.filetype == llvm::MachO::HeaderFileTypeExecutable)
                exe_idx = i;
            
        }
    }

    if (exe_idx < image_infos.size())
    {
        const bool can_create = true;
        ModuleSP exe_module_sp (FindTargetModuleForDYLDImageInfo (image_infos[exe_idx], can_create, NULL));

        if (!exe_module_sp)
        {
            ArchSpec exe_arch_spec (image_infos[exe_idx].GetArchitecture ());
            exe_module_sp = m_process->GetTarget().GetSharedModule (image_infos[exe_idx].file_spec,
                                                                    exe_arch_spec,
                                                                    &image_infos[exe_idx].uuid);
        }
        
        if (exe_module_sp)
        {
            if (exe_module_sp.get() != m_process->GetTarget().GetExecutableModulePointer())
            {
                // Don't load dependent images since we are in dyld where we will know
                // and find out about all images that are loaded
                const bool get_dependent_images = false;
                m_process->GetTarget().SetExecutableModule (exe_module_sp, 
                                                            get_dependent_images);
            }
        }
    }
}

//----------------------------------------------------------------------
// On Mac OS X libobjc (the Objective-C runtime) has several critical dispatch
// functions written in hand-written assembly, and also have hand-written unwind
// information in the eh_frame section.  Normally we prefer analyzing the 
// assembly instructions of a curently executing frame to unwind from that frame --
// but on hand-written functions this profiling can fail.  We should use the
// eh_frame instructions for these functions all the time.
//
// As an aside, it would be better if the eh_frame entries had a flag (or were
// extensible so they could have an Apple-specific flag) which indicates that
// the instructions are asynchronous -- accurate at every instruction, instead
// of our normal default assumption that they are not.
//----------------------------------------------------------------------

bool
DynamicLoaderMacOSXDYLD::AlwaysRelyOnEHUnwindInfo (SymbolContext &sym_ctx)
{
    ModuleSP module_sp;
    if (sym_ctx.symbol)
    {
        AddressRange *ar = sym_ctx.symbol->GetAddressRangePtr();
        if (ar)
        {
            module_sp = ar->GetBaseAddress().GetModule();
        }
    }
    if (module_sp.get() == NULL && sym_ctx.function)
    {
        module_sp = sym_ctx.function->GetAddressRange().GetBaseAddress().GetModule();
    }
    if (module_sp.get() == NULL)
        return false;

    ObjCLanguageRuntime *objc_runtime = m_process->GetObjCLanguageRuntime();
    if (objc_runtime != NULL && objc_runtime->IsModuleObjCLibrary (module_sp))
    {
        return true;
    }

    return false;
}



//----------------------------------------------------------------------
// Dump a Segment to the file handle provided.
//----------------------------------------------------------------------
void
DynamicLoaderMacOSXDYLD::Segment::PutToLog (Log *log, lldb::addr_t slide) const
{
    if (log)
    {
        if (slide == 0)
            log->Printf ("\t\t%16s [0x%16.16llx - 0x%16.16llx)", 
                         name.AsCString(""), 
                         vmaddr + slide, 
                         vmaddr + slide + vmsize);
        else
            log->Printf ("\t\t%16s [0x%16.16llx - 0x%16.16llx) slide = 0x%llx", 
                         name.AsCString(""), 
                         vmaddr + slide, 
                         vmaddr + slide + vmsize, 
                         slide);
    }
}

const DynamicLoaderMacOSXDYLD::Segment *
DynamicLoaderMacOSXDYLD::DYLDImageInfo::FindSegment (const ConstString &name) const
{
    const size_t num_segments = segments.size();
    for (size_t i=0; i<num_segments; ++i)
    {
        if (segments[i].name == name)
            return &segments[i];
    }
    return NULL;
}


//----------------------------------------------------------------------
// Dump an image info structure to the file handle provided.
//----------------------------------------------------------------------
void
DynamicLoaderMacOSXDYLD::DYLDImageInfo::PutToLog (Log *log) const
{
    if (log == NULL)
        return;
    uint8_t *u = (uint8_t *)uuid.GetBytes();

    if (address == LLDB_INVALID_ADDRESS)
    {
        if (u)
        {
            log->Printf("\t                           modtime=0x%8.8llx uuid=%2.2X%2.2X%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X path='%s/%s' (UNLOADED)",
                        mod_date,
                        u[ 0], u[ 1], u[ 2], u[ 3],
                        u[ 4], u[ 5], u[ 6], u[ 7],
                        u[ 8], u[ 9], u[10], u[11],
                        u[12], u[13], u[14], u[15],
                        file_spec.GetDirectory().AsCString(),
                        file_spec.GetFilename().AsCString());
        }
        else
            log->Printf("\t                           modtime=0x%8.8llx path='%s/%s' (UNLOADED)",
                        mod_date,
                        file_spec.GetDirectory().AsCString(),
                        file_spec.GetFilename().AsCString());
    }
    else
    {
        if (u)
        {
            log->Printf("\taddress=0x%16.16llx modtime=0x%8.8llx uuid=%2.2X%2.2X%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X-%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X path='%s/%s'",
                        address,
                        mod_date,
                        u[ 0], u[ 1], u[ 2], u[ 3],
                        u[ 4], u[ 5], u[ 6], u[ 7],
                        u[ 8], u[ 9], u[10], u[11],
                        u[12], u[13], u[14], u[15],
                        file_spec.GetDirectory().AsCString(),
                        file_spec.GetFilename().AsCString());
        }
        else
        {
            log->Printf("\taddress=0x%16.16llx modtime=0x%8.8llx path='%s/%s'",
                        address,
                        mod_date,
                        file_spec.GetDirectory().AsCString(),
                        file_spec.GetFilename().AsCString());

        }
        for (uint32_t i=0; i<segments.size(); ++i)
            segments[i].PutToLog(log, slide);
    }
}

//----------------------------------------------------------------------
// Dump the _dyld_all_image_infos members and all current image infos
// that we have parsed to the file handle provided.
//----------------------------------------------------------------------
void
DynamicLoaderMacOSXDYLD::PutToLog(Log *log) const
{
    if (log == NULL)
        return;

    Mutex::Locker locker(m_mutex);
    log->Printf("dyld_all_image_infos = { version=%d, count=%d, addr=0x%8.8llx, notify=0x%8.8llx }",
                    m_dyld_all_image_infos.version,
                    m_dyld_all_image_infos.dylib_info_count,
                    (uint64_t)m_dyld_all_image_infos.dylib_info_addr,
                    (uint64_t)m_dyld_all_image_infos.notification);
    size_t i;
    const size_t count = m_dyld_image_infos.size();
    if (count > 0)
    {
        log->PutCString("Loaded:");
        for (i = 0; i<count; i++)
            m_dyld_image_infos[i].PutToLog(log);
    }
}

void
DynamicLoaderMacOSXDYLD::PrivateInitialize(Process *process)
{
    DEBUG_PRINTF("DynamicLoaderMacOSXDYLD::%s() process state = %s\n", __FUNCTION__, StateAsCString(m_process->GetState()));
    Clear(true);
    m_process = process;
    m_process->GetTarget().GetSectionLoadList().Clear();
}

bool
DynamicLoaderMacOSXDYLD::SetNotificationBreakpoint ()
{
    DEBUG_PRINTF("DynamicLoaderMacOSXDYLD::%s() process state = %s\n", __FUNCTION__, StateAsCString(m_process->GetState()));
    if (m_break_id == LLDB_INVALID_BREAK_ID)
    {
        if (m_dyld_all_image_infos.notification != LLDB_INVALID_ADDRESS)
        {
            Address so_addr;
            // Set the notification breakpoint and install a breakpoint
            // callback function that will get called each time the
            // breakpoint gets hit. We will use this to track when shared
            // libraries get loaded/unloaded.

            if (m_process->GetTarget().GetSectionLoadList().ResolveLoadAddress(m_dyld_all_image_infos.notification, so_addr))
            {
                Breakpoint *dyld_break = m_process->GetTarget().CreateBreakpoint (so_addr, true).get();
                dyld_break->SetCallback (DynamicLoaderMacOSXDYLD::NotifyBreakpointHit, this, true);
                m_break_id = dyld_break->GetID();
            }
        }
    }
    return m_break_id != LLDB_INVALID_BREAK_ID;
}

//----------------------------------------------------------------------
// Member function that gets called when the process state changes.
//----------------------------------------------------------------------
void
DynamicLoaderMacOSXDYLD::PrivateProcessStateChanged (Process *process, StateType state)
{
    DEBUG_PRINTF("DynamicLoaderMacOSXDYLD::%s(%s)\n", __FUNCTION__, StateAsCString(state));
    switch (state)
    {
    case eStateConnected:
    case eStateAttaching:
    case eStateLaunching:
    case eStateInvalid:
    case eStateUnloaded:
    case eStateExited:
    case eStateDetached:
        Clear(false);
        break;

    case eStateStopped:
        // Keep trying find dyld and set our notification breakpoint each time
        // we stop until we succeed
        if (!DidSetNotificationBreakpoint () && m_process->IsAlive())
        {
            if (NeedToLocateDYLD ())
                LocateDYLD ();

            SetNotificationBreakpoint ();
        }
        break;

    case eStateRunning:
    case eStateStepping:
    case eStateCrashed:
    case eStateSuspended:
        break;

    default:
        break;
    }
}

// This bit in the n_desc field of the mach file means that this is a
// stub that runs arbitrary code to determine the trampoline target.
// We've established a naming convention with the CoreOS folks for the
// equivalent symbols they will use for this (which the objc guys didn't follow...) 
// For now we'll just look for all symbols matching that naming convention...

#define MACH_O_N_SYMBOL_RESOLVER 0x100

ThreadPlanSP
DynamicLoaderMacOSXDYLD::GetStepThroughTrampolinePlan (Thread &thread, bool stop_others)
{
    ThreadPlanSP thread_plan_sp;
    StackFrame *current_frame = thread.GetStackFrameAtIndex(0).get();
    const SymbolContext &current_context = current_frame->GetSymbolContext(eSymbolContextSymbol);
    Symbol *current_symbol = current_context.symbol;
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));

    if (current_symbol != NULL)
    {
        if (current_symbol->IsTrampoline())
        {
            const ConstString &trampoline_name = current_symbol->GetMangled().GetName(Mangled::ePreferMangled);
            
            if (trampoline_name)
            {
                SymbolContextList target_symbols;
                ModuleList &images = thread.GetProcess().GetTarget().GetImages();
                
                images.FindSymbolsWithNameAndType(trampoline_name, eSymbolTypeCode, target_symbols);

                size_t num_original_symbols = target_symbols.GetSize();
                // FIXME: The resolver symbol is only valid in object files.  In binaries it is reused for the
                // shared library slot number.  So we'll have to look this up in the dyld info.
                // For now, just turn this off.
                
                // bool orig_is_resolver = (current_symbol->GetFlags() & MACH_O_N_SYMBOL_RESOLVER) == MACH_O_N_SYMBOL_RESOLVER;
                bool orig_is_resolver = false;
                
                if (num_original_symbols > 0)
                {
                    // We found symbols that look like they are the targets to our symbol.  Now look through the
                    // modules containing our symbols to see if there are any for our symbol.  
                    
                    ModuleList modules_to_search;
                    
                    for (size_t i = 0; i < num_original_symbols; i++)
                    {
                        SymbolContext sc;
                        target_symbols.GetContextAtIndex(i, sc);
                        
                        Module* module_to_add = sc.symbol->CalculateSymbolContextModule();
                        if (module_to_add)
                             modules_to_search.AppendIfNeeded(static_cast<ModuleSP>(module_to_add));
                    }
                    
                    // If the original stub symbol is a resolver, then we don't want to break on the symbol with the
                    // original name, but instead on all the symbols it could resolve to since otherwise we would stop 
                    // in the middle of the resolution...
                    // Note that the stub is not of the resolver type it will point to the equivalent symbol,
                    // not the original name, so in that case we don't need to do anything.
                    
                    if (orig_is_resolver)
                    {
                        target_symbols.Clear();
                        
                        FindEquivalentSymbols (current_symbol, modules_to_search, target_symbols);
                    }
                                            
                    // FIXME - Make the Run to Address take multiple addresses, and
                    // run to any of them.
                    uint32_t num_symbols = target_symbols.GetSize();
                    if (num_symbols > 0)
                    {
                        std::vector<lldb::addr_t>  addresses;
                        addresses.resize (num_symbols);
                        for (uint32_t i = 0; i < num_symbols; i++)
                        {
                            SymbolContext context;
                            AddressRange addr_range;
                            if (target_symbols.GetContextAtIndex(i, context))
                            {
                                context.GetAddressRange (eSymbolContextEverything, 0, false, addr_range);
                                lldb::addr_t load_addr = addr_range.GetBaseAddress().GetLoadAddress(&thread.GetProcess().GetTarget());
                                addresses[i] = load_addr;
                            }
                        }
                        if (addresses.size() > 0)
                            thread_plan_sp.reset (new ThreadPlanRunToAddress (thread, addresses, stop_others));
                        else
                        {
                            if (log)
                                log->Printf ("Couldn't resolve the symbol contexts.");
                        }
                    }
                    else
                    {
                        if (log)
                        {
                            log->Printf ("Found a resolver stub for: \"%s\" but could not find any symbols it resolves to.", 
                                         trampoline_name.AsCString());
                        }
                    }
                }
                else
                {
                    if (log)
                    {
                        log->Printf ("Could not find symbol for trampoline target: \"%s\"", trampoline_name.AsCString());
                    }
                }
            }
        }
    }
    else
    {
        if (log)
            log->Printf ("Could not find symbol for step through.");
    }

    return thread_plan_sp;
}

size_t
DynamicLoaderMacOSXDYLD::FindEquivalentSymbols (lldb_private::Symbol *original_symbol, 
                                               lldb_private::ModuleList &images, 
                                               lldb_private::SymbolContextList &equivalent_symbols)
{
    const ConstString &trampoline_name = original_symbol->GetMangled().GetName(Mangled::ePreferMangled);
    if (!trampoline_name)
        return 0;
        
    size_t initial_size = equivalent_symbols.GetSize();
    
    static const char *resolver_name_regex = "(_gc|_non_gc|\\$[A-Z0-9]+)$";
    std::string equivalent_regex_buf("^");
    equivalent_regex_buf.append (trampoline_name.GetCString());
    equivalent_regex_buf.append (resolver_name_regex);

    RegularExpression equivalent_name_regex (equivalent_regex_buf.c_str());
    const bool append = true;
    images.FindSymbolsMatchingRegExAndType (equivalent_name_regex, eSymbolTypeCode, equivalent_symbols, append);
    
    return equivalent_symbols.GetSize() - initial_size;
}

Error
DynamicLoaderMacOSXDYLD::CanLoadImage ()
{
    Error error;
    // In order for us to tell if we can load a shared library we verify that
    // the dylib_info_addr isn't zero (which means no shared libraries have
    // been set yet, or dyld is currently mucking with the shared library list).
    if (ReadAllImageInfosStructure ())
    {
        // TODO: also check the _dyld_global_lock_held variable in libSystem.B.dylib?
        // TODO: check the malloc lock?
        // TODO: check the objective C lock?
        if (m_dyld_all_image_infos.dylib_info_addr != 0)
            return error; // Success
    }

    error.SetErrorString("unsafe to load or unload shared libraries");
    return error;
}

void
DynamicLoaderMacOSXDYLD::Initialize()
{
    PluginManager::RegisterPlugin (GetPluginNameStatic(),
                                   GetPluginDescriptionStatic(),
                                   CreateInstance);
}

void
DynamicLoaderMacOSXDYLD::Terminate()
{
    PluginManager::UnregisterPlugin (CreateInstance);
}


const char *
DynamicLoaderMacOSXDYLD::GetPluginNameStatic()
{
    return "dynamic-loader.macosx-dyld";
}

const char *
DynamicLoaderMacOSXDYLD::GetPluginDescriptionStatic()
{
    return "Dynamic loader plug-in that watches for shared library loads/unloads in MacOSX user processes.";
}


//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
const char *
DynamicLoaderMacOSXDYLD::GetPluginName()
{
    return "DynamicLoaderMacOSXDYLD";
}

const char *
DynamicLoaderMacOSXDYLD::GetShortPluginName()
{
    return GetPluginNameStatic();
}

uint32_t
DynamicLoaderMacOSXDYLD::GetPluginVersion()
{
    return 1;
}

