//===-- SymbolFileDWARFDebugMap.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_SymbolFileDWARFDebugMap_h_
#define SymbolFileDWARF_SymbolFileDWARFDebugMap_h_


#include <vector>
#include <bitset>
#include "lldb/Symbol/SymbolFile.h"

#include "UniqueDWARFASTType.h"

class SymbolFileDWARF;
class DWARFCompileUnit;
class DWARFDebugInfoEntry;

class SymbolFileDWARFDebugMap : public lldb_private::SymbolFile
{
public:
    //------------------------------------------------------------------
    // Static Functions
    //------------------------------------------------------------------
    static void
    Initialize();

    static void
    Terminate();

    static const char *
    GetPluginNameStatic();

    static const char *
    GetPluginDescriptionStatic();

    static lldb_private::SymbolFile *
    CreateInstance (lldb_private::ObjectFile* obj_file);

    //------------------------------------------------------------------
    // Constructors and Destructors
    //------------------------------------------------------------------
                            SymbolFileDWARFDebugMap (lldb_private::ObjectFile* ofile);
    virtual               ~ SymbolFileDWARFDebugMap ();

    virtual uint32_t        CalculateAbilities ();

    virtual void            InitializeObject();

    //------------------------------------------------------------------
    // Compile Unit function calls
    //------------------------------------------------------------------
    virtual uint32_t        GetNumCompileUnits ();
    virtual lldb::CompUnitSP ParseCompileUnitAtIndex (uint32_t index);

    virtual size_t          ParseCompileUnitFunctions (const lldb_private::SymbolContext& sc);
    virtual bool            ParseCompileUnitLineTable (const lldb_private::SymbolContext& sc);
    virtual bool            ParseCompileUnitSupportFiles (const lldb_private::SymbolContext& sc, lldb_private::FileSpecList &support_files);
    virtual size_t          ParseFunctionBlocks (const lldb_private::SymbolContext& sc);
    virtual size_t          ParseTypes (const lldb_private::SymbolContext& sc);
    virtual size_t          ParseVariablesForContext (const lldb_private::SymbolContext& sc);

    virtual lldb_private::Type* ResolveTypeUID (lldb::user_id_t type_uid);
    virtual clang::DeclContext* GetClangDeclContextContainingTypeUID (lldb::user_id_t type_uid);
    virtual clang::DeclContext* GetClangDeclContextForTypeUID (const lldb_private::SymbolContext &sc, lldb::user_id_t type_uid);
    virtual lldb::clang_type_t  ResolveClangOpaqueTypeDefinition (lldb::clang_type_t clang_Type);
    virtual uint32_t        ResolveSymbolContext (const lldb_private::Address& so_addr, uint32_t resolve_scope, lldb_private::SymbolContext& sc);
    virtual uint32_t        ResolveSymbolContext (const lldb_private::FileSpec& file_spec, uint32_t line, bool check_inlines, uint32_t resolve_scope, lldb_private::SymbolContextList& sc_list);
    virtual uint32_t        FindGlobalVariables (const lldb_private::ConstString &name, const lldb_private::ClangNamespaceDecl *namespace_decl, bool append, uint32_t max_matches, lldb_private::VariableList& variables);
    virtual uint32_t        FindGlobalVariables (const lldb_private::RegularExpression& regex, bool append, uint32_t max_matches, lldb_private::VariableList& variables);
    virtual uint32_t        FindFunctions (const lldb_private::ConstString &name, const lldb_private::ClangNamespaceDecl *namespace_decl, uint32_t name_type_mask, bool append, lldb_private::SymbolContextList& sc_list);
    virtual uint32_t        FindFunctions (const lldb_private::RegularExpression& regex, bool append, lldb_private::SymbolContextList& sc_list);
    virtual uint32_t        FindTypes (const lldb_private::SymbolContext& sc, const lldb_private::ConstString &name, const lldb_private::ClangNamespaceDecl *namespace_decl, bool append, uint32_t max_matches, lldb_private::TypeList& types);
    virtual lldb_private::ClangNamespaceDecl
            FindNamespace (const lldb_private::SymbolContext& sc, 
                           const lldb_private::ConstString &name,
                           const lldb_private::ClangNamespaceDecl *parent_namespace_decl);


    //------------------------------------------------------------------
    // ClangASTContext callbacks for external source lookups.
    //------------------------------------------------------------------
    static void
    CompleteTagDecl (void *baton, clang::TagDecl *);
    
    static void
    CompleteObjCInterfaceDecl (void *baton, clang::ObjCInterfaceDecl *);

    //------------------------------------------------------------------
    // PluginInterface protocol
    //------------------------------------------------------------------
    virtual const char *
    GetPluginName();

    virtual const char *
    GetShortPluginName();

    virtual uint32_t
    GetPluginVersion();

protected:
    enum
    {
        kHaveInitializedOSOs = (1 << 0),
        kNumFlags
    };

