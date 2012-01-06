//===-- Module.h ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Module_h_
#define liblldb_Module_h_

#include "lldb/Core/ArchSpec.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/UUID.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Host/Mutex.h"
#include "lldb/Host/TimeValue.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Symbol/TypeList.h"

//----------------------------------------------------------------------
/// @class Module Module.h "lldb/Core/Module.h"
/// @brief A class that describes an executable image and its associated
///        object and symbol files.
///
/// The module is designed to be able to select a single slice of an
/// executable image as it would appear on disk and during program
/// execution.
///
/// Modules control when and if information is parsed according to which
/// accessors are called. For example the object file (ObjectFile)
/// representation will only be parsed if the object file is requested
/// using the Module::GetObjectFile() is called. The debug symbols
/// will only be parsed if the symbol vendor (SymbolVendor) is
/// requested using the Module::GetSymbolVendor() is called.
///
/// The module will parse more detailed information as more queries are
/// made.
//----------------------------------------------------------------------
namespace lldb_private {

class Module :
    public ReferenceCountedBaseVirtual<Module>,
    public SymbolContextScope
{
public:
    friend class ModuleList;
    friend bool ObjectFile::SetModulesArchitecture (const ArchSpec &new_arch);

	// Static functions that can track the lifetime of moodule objects.
	// This is handy because we might have Module objects that are in
	// shared pointers that aren't in the global module list (from 
	// ModuleList). If this is the case we need to know about it.
    // The modules in the global list maintained by these functions
    // can be viewed using the "target modules list" command using the
    // "--global" (-g for short).
    static size_t
    GetNumberAllocatedModules ();
    
    static Module *
    GetAllocatedModuleAtIndex (size_t idx);

    static Mutex &
    GetAllocationModuleCollectionMutex();

    //------------------------------------------------------------------
    /// Construct with file specification and architecture.
    ///
    /// Clients that wish to share modules with other targets should
    /// use ModuleList::GetSharedModule().
    ///
    /// @param[in] file_spec
    ///     The file specification for the on disk repesentation of
    ///     this executable image.
    ///
    /// @param[in] arch
    ///     The architecture to set as the current architecture in
    ///     this module.
    ///
    /// @param[in] object_name
    ///     The name of an object in a module used to extract a module
    ///     within a module (.a files and modules that contain multiple
    ///     architectures).
    ///
    /// @param[in] object_offset
    ///     The offset within an existing module used to extract a
    ///     module within a module (.a files and modules that contain
    ///     multiple architectures).
    //------------------------------------------------------------------
    Module (const FileSpec& file_spec,
            const ArchSpec& arch,
            const ConstString *object_name = NULL,
            off_t object_offset = 0);

    //------------------------------------------------------------------
    /// Destructor.
    //------------------------------------------------------------------
    virtual 
    ~Module ();

    //------------------------------------------------------------------
    /// @copydoc SymbolContextScope::CalculateSymbolContext(SymbolContext*)
    ///
    /// @see SymbolContextScope
    //------------------------------------------------------------------
    virtual void
    CalculateSymbolContext (SymbolContext* sc);

    virtual Module *
    CalculateSymbolContextModule ();

    void
    GetDescription (Stream *s,
                    lldb::DescriptionLevel level = lldb::eDescriptionLevelFull);

    //------------------------------------------------------------------
    /// Dump a description of this object to a Stream.
    ///
    /// Dump a description of the contents of this object to the
    /// supplied stream \a s. The dumped content will be only what has
    /// been loaded or parsed up to this point at which this function
    /// is called, so this is a good way to see what has been parsed
    /// in a module.
    ///
    /// @param[in] s
    ///     The stream to which to dump the object descripton.
    //------------------------------------------------------------------
    void
    Dump (Stream *s);

    //------------------------------------------------------------------
    /// @copydoc SymbolContextScope::DumpSymbolContext(Stream*)
    ///
    /// @see SymbolContextScope
    //------------------------------------------------------------------
    virtual void
    DumpSymbolContext (Stream *s);

    //------------------------------------------------------------------
    /// Find a symbol in the object files symbol table.
    ///
    /// @param[in] name
    ///     The name of the symbol that we are looking for.
    ///
    /// @param[in] symbol_type
    ///     If set to eSymbolTypeAny, find a symbol of any type that
    ///     has a name that matches \a name. If set to any other valid
    ///     SymbolType enumeration value, then search only for
    ///     symbols that match \a symbol_type.
    ///
    /// @return
    ///     Returns a valid symbol pointer if a symbol was found,
    ///     NULL otherwise.
    //------------------------------------------------------------------
    const Symbol *
    FindFirstSymbolWithNameAndType (const ConstString &name, 
                                    lldb::SymbolType symbol_type = lldb::eSymbolTypeAny);

    size_t
    FindSymbolsWithNameAndType (const ConstString &name,
                                lldb::SymbolType symbol_type, 
                                SymbolContextList &sc_list);

    size_t
    FindSymbolsMatchingRegExAndType (const RegularExpression &regex, 
                                     lldb::SymbolType symbol_type, 
                                     SymbolContextList &sc_list);

    //------------------------------------------------------------------
    /// Find compile units by partial or full path.
    ///
    /// Finds all compile units that match \a path in all of the modules
    /// and returns the results in \a sc_list.
    ///
    /// @param[in] path
    ///     The name of the function we are looking for.
    ///
    /// @param[in] append
    ///     If \b true, then append any compile units that were found
    ///     to \a sc_list. If \b false, then the \a sc_list is cleared
    ///     and the contents of \a sc_list are replaced.
    ///
    /// @param[out] sc_list
    ///     A symbol context list that gets filled in with all of the
    ///     matches.
    ///
    /// @return
    ///     The number of matches added to \a sc_list.
    //------------------------------------------------------------------
    uint32_t
    FindCompileUnits (const FileSpec &path,
                      bool append,
                      SymbolContextList &sc_list);
    

    //------------------------------------------------------------------
    /// Find functions by name.
    ///
    /// If the function is an inlined function, it will have a block,
    /// representing the inlined function, and the function will be the
    /// containing function.  If it is not inlined, then the block will 
    /// be NULL.
    ///
    /// @param[in] name
    ///     The name of the compile unit we are looking for.
    ///
    /// @param[in] namespace_decl
    ///     If valid, a namespace to search in.
    ///
    /// @param[in] name_type_mask
    ///     A bit mask of bits that indicate what kind of names should
    ///     be used when doing the lookup. Bits include fully qualified
    ///     names, base names, C++ methods, or ObjC selectors. 
    ///     See FunctionNameType for more details.
    ///
    /// @param[in] append
    ///     If \b true, any matches will be appended to \a sc_list, else
    ///     matches replace the contents of \a sc_list.
    ///
    /// @param[out] sc_list
    ///     A symbol context list that gets filled in with all of the
    ///     matches.
    ///
    /// @return
    ///     The number of matches added to \a sc_list.
    //------------------------------------------------------------------
    uint32_t
    FindFunctions (const ConstString &name,
                   const ClangNamespaceDecl *namespace_decl,
                   uint32_t name_type_mask, 
                   bool symbols_ok, 
                   bool append, 
                   SymbolContextList& sc_list);

    //------------------------------------------------------------------
    /// Find functions by name.
    ///
    /// If the function is an inlined function, it will have a block,
    /// representing the inlined function, and the function will be the
    /// containing function.  If it is not inlined, then the block will 
    /// be NULL.
    ///
    /// @param[in] regex
    ///     A regular expression to use when matching the name.
    ///
    /// @param[in] append
    ///     If \b true, any matches will be appended to \a sc_list, else
    ///     matches replace the contents of \a sc_list.
    ///
    /// @param[out] sc_list
    ///     A symbol context list that gets filled in with all of the
    ///     matches.
    ///
    /// @return
    ///     The number of matches added to \a sc_list.
    //------------------------------------------------------------------
    uint32_t
    FindFunctions (const RegularExpression& regex, 
                   bool symbols_ok, 
                   bool append, 
                   SymbolContextList& sc_list);

    //------------------------------------------------------------------
    /// Find global and static variables by name.
    ///
    /// @param[in] name
    ///     The name of the global or static variable we are looking
    ///     for.
    ///
    /// @param[in] namespace_decl
    ///     If valid, a namespace to search in.
    ///
    /// @param[in] append
    ///     If \b true, any matches will be appended to \a
    ///     variable_list, else matches replace the contents of
    ///     \a variable_list.
    ///
    /// @param[in] max_matches
    ///     Allow the number of matches to be limited to \a
    ///     max_matches. Specify UINT32_MAX to get all possible matches.
    ///
    /// @param[in] variable_list
    ///     A list of variables that gets the matches appended to (if
    ///     \a append it \b true), or replace (if \a append is \b false).
    ///
    /// @return
    ///     The number of matches added to \a variable_list.
    //------------------------------------------------------------------
    uint32_t
    FindGlobalVariables (const ConstString &name,
                         const ClangNamespaceDecl *namespace_decl,
                         bool append, 
                         uint32_t max_matches, 
                         VariableList& variable_list);

    //------------------------------------------------------------------
    /// Find global and static variables by regular exression.
    ///
    /// @param[in] regex
    ///     A regular expression to use when matching the name.
    ///
    /// @param[in] append
    ///     If \b true, any matches will be appended to \a
    ///     variable_list, else matches replace the contents of
    ///     \a variable_list.
    ///
    /// @param[in] max_matches
    ///     Allow the number of matches to be limited to \a
    ///     max_matches. Specify UINT32_MAX to get all possible matches.
    ///
    /// @param[in] variable_list
    ///     A list of variables that gets the matches appended to (if
    ///     \a append it \b true), or replace (if \a append is \b false).
    ///
    /// @return
    ///     The number of matches added to \a variable_list.
    //------------------------------------------------------------------
    uint32_t
    FindGlobalVariables (const RegularExpression& regex, 
                         bool append, 
                         uint32_t max_matches, 
                         VariableList& variable_list);

    //------------------------------------------------------------------
    /// Find types by name.
    ///
    /// @param[in] sc
    ///     A symbol context that scopes where to extract a type list
    ///     from.
    ///
    /// @param[in] name
    ///     The name of the type we are looking for.
    ///
    /// @param[in] namespace_decl
    ///     If valid, a namespace to search in.
    ///
    /// @param[in] append
    ///     If \b true, any matches will be appended to \a
    ///     variable_list, else matches replace the contents of
    ///     \a variable_list.
    ///
    /// @param[in] max_matches
    ///     Allow the number of matches to be limited to \a
    ///     max_matches. Specify UINT32_MAX to get all possible matches.
    ///
    /// @param[in] encoding
    ///     Limit the search to specific types, or get all types if
    ///     set to Type::invalid.
    ///
    /// @param[in] udt_name
    ///     If the encoding is a user defined type, specify the name
    ///     of the user defined type ("struct", "union", "class", etc).
    ///
    /// @param[out] type_list
    ///     A type list gets populated with any matches.
    ///
    /// @return
    ///     The number of matches added to \a type_list.
    //------------------------------------------------------------------
    uint32_t
    FindTypes (const SymbolContext& sc,
               const ConstString &name,
               const ClangNamespaceDecl *namespace_decl,
               bool append, 
               uint32_t max_matches, 
               TypeList& types);

    //------------------------------------------------------------------
    /// Get const accessor for the module architecture.
    ///
    /// @return
    ///     A const reference to the architecture object.
    //------------------------------------------------------------------
    const ArchSpec&
    GetArchitecture () const;

    //------------------------------------------------------------------
    /// Get const accessor for the module file specification.
    ///
    /// This function returns the file for the module on the host system
    /// that is running LLDB. This can differ from the path on the 
    /// platform since we might be doing remote debugging.
    ///
    /// @return
    ///     A const reference to the file specification object.
    //------------------------------------------------------------------
    const FileSpec &
    GetFileSpec () const
    {
        return m_file;
    }

    //------------------------------------------------------------------
    /// Get accessor for the module platform file specification.
    ///
    /// Platform file refers to the path of the module as it is known on
    /// the remote system on which it is being debugged. For local 
    /// debugging this is always the same as Module::GetFileSpec(). But
    /// remote debugging might mention a file "/usr/lib/liba.dylib"
    /// which might be locally downloaded and cached. In this case the
    /// platform file could be something like:
    /// "/tmp/lldb/platform-cache/remote.host.computer/usr/lib/liba.dylib"
    /// The file could also be cached in a local developer kit directory.
    ///
    /// @return
    ///     A const reference to the file specification object.
    //------------------------------------------------------------------
    const FileSpec &
    GetPlatformFileSpec () const
    {
        if (m_platform_file)
            return m_platform_file;
        return m_file;
    }

    void
    SetPlatformFileSpec (const FileSpec &file)
    {
        m_platform_file = file;
    }

    const TimeValue &
    GetModificationTime () const;
   
    //------------------------------------------------------------------
    /// Tells whether this module is capable of being the main executable
    /// for a process.
    ///
    /// @return
    ///     \b true if it is, \b false otherwise.
    //------------------------------------------------------------------
    bool
    IsExecutable ();
    
    //------------------------------------------------------------------
    /// Tells whether this module has been loaded in the target passed in.
    /// This call doesn't distinguish between whether the module is loaded
    /// by the dynamic loader, or by a "target module add" type call.
    ///
    /// @param[in] target
    ///    The target to check whether this is loaded in.
    ///
    /// @return
    ///     \b true if it is, \b false otherwise.
    //------------------------------------------------------------------
    bool
    IsLoadedInTarget (Target *target);

    //------------------------------------------------------------------
    /// Get the number of compile units for this module.
    ///
    /// @return
    ///     The number of compile units that the symbol vendor plug-in
    ///     finds.
    //------------------------------------------------------------------
    uint32_t
    GetNumCompileUnits();

    lldb::CompUnitSP
    GetCompileUnitAtIndex (uint32_t);

    const ConstString &
    GetObjectName() const;

    uint64_t
    GetObjectOffset() const
    {
        return m_object_offset;
    }

    //------------------------------------------------------------------
    /// Get the object file representation for the current architecture.
    ///
    /// If the object file has not been located or parsed yet, this
    /// function will find the best ObjectFile plug-in that can parse
    /// Module::m_file.
    ///
    /// @return
    ///     If Module::m_file does not exist, or no plug-in was found
    ///     that can parse the file, or the object file doesn't contain
    ///     the current architecture in Module::m_arch, NULL will be
    ///     returned, else a valid object file interface will be
    ///     returned. The returned pointer is owned by this object and
    ///     remains valid as long as the object is around.
    //------------------------------------------------------------------
    ObjectFile *
    GetObjectFile ();

    //------------------------------------------------------------------
    /// Get the symbol vendor interface for the current architecture.
    ///
    /// If the symbol vendor file has not been located yet, this
    /// function will find the best SymbolVendor plug-in that can
    /// use the current object file.
    ///
    /// @return
    ///     If this module does not have a valid object file, or no
    ///     plug-in can be found that can use the object file, NULL will
    ///     be returned, else a valid symbol vendor plug-in interface
    ///     will be returned. The returned pointer is owned by this
    ///     object and remains valid as long as the object is around.
    //------------------------------------------------------------------
    SymbolVendor*
    GetSymbolVendor(bool can_create = true);

    //------------------------------------------------------------------
    /// Get accessor the type list for this module.
    ///
    /// @return
    ///     A valid type list pointer, or NULL if there is no valid
    ///     symbol vendor for this module.
    //------------------------------------------------------------------
    TypeList*
    GetTypeList ();

    //------------------------------------------------------------------
    /// Get a pointer to the UUID value contained in this object.
    ///
    /// If the executable image file doesn't not have a UUID value built
    /// into the file format, an MD5 checksum of the entire file, or
    /// slice of the file for the current architecture should be used.
    ///
    /// @return
    ///     A const pointer to the internal copy of the UUID value in
    ///     this module if this module has a valid UUID value, NULL
    ///     otherwise.
    //------------------------------------------------------------------
    const lldb_private::UUID &
    GetUUID ();

    //------------------------------------------------------------------
    /// A debugging function that will cause everything in a module to
    /// be parsed.
    ///
    /// All compile units will be pasred, along with all globals and
    /// static variables and all functions for those compile units.
    /// All types, scopes, local variables, static variables, global
    /// variables, and line tables will be parsed. This can be used
    /// prior to dumping a module to see a complete list of the
    /// resuling debug information that gets parsed, or as a debug
    /// function to ensure that the module can consume all of the
    /// debug data the symbol vendor provides.
    //------------------------------------------------------------------
    void
    ParseAllDebugSymbols();

    bool
    ResolveFileAddress (lldb::addr_t vm_addr, Address& so_addr);

    uint32_t
    ResolveSymbolContextForAddress (const Address& so_addr, uint32_t resolve_scope, SymbolContext& sc);

    //------------------------------------------------------------------
    /// Resolve items in the symbol context for a given file and line.
    ///
    /// Tries to resolve \a file_path and \a line to a list of matching
    /// symbol contexts.
    ///
    /// The line table entries contains addresses that can be used to
    /// further resolve the values in each match: the function, block,
    /// symbol. Care should be taken to minimize the amount of
    /// information that is requested to only what is needed --
    /// typically the module, compile unit, line table and line table
    /// entry are sufficient.
    ///
    /// @param[in] file_path
    ///     A path to a source file to match. If \a file_path does not
    ///     specify a directory, then this query will match all files
    ///     whose base filename matches. If \a file_path does specify
    ///     a directory, the fullpath to the file must match.
    ///
    /// @param[in] line
    ///     The source line to match, or zero if just the compile unit
    ///     should be resolved.
    ///
    /// @param[in] check_inlines
    ///     Check for inline file and line number matches. This option
    ///     should be used sparingly as it will cause all line tables
    ///     for every compile unit to be parsed and searched for
    ///     matching inline file entries.
    ///
    /// @param[in] resolve_scope
    ///     The scope that should be resolved (see
    ///     SymbolContext::Scope).
    ///
    /// @param[out] sc_list
    ///     A symbol context list that gets matching symbols contexts
    ///     appended to.
    ///
    /// @return
    ///     The number of matches that were added to \a sc_list.
    ///
    /// @see SymbolContext::Scope
    //------------------------------------------------------------------
    uint32_t
    ResolveSymbolContextForFilePath (const char *file_path, uint32_t line, bool check_inlines, uint32_t resolve_scope, SymbolContextList& sc_list);

    //------------------------------------------------------------------
    /// Resolve items in the symbol context for a given file and line.
    ///
    /// Tries to resolve \a file_spec and \a line to a list of matching
    /// symbol contexts.
    ///
    /// The line table entries contains addresses that can be used to
    /// further resolve the values in each match: the function, block,
    /// symbol. Care should be taken to minimize the amount of
    /// information that is requested to only what is needed --
    /// typically the module, compile unit, line table and line table
    /// entry are sufficient.
    ///
    /// @param[in] file_spec
    ///     A file spec to a source file to match. If \a file_path does
    ///     not specify a directory, then this query will match all
    ///     files whose base filename matches. If \a file_path does
    ///     specify a directory, the fullpath to the file must match.
    ///
    /// @param[in] line
    ///     The source line to match, or zero if just the compile unit
    ///     should be resolved.
    ///
    /// @param[in] check_inlines
    ///     Check for inline file and line number matches. This option
    ///     should be used sparingly as it will cause all line tables
    ///     for every compile unit to be parsed and searched for
    ///     matching inline file entries.
    ///
    /// @param[in] resolve_scope
    ///     The scope that should be resolved (see
    ///     SymbolContext::Scope).
    ///
    /// @param[out] sc_list
    ///     A symbol context list that gets filled in with all of the
    ///     matches.
    ///
    /// @return
    ///     A integer that contains SymbolContext::Scope bits set for
    ///     each item that was successfully resolved.
    ///
    /// @see SymbolContext::Scope
    //------------------------------------------------------------------
    uint32_t
    ResolveSymbolContextsForFileSpec (const FileSpec &file_spec, uint32_t line, bool check_inlines, uint32_t resolve_scope, SymbolContextList& sc_list);


    void
    SetFileSpecAndObjectName (const FileSpec &file,
                              const ConstString &object_name);

    bool
    GetIsDynamicLinkEditor () const
    {
        return m_is_dynamic_loader_module;
    }
    
    void
    SetIsDynamicLinkEditor (bool b)
    {
        m_is_dynamic_loader_module = b;
    }
    
    ClangASTContext &
    GetClangASTContext ();

    // Special error functions that can do printf style formatting that will prepend the message with
    // something appropriate for this module (like the architecture, path and object name (if any)). 
    // This centralizes code so that everyone doesn't need to format their error and log messages on
    // their own and keeps the output a bit more consistent.
    void                    
    LogMessage (Log *log, const char *format, ...) __attribute__ ((format (printf, 3, 4)));

    void
    ReportWarning (const char *format, ...) __attribute__ ((format (printf, 2, 3)));

    void
    ReportError (const char *format, ...) __attribute__ ((format (printf, 2, 3)));

    // Only report an error once when the module is first detected to be modified
    // so we don't spam the console with many messages.
    void
    ReportErrorIfModifyDetected (const char *format, ...) __attribute__ ((format (printf, 2, 3)));

    bool
    GetModified (bool use_cached_only);
    
    bool
    SetModified (bool b);

protected:
    //------------------------------------------------------------------
    // Member Variables
    //------------------------------------------------------------------
    mutable Mutex               m_mutex;        ///< A mutex to keep this object happy in multi-threaded environments.
    TimeValue                   m_mod_time;     ///< The modification time for this module when it was created.
    ArchSpec                    m_arch;         ///< The architecture for this module.
    lldb_private::UUID          m_uuid;         ///< Each module is assumed to have a unique identifier to help match it up to debug symbols.
    FileSpec                    m_file;         ///< The file representation on disk for this module (if there is one).
    FileSpec                    m_platform_file;///< The path to the module on the platform on which it is being debugged
    ConstString                 m_object_name;  ///< The name an object within this module that is selected, or empty of the module is represented by \a m_file.
    uint64_t                    m_object_offset;
    lldb::ObjectFileSP          m_objfile_sp;   ///< A shared pointer to the object file parser for this module as it may or may not be shared with the SymbolFile
    std::auto_ptr<SymbolVendor> m_symfile_ap;   ///< A pointer to the symbol vendor for this module.
    ClangASTContext             m_ast;          ///< The AST context for this module.
    bool                        m_did_load_objfile:1,
                                m_did_load_symbol_vendor:1,
                                m_did_parse_uuid:1,
                                m_did_init_ast:1,
                                m_is_dynamic_loader_module:1,
                                m_was_modified:1;   /// See if the module was modified after it was initially opened.
    
    //------------------------------------------------------------------
    /// Resolve a file or load virtual address.
    ///
    /// Tries to resolve \a vm_addr as a file address (if \a
    /// vm_addr_is_file_addr is true) or as a load address if \a
    /// vm_addr_is_file_addr is false) in the symbol vendor.
    /// \a resolve_scope indicates what clients wish to resolve
    /// and can be used to limit the scope of what is parsed.
    ///
    /// @param[in] vm_addr
    ///     The load virtual address to resolve.
    ///
    /// @param[in] vm_addr_is_file_addr
    ///     If \b true, \a vm_addr is a file address, else \a vm_addr
    ///     if a load address.
    ///
    /// @param[in] resolve_scope
    ///     The scope that should be resolved (see
    ///     SymbolContext::Scope).
    ///
    /// @param[out] so_addr
    ///     The section offset based address that got resolved if
    ///     any bits are returned.
    ///
    /// @param[out] sc
    //      The symbol context that has objects filled in. Each bit
    ///     in the \a resolve_scope pertains to a member in the \a sc.
    ///
    /// @return
    ///     A integer that contains SymbolContext::Scope bits set for
    ///     each item that was successfully resolved.
    ///
    /// @see SymbolContext::Scope
    //------------------------------------------------------------------
    uint32_t
    ResolveSymbolContextForAddress (lldb::addr_t vm_addr, 
                                    bool vm_addr_is_file_addr, 
                                    uint32_t resolve_scope, 
                                    Address& so_addr, 
                                    SymbolContext& sc);
    
    void 
    SymbolIndicesToSymbolContextList (Symtab *symtab, 
                                      std::vector<uint32_t> &symbol_indexes, 
                                      SymbolContextList &sc_list);
    
    bool
    SetArchitecture (const ArchSpec &new_arch);
    
private:

    uint32_t
    FindTypes_Impl (const SymbolContext& sc, 
                    const ConstString &name,
                    const ClangNamespaceDecl *namespace_decl,
                    bool append, 
                    uint32_t max_matches, 
                    TypeList& types);

    
    DISALLOW_COPY_AND_ASSIGN (Module);
};

} // namespace lldb_private

#endif  // liblldb_Module_h_
