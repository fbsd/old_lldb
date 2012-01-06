//===-- ModuleList.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ModuleList.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/Log.h"
#include "lldb/Core/Module.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/Symbols.h"
#include "lldb/Symbol/ClangNamespaceDecl.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/VariableList.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// ModuleList constructor
//----------------------------------------------------------------------
ModuleList::ModuleList() :
    m_modules(),
    m_modules_mutex (Mutex::eMutexTypeRecursive)
{
}

//----------------------------------------------------------------------
// Copy constructor
//----------------------------------------------------------------------
ModuleList::ModuleList(const ModuleList& rhs) :
    m_modules(rhs.m_modules)
{
}

//----------------------------------------------------------------------
// Assignment operator
//----------------------------------------------------------------------
const ModuleList&
ModuleList::operator= (const ModuleList& rhs)
{
    if (this != &rhs)
    {
        Mutex::Locker locker(m_modules_mutex);
        m_modules = rhs.m_modules;
    }
    return *this;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
ModuleList::~ModuleList()
{
}

void
ModuleList::Append (const ModuleSP &module_sp)
{
    if (module_sp)
    {
        Mutex::Locker locker(m_modules_mutex);
        m_modules.push_back(module_sp);
    }
}

bool
ModuleList::AppendIfNeeded (const ModuleSP &module_sp)
{
    if (module_sp)
    {
        Mutex::Locker locker(m_modules_mutex);
        collection::iterator pos, end = m_modules.end();
        for (pos = m_modules.begin(); pos != end; ++pos)
        {
            if (pos->get() == module_sp.get())
                return false; // Already in the list
        }
        // Only push module_sp on the list if it wasn't already in there.
        m_modules.push_back(module_sp);
        return true;
    }
    return false;
}

bool
ModuleList::Remove (const ModuleSP &module_sp)
{
    if (module_sp)
    {
        Mutex::Locker locker(m_modules_mutex);
        collection::iterator pos, end = m_modules.end();
        for (pos = m_modules.begin(); pos != end; ++pos)
        {
            if (pos->get() == module_sp.get())
            {
                m_modules.erase (pos);
                return true;
            }
        }
    }
    return false;
}


size_t
ModuleList::RemoveOrphans ()
{
    Mutex::Locker locker(m_modules_mutex);
    collection::iterator pos = m_modules.begin();
    size_t remove_count = 0;
    while (pos != m_modules.end())
    {
        if (pos->unique())
        {
            pos = m_modules.erase (pos);
            ++remove_count;
        }
        else
        {
            ++pos;
        }
    }
    return remove_count;
}

size_t
ModuleList::Remove (ModuleList &module_list)
{
    Mutex::Locker locker(m_modules_mutex);
    size_t num_removed = 0;
    collection::iterator pos, end = module_list.m_modules.end();
    for (pos = module_list.m_modules.begin(); pos != end; ++pos)
    {
        if (Remove (*pos))
            ++num_removed;
    }
    return num_removed;
}



void
ModuleList::Clear()
{
    Mutex::Locker locker(m_modules_mutex);
    m_modules.clear();
}

Module*
ModuleList::GetModulePointerAtIndex (uint32_t idx) const
{
    Mutex::Locker locker(m_modules_mutex);
    if (idx < m_modules.size())
        return m_modules[idx].get();
    return NULL;
}

ModuleSP
ModuleList::GetModuleAtIndex(uint32_t idx)
{
    Mutex::Locker locker(m_modules_mutex);
    ModuleSP module_sp;
    if (idx < m_modules.size())
        module_sp = m_modules[idx];
    return module_sp;
}

uint32_t
ModuleList::FindFunctions (const ConstString &name, 
                           uint32_t name_type_mask, 
                           bool include_symbols,
                           bool append, 
                           SymbolContextList &sc_list)
{
    if (!append)
        sc_list.Clear();
    
    Mutex::Locker locker(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos)
    {
        (*pos)->FindFunctions (name, NULL, name_type_mask, include_symbols, true, sc_list);
    }
    
    return sc_list.GetSize();
}

uint32_t
ModuleList::FindCompileUnits (const FileSpec &path, 
                              bool append, 
                              SymbolContextList &sc_list)
{
    if (!append)
        sc_list.Clear();
    
    Mutex::Locker locker(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos)
    {
        (*pos)->FindCompileUnits (path, true, sc_list);
    }
    
    return sc_list.GetSize();
}

uint32_t
ModuleList::FindGlobalVariables (const ConstString &name, 
                                 bool append, 
                                 uint32_t max_matches, 
                                 VariableList& variable_list)
{
    size_t initial_size = variable_list.GetSize();
    Mutex::Locker locker(m_modules_mutex);
    collection::iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos)
    {
        (*pos)->FindGlobalVariables (name, NULL, append, max_matches, variable_list);
    }
    return variable_list.GetSize() - initial_size;
}