    friend class SymbolFileDWARF;

    //------------------------------------------------------------------
    // Class specific types
    //------------------------------------------------------------------
    struct CompileUnitInfo
    {
        lldb_private::FileSpec so_file;
        lldb_private::Symbol *so_symbol;
        lldb_private::Symbol *oso_symbol;
        lldb_private::Symbol *last_symbol;
        uint32_t first_symbol_index;
        uint32_t last_symbol_index;
        lldb::ModuleSP oso_module_sp;
        lldb::CompUnitSP oso_compile_unit_sp;
        lldb_private::SymbolVendor *oso_symbol_vendor;
        std::vector<uint32_t> function_indexes;
        std::vector<uint32_t> static_indexes;
        lldb::SharedPtr<lldb_private::SectionList>::Type debug_map_sections_sp;

        CompileUnitInfo() :
            so_file (),
            so_symbol (NULL),
            oso_symbol (NULL),
            last_symbol (NULL),
            first_symbol_index (UINT32_MAX),
            last_symbol_index (UINT32_MAX),
            oso_module_sp (),
            oso_compile_unit_sp (),
            oso_symbol_vendor (NULL),
            function_indexes (),
            static_indexes (),
            debug_map_sections_sp ()
        {
        }
    };

    //------------------------------------------------------------------
    // Protected Member Functions
    //------------------------------------------------------------------
    void
    InitOSO ();

    static uint32_t
    GetOSOIndexFromUserID (lldb::user_id_t uid)
    {
        return (uint32_t)((uid >> 32ull) - 1ull);
    }
    bool
    GetFileSpecForSO (uint32_t oso_idx, lldb_private::FileSpec &file_spec);

    CompileUnitInfo *
    GetCompUnitInfo (const lldb_private::SymbolContext& sc);

    lldb_private::Module *
    GetModuleByCompUnitInfo (CompileUnitInfo *comp_unit_info);

    lldb_private::Module *
    GetModuleByOSOIndex (uint32_t oso_idx);

    lldb_private::ObjectFile *
    GetObjectFileByCompUnitInfo (CompileUnitInfo *comp_unit_info);

    lldb_private::ObjectFile *
    GetObjectFileByOSOIndex (uint32_t oso_idx);

    uint32_t
    GetCompUnitInfoIndex (const CompileUnitInfo *comp_unit_info);

    SymbolFileDWARF *
    GetSymbolFile (const lldb_private::SymbolContext& sc);

    SymbolFileDWARF *
    GetSymbolFileByCompUnitInfo (CompileUnitInfo *comp_unit_info);

    SymbolFileDWARF *
    GetSymbolFileByOSOIndex (uint32_t oso_idx);

    CompileUnitInfo *
    GetCompileUnitInfoForSymbolWithIndex (uint32_t symbol_idx, uint32_t *oso_idx_ptr);
    
    CompileUnitInfo *
    GetCompileUnitInfoForSymbolWithID (lldb::user_id_t symbol_id, uint32_t *oso_idx_ptr);

    static int
    SymbolContainsSymbolWithIndex (uint32_t *symbol_idx_ptr, const CompileUnitInfo *comp_unit_info);

    static int
    SymbolContainsSymbolWithID (lldb::user_id_t *symbol_idx_ptr, const CompileUnitInfo *comp_unit_info);

    uint32_t
    PrivateFindGlobalVariables (const lldb_private::ConstString &name,
                                const lldb_private::ClangNamespaceDecl *namespace_decl,
                                const std::vector<uint32_t> &name_symbol_indexes,
                                uint32_t max_matches,
                                lldb_private::VariableList& variables);


    void
    SetCompileUnit (SymbolFileDWARF *oso_dwarf, const lldb::CompUnitSP &cu_sp);

    lldb::TypeSP
    FindDefinitionTypeForDIE (DWARFCompileUnit* cu, 
                              const DWARFDebugInfoEntry *die, 
                              const lldb_private::ConstString &type_name);    

    lldb::TypeSP
    FindCompleteObjCDefinitionTypeForDIE (DWARFCompileUnit* cu, 
                                          const DWARFDebugInfoEntry *die, 
                                          const lldb_private::ConstString &type_name);
    

    UniqueDWARFASTTypeMap &
    GetUniqueDWARFASTTypeMap ()
    {
        return m_unique_ast_type_map;
    }
    //------------------------------------------------------------------
    // Member Variables
    //------------------------------------------------------------------
    std::bitset<kNumFlags> m_flags;
    std::vector<CompileUnitInfo> m_compile_unit_infos;
    std::vector<uint32_t> m_func_indexes;   // Sorted by address
    std::vector<uint32_t> m_glob_indexes;
    UniqueDWARFASTTypeMap m_unique_ast_type_map;
};

#endif // #ifndef SymbolFileDWARF_SymbolFileDWARFDebugMap_h_