uint32_t
ModuleList::FindGlobalVariables (const RegularExpression& regex, 
                                 bool append, 
                                 uint32_t max_matches, 
                                 VariableList& variable_list)
{
    size_t initial_size = variable_list.GetSize();
    Mutex::Locker locker(m_modules_mutex);
    collection::iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos)
    {
        (*pos)->FindGlobalVariables (regex, append, max_matches, variable_list);
    }
    return variable_list.GetSize() - initial_size;
}


size_t
ModuleList::FindSymbolsWithNameAndType (const ConstString &name, 
                                        SymbolType symbol_type, 
                                        SymbolContextList &sc_list,
                                        bool append)
{
    Mutex::Locker locker(m_modules_mutex);
    if (!append)
        sc_list.Clear();
    size_t initial_size = sc_list.GetSize();
    
    collection::iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos)
        (*pos)->FindSymbolsWithNameAndType (name, symbol_type, sc_list);
    return sc_list.GetSize() - initial_size;
}

    size_t
ModuleList::FindSymbolsMatchingRegExAndType (const RegularExpression &regex, 
                                             lldb::SymbolType symbol_type, 
                                             SymbolContextList &sc_list,
                                             bool append)
{
    Mutex::Locker locker(m_modules_mutex);
    if (!append)
        sc_list.Clear();
    size_t initial_size = sc_list.GetSize();
    
    collection::iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos)
        (*pos)->FindSymbolsMatchingRegExAndType (regex, symbol_type, sc_list);
    return sc_list.GetSize() - initial_size;
}

class ModuleMatches
{
public:
    //--------------------------------------------------------------
    /// Construct with the user ID to look for.
    //--------------------------------------------------------------
    ModuleMatches (const FileSpec *file_spec_ptr,
                   const ArchSpec *arch_ptr,
                   const lldb_private::UUID *uuid_ptr,
                   const ConstString *object_name,
                   bool file_spec_is_platform) :
        m_file_spec_ptr (file_spec_ptr),
        m_arch_ptr (arch_ptr),
        m_uuid_ptr (uuid_ptr),
        m_object_name (object_name),
        m_file_spec_compare_basename_only (false),
        m_file_spec_is_platform (file_spec_is_platform)
    {
        if (file_spec_ptr)
            m_file_spec_compare_basename_only = file_spec_ptr->GetDirectory();
    }

    
    //--------------------------------------------------------------
    /// Unary predicate function object callback.
    //--------------------------------------------------------------
    bool
    operator () (const ModuleSP& module_sp) const
    {
        if (m_file_spec_ptr)
        {
            if (m_file_spec_is_platform)
            {
                if (!FileSpec::Equal (*m_file_spec_ptr, 
                                      module_sp->GetPlatformFileSpec(), 
                                      m_file_spec_compare_basename_only))
                    return false;
        
            }
            else
            {
                if (!FileSpec::Equal (*m_file_spec_ptr, 
                                      module_sp->GetFileSpec(), 
                                      m_file_spec_compare_basename_only))
                    return false;
            }
        }

        if (m_arch_ptr && m_arch_ptr->IsValid())
        {
            if (module_sp->GetArchitecture() != *m_arch_ptr)
                return false;
        }

        if (m_uuid_ptr && m_uuid_ptr->IsValid())
        {
            if (module_sp->GetUUID() != *m_uuid_ptr)
                return false;
        }

        if (m_object_name)
        {
            if (module_sp->GetObjectName() != *m_object_name)
                return false;
        }
        return true;
    }

private:
    //--------------------------------------------------------------
    // Member variables.
    //--------------------------------------------------------------
    const FileSpec *            m_file_spec_ptr;
    const ArchSpec *            m_arch_ptr;
    const lldb_private::UUID *  m_uuid_ptr;
    const ConstString *         m_object_name;
    bool                        m_file_spec_compare_basename_only;
    bool                        m_file_spec_is_platform;
};

size_t
ModuleList::FindModules
(
    const FileSpec *file_spec_ptr,
    const ArchSpec *arch_ptr,
    const lldb_private::UUID *uuid_ptr,
    const ConstString *object_name,
    ModuleList& matching_module_list
) const
{
    size_t existing_matches = matching_module_list.GetSize();
    ModuleMatches matcher (file_spec_ptr, arch_ptr, uuid_ptr, object_name, false);

    Mutex::Locker locker(m_modules_mutex);
    collection::const_iterator end = m_modules.end();
    collection::const_iterator pos;

    for (pos = std::find_if (m_modules.begin(), end, matcher);
         pos != end;
         pos = std::find_if (++pos, end, matcher))
    {
        ModuleSP module_sp(*pos);
        matching_module_list.Append(module_sp);
    }
    return matching_module_list.GetSize() - existing_matches;
}

ModuleSP
ModuleList::FindModule (const Module *module_ptr)
{
    ModuleSP module_sp;

    // Scope for "locker"
    {
        Mutex::Locker locker(m_modules_mutex);
        collection::const_iterator pos, end = m_modules.end();

        for (pos = m_modules.begin(); pos != end; ++pos)
        {
            if ((*pos).get() == module_ptr)
            {
                module_sp = (*pos);
                break;
            }
        }
    }
    return module_sp;

}

ModuleSP
ModuleList::FindModule (const UUID &uuid)
{
    ModuleSP module_sp;
    
    if (uuid.IsValid())
    {
        Mutex::Locker locker(m_modules_mutex);
        collection::const_iterator pos, end = m_modules.end();
        
        for (pos = m_modules.begin(); pos != end; ++pos)
        {
            if ((*pos)->GetUUID() == uuid)
            {
                module_sp = (*pos);
                break;
            }
        }
    }
    return module_sp;
}


uint32_t
ModuleList::FindTypes_Impl (const SymbolContext& sc, const ConstString &name, bool append, uint32_t max_matches, TypeList& types)
{
    Mutex::Locker locker(m_modules_mutex);
    
    if (!append)
        types.Clear();

    uint32_t total_matches = 0;
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos)
    {
        if (sc.module_sp.get() == NULL || sc.module_sp.get() == (*pos).get())
            total_matches += (*pos)->FindTypes (sc, name, NULL, true, max_matches, types);

        if (total_matches >= max_matches)
            break;
    }
    return total_matches;
}

// depending on implementation details, type lookup might fail because of
// embedded spurious namespace:: prefixes. this call strips them, paying
// attention to the fact that a type might have namespace'd type names as
// arguments to templates, and those must not be stripped off
static const char*
StripTypeName(const char* name_cstr)
{
    const char* skip_namespace = strstr(name_cstr, "::");
    const char* template_arg_char = strchr(name_cstr, '<');
    while (skip_namespace != NULL)
    {
        if (template_arg_char != NULL &&
            skip_namespace > template_arg_char) // but namespace'd template arguments are still good to go
            break;
        name_cstr = skip_namespace+2;
        skip_namespace = strstr(name_cstr, "::");
    }
    return name_cstr;
}

uint32_t
ModuleList::FindTypes (const SymbolContext& sc, const ConstString &name, bool append, uint32_t max_matches, TypeList& types)
{
    uint32_t retval = FindTypes_Impl(sc, name, append, max_matches, types);
    
    if (retval == 0)
    {
        const char *stripped = StripTypeName(name.GetCString());
        return FindTypes_Impl(sc, ConstString(stripped), append, max_matches, types);
    }
    else
        return retval;
    
}

ModuleSP
ModuleList::FindFirstModuleForFileSpec (const FileSpec &file_spec, 
                                        const ArchSpec *arch_ptr,
                                        const ConstString *object_name)
{
    ModuleSP module_sp;
    ModuleMatches matcher (&file_spec, 
                           arch_ptr, 
                           NULL, 
                           object_name, 
                           false);

    // Scope for "locker"
    {
        Mutex::Locker locker(m_modules_mutex);
        collection::const_iterator end = m_modules.end();
        collection::const_iterator pos = m_modules.begin();

        pos = std::find_if (pos, end, matcher);
        if (pos != end)
            module_sp = (*pos);
    }
    return module_sp;

}

ModuleSP
ModuleList::FindFirstModuleForPlatormFileSpec (const FileSpec &file_spec, 
                                               const ArchSpec *arch_ptr,
                                               const ConstString *object_name)
{
    ModuleSP module_sp;
    ModuleMatches matcher (&file_spec, 
                           arch_ptr, 
                           NULL, 
                           object_name, 
                           true);
    
    // Scope for "locker"
    {
        Mutex::Locker locker(m_modules_mutex);
        collection::const_iterator end = m_modules.end();
        collection::const_iterator pos = m_modules.begin();
        
        pos = std::find_if (pos, end, matcher);
        if (pos != end)
            module_sp = (*pos);
    }
    return module_sp;
    
}


size_t
ModuleList::GetSize() const
{
    size_t size = 0;
    {
        Mutex::Locker locker(m_modules_mutex);
        size = m_modules.size();
    }
    return size;
}


void
ModuleList::Dump(Stream *s) const
{
//  s.Printf("%.*p: ", (int)sizeof(void*) * 2, this);
//  s.Indent();
//  s << "ModuleList\n";

    Mutex::Locker locker(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos)
    {
        (*pos)->Dump(s);
    }
}

void
ModuleList::LogUUIDAndPaths (LogSP &log_sp, const char *prefix_cstr)
{
    if (log_sp)
    {   
        Mutex::Locker locker(m_modules_mutex);
        char uuid_cstr[256];
        collection::const_iterator pos, begin = m_modules.begin(), end = m_modules.end();
        for (pos = begin; pos != end; ++pos)
        {
            Module *module = pos->get();
            module->GetUUID().GetAsCString (uuid_cstr, sizeof(uuid_cstr));
            const FileSpec &module_file_spec = module->GetFileSpec();
            log_sp->Printf ("%s[%u] %s (%s) \"%s/%s\"", 
                            prefix_cstr ? prefix_cstr : "",
                            (uint32_t)std::distance (begin, pos),
                            uuid_cstr,
                            module->GetArchitecture().GetArchitectureName(),
                            module_file_spec.GetDirectory().GetCString(),
                            module_file_spec.GetFilename().GetCString());
        }
    }
}

bool
ModuleList::ResolveFileAddress (lldb::addr_t vm_addr, Address& so_addr)
{
    Mutex::Locker locker(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos)
    {
        if ((*pos)->ResolveFileAddress (vm_addr, so_addr))
            return true;
    }

    return false;
}

uint32_t
ModuleList::ResolveSymbolContextForAddress (const Address& so_addr, uint32_t resolve_scope, SymbolContext& sc)
{
    // The address is already section offset so it has a module
    uint32_t resolved_flags = 0;
    Module *module = so_addr.GetModule();
    if (module)
    {
        resolved_flags = module->ResolveSymbolContextForAddress (so_addr,
                                                                 resolve_scope,
                                                                 sc);
    }
    else
    {
        Mutex::Locker locker(m_modules_mutex);
        collection::const_iterator pos, end = m_modules.end();
        for (pos = m_modules.begin(); pos != end; ++pos)
        {
            resolved_flags = (*pos)->ResolveSymbolContextForAddress (so_addr,
                                                                     resolve_scope,
                                                                     sc);
            if (resolved_flags != 0)
                break;
        }
    }

    return resolved_flags;
}

uint32_t
ModuleList::ResolveSymbolContextForFilePath 
(
    const char *file_path, 
    uint32_t line, 
    bool check_inlines, 
    uint32_t resolve_scope, 
    SymbolContextList& sc_list
)
{
    FileSpec file_spec(file_path, false);
    return ResolveSymbolContextsForFileSpec (file_spec, line, check_inlines, resolve_scope, sc_list);
}

uint32_t
ModuleList::ResolveSymbolContextsForFileSpec (const FileSpec &file_spec, uint32_t line, bool check_inlines, uint32_t resolve_scope, SymbolContextList& sc_list)
{
    Mutex::Locker locker(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos)
    {
        (*pos)->ResolveSymbolContextsForFileSpec (file_spec, line, check_inlines, resolve_scope, sc_list);
    }

    return sc_list.GetSize();
}

uint32_t
ModuleList::GetIndexForModule (const Module *module) const
{
    if (module)
    {
        Mutex::Locker locker(m_modules_mutex);
        collection::const_iterator pos;
        collection::const_iterator begin = m_modules.begin();
        collection::const_iterator end = m_modules.end();
        for (pos = begin; pos != end; ++pos)
        {
            if ((*pos).get() == module)
                return std::distance (begin, pos);
        }
    }
    return LLDB_INVALID_INDEX32;
}

static ModuleList &
GetSharedModuleList ()
{
    static ModuleList g_shared_module_list;
    return g_shared_module_list;
}

const lldb::ModuleSP
ModuleList::GetModuleSP (const Module *module_ptr)
{
    lldb::ModuleSP module_sp;
    if (module_ptr)
    {
        ModuleList &shared_module_list = GetSharedModuleList ();
        module_sp = shared_module_list.FindModule (module_ptr);
        if (module_sp.get() == NULL)
        {
            char uuid_cstr[256];
            const_cast<Module *>(module_ptr)->GetUUID().GetAsCString (uuid_cstr, sizeof(uuid_cstr));
            const FileSpec &module_file_spec = module_ptr->GetFileSpec();
            Host::SystemLog (Host::eSystemLogWarning, 
                             "warning: module not in shared module list: %s (%s) \"%s/%s\"\n", 
                             uuid_cstr,
                             module_ptr->GetArchitecture().GetArchitectureName(),
                             module_file_spec.GetDirectory().GetCString(),
                             module_file_spec.GetFilename().GetCString());
        }
    }
    return module_sp;
}

size_t
ModuleList::FindSharedModules 
(
    const FileSpec& in_file_spec,
    const ArchSpec& arch,
    const lldb_private::UUID *uuid_ptr,
    const ConstString *object_name_ptr,
    ModuleList &matching_module_list
)
{
    ModuleList &shared_module_list = GetSharedModuleList ();
    return shared_module_list.FindModules (&in_file_spec, &arch, uuid_ptr, object_name_ptr, matching_module_list);
}

uint32_t
ModuleList::RemoveOrphanSharedModules ()
{
    return GetSharedModuleList ().RemoveOrphans();    
}
//#define ENABLE_MODULE_SP_LOGGING
#if defined (ENABLE_MODULE_SP_LOGGING)
#include "lldb/Core/StreamFile.h"
#include "lldb/Host/Host.h"
static void 
ModuleSharedPtrLogger(void* p, const ModuleSP& sp, bool will_decrement)
{
    if (sp.get())
    {
        const char *module_basename = sp->GetFileSpec().GetFilename().GetCString();
        // If "p" is set, then it is the basename of a module to watch for. This
        // basename MUST be uniqued first by getting it from a ConstString or this
        // won't work.
        if (p && p != module_basename)
        {
            return;
        }
        long use_count = sp.use_count();
        if (will_decrement)
            --use_count;

        printf("\nModuleSP(%p): %c %p {%lu} %s/%s\n", &sp, will_decrement ? '-' : '+', sp.get(), use_count, sp->GetFileSpec().GetDirectory().GetCString(), module_basename);
        StreamFile stdout_strm(stdout, false);
        Host::Backtrace (stdout_strm, 512);
    }
}
#endif

Error
ModuleList::GetSharedModule
(
    const FileSpec& in_file_spec,
    const ArchSpec& arch,
    const lldb_private::UUID *uuid_ptr,
    const ConstString *object_name_ptr,
    off_t object_offset,
    ModuleSP &module_sp,
    ModuleSP *old_module_sp_ptr,
    bool *did_create_ptr,
    bool always_create
)
{
    ModuleList &shared_module_list = GetSharedModuleList ();
    Mutex::Locker locker(shared_module_list.m_modules_mutex);
    char path[PATH_MAX];
    char uuid_cstr[64];

    Error error;

    module_sp.reset();

    if (did_create_ptr)
        *did_create_ptr = false;
    if (old_module_sp_ptr)
        old_module_sp_ptr->reset();


    // First just try and get the file where it purports to be (path in
    // in_file_spec), then check and uuid.

    if (in_file_spec)
    {
        // Make sure no one else can try and get or create a module while this
        // function is actively working on it by doing an extra lock on the
        // global mutex list.
        if (always_create == false)
        {
            ModuleList matching_module_list;
            const size_t num_matching_modules = shared_module_list.FindModules (&in_file_spec, &arch, NULL, object_name_ptr, matching_module_list);
            if (num_matching_modules > 0)
            {
                for (uint32_t module_idx = 0; module_idx < num_matching_modules; ++module_idx)
                {
                    module_sp = matching_module_list.GetModuleAtIndex(module_idx);
                    if (uuid_ptr && uuid_ptr->IsValid())
                    {
                        // We found the module we were looking for.
                        if (module_sp->GetUUID() == *uuid_ptr)
                            return error;
                    }
                    else
                    {
                        // If we didn't have a UUID in mind when looking for the object file,
                        // then we should make sure the modification time hasn't changed!
                        TimeValue file_spec_mod_time(in_file_spec.GetModificationTime());
                        if (file_spec_mod_time.IsValid())
                        {
                            if (file_spec_mod_time == module_sp->GetModificationTime())
                                return error;
                        }
                    }
                    if (old_module_sp_ptr && !old_module_sp_ptr->get())
                        *old_module_sp_ptr = module_sp;
                    shared_module_list.Remove (module_sp);
                    module_sp.reset();
                }
            }
        }

        if (module_sp)
            return error;
        else
        {
#if defined ENABLE_MODULE_SP_LOGGING
            ModuleSP logging_module_sp (new Module (in_file_spec, arch, object_name_ptr, object_offset), ModuleSharedPtrLogger, (void *)ConstString("a.out").GetCString());
            module_sp = logging_module_sp;
#else
            module_sp.reset (new Module (in_file_spec, arch, object_name_ptr, object_offset));
#endif
            // Make sure there are a module and an object file since we can specify
            // a valid file path with an architecture that might not be in that file.
            // By getting the object file we can guarantee that the architecture matches
            if (module_sp && module_sp->GetObjectFile())
            {
                // If we get in here we got the correct arch, now we just need
                // to verify the UUID if one was given
                if (uuid_ptr && *uuid_ptr != module_sp->GetUUID())
                    module_sp.reset();
                else
                {
                    if (did_create_ptr)
                        *did_create_ptr = true;
                    
                    shared_module_list.Append(module_sp);
                    return error;
                }
            }
        }
    }

    // Either the file didn't exist where at the path, or no path was given, so
    // we now have to use more extreme measures to try and find the appropriate
    // module.

    // Fixup the incoming path in case the path points to a valid file, yet
    // the arch or UUID (if one was passed in) don't match.
    FileSpec file_spec = Symbols::LocateExecutableObjectFile (in_file_spec ? &in_file_spec : NULL, 
                                                              arch.IsValid() ? &arch : NULL, 
                                                              uuid_ptr);

    // Don't look for the file if it appears to be the same one we already
    // checked for above...
    if (file_spec != in_file_spec)
    {
        if (!file_spec.Exists())
        {
            file_spec.GetPath(path, sizeof(path));
            if (file_spec.Exists())
            {
                if (uuid_ptr && uuid_ptr->IsValid())
                    uuid_ptr->GetAsCString(uuid_cstr, sizeof (uuid_cstr));
                else
                    uuid_cstr[0] = '\0';


                if (arch.IsValid())
                {
                    if (uuid_cstr[0])
                        error.SetErrorStringWithFormat("'%s' does not contain the %s architecture and UUID %s", path, arch.GetArchitectureName(), uuid_cstr);
                    else
                        error.SetErrorStringWithFormat("'%s' does not contain the %s architecture.", path, arch.GetArchitectureName());
                }
            }
            else
            {
                error.SetErrorStringWithFormat("'%s' does not exist", path);
            }
            return error;
        }


        // Make sure no one else can try and get or create a module while this
        // function is actively working on it by doing an extra lock on the
        // global mutex list.
        ModuleList matching_module_list;
        if (shared_module_list.FindModules (&file_spec, &arch, uuid_ptr, object_name_ptr, matching_module_list) > 0)
        {
            module_sp = matching_module_list.GetModuleAtIndex(0);

            // If we didn't have a UUID in mind when looking for the object file,
            // then we should make sure the modification time hasn't changed!
            if (uuid_ptr == NULL)
            {
                TimeValue file_spec_mod_time(file_spec.GetModificationTime());
                if (file_spec_mod_time.IsValid())
                {
                    if (file_spec_mod_time != module_sp->GetModificationTime())
                    {
                        if (old_module_sp_ptr)
                            *old_module_sp_ptr = module_sp;
                        shared_module_list.Remove (module_sp);
                        module_sp.reset();
                    }
                }
            }
        }

        if (module_sp.get() == NULL)
        {
#if defined ENABLE_MODULE_SP_LOGGING
            ModuleSP logging_module_sp (new Module (file_spec, arch, object_name_ptr, object_offset), ModuleSharedPtrLogger, 0);
            module_sp = logging_module_sp;
#else
            module_sp.reset (new Module (file_spec, arch, object_name_ptr, object_offset));
#endif
            // Make sure there are a module and an object file since we can specify
            // a valid file path with an architecture that might not be in that file.
            // By getting the object file we can guarantee that the architecture matches
            if (module_sp && module_sp->GetObjectFile())
            {
                if (did_create_ptr)
                    *did_create_ptr = true;

                shared_module_list.Append(module_sp);
            }
            else
            {
                file_spec.GetPath(path, sizeof(path));

                if (file_spec)
                {
                    if (arch.IsValid())
                        error.SetErrorStringWithFormat("unable to open %s architecture in '%s'", arch.GetArchitectureName(), path);
                    else
                        error.SetErrorStringWithFormat("unable to open '%s'", path);
                }
                else
                {
                    if (uuid_ptr && uuid_ptr->IsValid())
                        uuid_ptr->GetAsCString(uuid_cstr, sizeof (uuid_cstr));
                    else
                        uuid_cstr[0] = '\0';

                    if (uuid_cstr[0])
                        error.SetErrorStringWithFormat("cannot locate a module for UUID '%s'", uuid_cstr);
                    else
                        error.SetErrorStringWithFormat("cannot locate a module");
                }
            }
        }
    }

    return error;
}

bool
ModuleList::RemoveSharedModule (lldb::ModuleSP &module_sp)
{
    return GetSharedModuleList ().Remove (module_sp);
}


