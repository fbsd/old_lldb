//===-- HashedNameToDIE.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_HashedNameToDIE_h_
#define SymbolFileDWARF_HashedNameToDIE_h_

#include <vector>

#include "DWARFDefines.h"
#include "DWARFFormValue.h"

#include "lldb/lldb-defines.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Core/RegularExpression.h"
#include "lldb/Core/MappedHash.h"


class SymbolFileDWARF;
class DWARFCompileUnit;
class DWARFDebugInfoEntry;

struct DWARFMappedHash
{
    struct DIEInfo
    {
        dw_offset_t offset;  // The DIE offset
        dw_tag_t tag;
        uint32_t type_flags; // Any flags for this DIEInfo

        DIEInfo () :
            offset (DW_INVALID_OFFSET),
            tag (0),
            type_flags (0)
        {
        }

        DIEInfo (dw_offset_t o, dw_tag_t t, uint32_t f) :
            offset(o),
            tag (t),
            type_flags (f)
        {
        }
        
        void
        Clear()
        {
            offset = DW_INVALID_OFFSET;
            tag = 0;
            type_flags = 0;
        }            
    };
    
    typedef std::vector<DIEInfo> DIEInfoArray;
    typedef std::vector<uint32_t> DIEArray;
    
    static void
    ExtractDIEArray (const DIEInfoArray &die_info_array,
                     DIEArray &die_offsets)
    {
        const size_t count = die_info_array.size();
        for (size_t i=0; i<count; ++i)
        {
            die_offsets.push_back (die_info_array[i].offset);
        }
    }

    static void
    ExtractDIEArray (const DIEInfoArray &die_info_array,
                     const dw_tag_t tag,
                     DIEArray &die_offsets)
    {
        if (tag == 0)
        {
            ExtractDIEArray (die_info_array, die_offsets);
        }
        else
        {
            const size_t count = die_info_array.size();
            for (size_t i=0; i<count; ++i)
            {
                const dw_tag_t die_tag = die_info_array[i].tag;
                if (die_tag == 0 || tag == die_tag)
                    die_offsets.push_back (die_info_array[i].offset);
            }
        }
    }

    static void
    ExtractTypesFromDIEArray (const DIEInfoArray &die_info_array,
                              uint32_t type_flag_mask,
                              uint32_t type_flag_value,
                              DIEArray &die_offsets)
    {
        const size_t count = die_info_array.size();
        for (size_t i=0; i<count; ++i)
        {
            if ((die_info_array[i].type_flags & type_flag_mask) == type_flag_value)
                die_offsets.push_back (die_info_array[i].offset);
        }
    }

    enum AtomType
    {
        eAtomTypeNULL       = 0u,
        eAtomTypeDIEOffset  = 1u,   // DIE offset, check form for encoding
        eAtomTypeCUOffset   = 2u,   // DIE offset of the compiler unit header that contains the item in question
        eAtomTypeTag        = 3u,   // DW_TAG_xxx value, should be encoded as DW_FORM_data1 (if no tags exceed 255) or DW_FORM_data2
        eAtomTypeNameFlags  = 4u,   // Flags from enum NameFlags
        eAtomTypeTypeFlags  = 5u    // Flags from enum TypeFlags
    };
    
    // Bit definitions for the eAtomTypeTypeFlags flags
    enum TypeFlags
    {
        // If the name contains the namespace and class scope or the type 
        // exists in the global namespace, then this bits should be set
        eTypeFlagNameIsFullyQualified   = ( 1u << 0 ),
        
        // Always set for C++, only set for ObjC if this is the 
        // @implementation for class
        eTypeFlagClassIsImplementation  = ( 1u << 1 )
    };

    struct Atom
    {
        uint16_t type;
        dw_form_t form;
        
        Atom (uint16_t t = eAtomTypeNULL, dw_form_t f = 0) :
            type (t),
            form (f)
        {
        }
    };
    
    typedef std::vector<Atom> AtomArray;
    
    static uint32_t 
    GetTypeFlags (SymbolFileDWARF *dwarf2Data,
                  const DWARFCompileUnit* cu,
                  const DWARFDebugInfoEntry* die);
    

    static const char *
    GetAtomTypeName (uint16_t atom)
    {
        switch (atom)
        {
            case eAtomTypeNULL:         return "NULL";
            case eAtomTypeDIEOffset:    return "die-offset";
            case eAtomTypeCUOffset:     return "cu-offset";
            case eAtomTypeTag:          return "die-tag";
            case eAtomTypeNameFlags:    return "name-flags";
            case eAtomTypeTypeFlags:    return "type-flags";
        }
        return "<invalid>";
    }
    struct Prologue
    {
        // DIE offset base so die offsets in hash_data can be CU relative
        dw_offset_t die_base_offset;
        AtomArray atoms;
        size_t min_hash_data_byte_size;
        bool hash_data_has_fixed_byte_size;
        
        Prologue (dw_offset_t _die_base_offset = 0) :
            die_base_offset (_die_base_offset),
            atoms(),
            min_hash_data_byte_size(0),
            hash_data_has_fixed_byte_size(true)
        {
            // Define an array of DIE offsets by first defining an array, 
            // and then define the atom type for the array, in this case
            // we have an array of DIE offsets
            AppendAtom (eAtomTypeDIEOffset, DW_FORM_data4);
        }
        
        virtual ~Prologue()
        {
        }

        void
        ClearAtoms ()
        {
            hash_data_has_fixed_byte_size = true;
            min_hash_data_byte_size = 0;
            atoms.clear();
        }

        virtual void
        Clear ()
        {
            die_base_offset = 0;
            ClearAtoms ();
        }
        
        void
        AppendAtom (AtomType type, dw_form_t form)
        {
            atoms.push_back (Atom(type, form));
            switch (form)
            {
                case DW_FORM_indirect:
                case DW_FORM_exprloc:
                case DW_FORM_flag_present:
                case DW_FORM_ref_sig8:
                    assert (!"Unhandled atom form");
                    break;

                case DW_FORM_string:
                case DW_FORM_block:
                case DW_FORM_block1:
                case DW_FORM_sdata:
                case DW_FORM_udata:
                case DW_FORM_ref_udata:
                    hash_data_has_fixed_byte_size = false;
                    // Fall through to the cases below...
                case DW_FORM_flag:
                case DW_FORM_data1:
                case DW_FORM_ref1:
                case DW_FORM_sec_offset:
                    min_hash_data_byte_size += 1; 
                    break;

                case DW_FORM_block2:
                    hash_data_has_fixed_byte_size = false;
                    // Fall through to the cases below...
                case DW_FORM_data2: 
                case DW_FORM_ref2:
                    min_hash_data_byte_size += 2; 
                    break;

                case DW_FORM_block4: 
                    hash_data_has_fixed_byte_size = false;
                    // Fall through to the cases below...
                case DW_FORM_data4:
                case DW_FORM_ref4:
                case DW_FORM_addr:
                case DW_FORM_ref_addr:
                case DW_FORM_strp:
                    min_hash_data_byte_size += 4; 
                    break;

                case DW_FORM_data8:
                case DW_FORM_ref8:
                    min_hash_data_byte_size += 8; 
                    break;
                    
            }
        }
        
//        void
//        Dump (std::ostream* ostrm_ptr);        
        
        uint32_t
        Read (const lldb_private::DataExtractor &data, uint32_t offset)
        {
            ClearAtoms ();
            
            die_base_offset = data.GetU32 (&offset);
            
            const uint32_t atom_count = data.GetU32 (&offset);
            if (atom_count == 0x00060003u)
            {
                // Old format, deal with contents of old pre-release format
                while (data.GetU32(&offset))
                    /* do nothing */;

                // Hardcode to the only known value for now.
                AppendAtom (eAtomTypeDIEOffset, DW_FORM_data4);
            }
            else
            {
                for (uint32_t i=0; i<atom_count; ++i)
                {
                    AtomType type = (AtomType)data.GetU16 (&offset);
                    dw_form_t form = (dw_form_t)data.GetU16 (&offset);                    
                    AppendAtom (type, form);
                }
            }
            return offset;
        }
        
//        virtual void
//        Write (BinaryStreamBuf &s);
        
        size_t
        GetByteSize () const
        {
            // Add an extra count to the atoms size for the zero termination Atom that gets
            // written to disk
            return sizeof(die_base_offset) + sizeof(uint32_t) + atoms.size() * sizeof(Atom);
        }
        
        size_t
        GetMinumumHashDataByteSize () const
        {
            return min_hash_data_byte_size;
        }

        bool
        HashDataHasFixedByteSize() const
        {
            return hash_data_has_fixed_byte_size;
        }
    };
    
    struct Header : public MappedHash::Header<Prologue>
    {
        Header (dw_offset_t _die_base_offset = 0)
        {
        }
        
        virtual 
        ~Header()
        {
        }

        virtual size_t
        GetByteSize (const HeaderData &header_data)
        {
            return header_data.GetByteSize();
        }

        //        virtual void
        //        Dump (std::ostream* ostrm_ptr);        
        //        
        virtual uint32_t
        Read (lldb_private::DataExtractor &data, uint32_t offset)
        {
            offset = MappedHash::Header<Prologue>::Read (data, offset);
            if (offset != UINT32_MAX)
            {
                offset = header_data.Read (data, offset);
            }
            return offset;
        }
        
        bool
        Read (const lldb_private::DataExtractor &data, 
              uint32_t *offset_ptr, 
              DIEInfo &hash_data) const
        {
            const size_t num_atoms = header_data.atoms.size();
            if (num_atoms == 0)
                return false;
            
            for (size_t i=0; i<num_atoms; ++i)
            {
                DWARFFormValue form_value (header_data.atoms[i].form);
                
                if (!form_value.ExtractValue(data, offset_ptr, NULL))
                    return false;
                
                switch (header_data.atoms[i].type)
                {
                    case eAtomTypeDIEOffset:    // DIE offset, check form for encoding
                        hash_data.offset = form_value.Reference (header_data.die_base_offset);
                        break;

                    case eAtomTypeTag:          // DW_TAG value for the DIE
                        hash_data.tag = form_value.Unsigned ();
                        
                    case eAtomTypeTypeFlags:    // Flags from enum TypeFlags
                        hash_data.type_flags = form_value.Unsigned ();
                        break;
                    default:
                        return false;
                        break;
                }
            }
            return true;
        }
        
        void
        Dump (lldb_private::Stream& strm, const DIEInfo &hash_data) const
        {
            const size_t num_atoms = header_data.atoms.size();
            for (size_t i=0; i<num_atoms; ++i)
            {
                if (i > 0)
                    strm.PutCString (", ");
                
                DWARFFormValue form_value (header_data.atoms[i].form);
                switch (header_data.atoms[i].type)
                {
                    case eAtomTypeDIEOffset:    // DIE offset, check form for encoding
                        strm.Printf ("{0x%8.8x}", hash_data.offset);
                        break;

                    case eAtomTypeTag:          // DW_TAG value for the DIE
                        {
                            const char *tag_cstr = lldb_private::DW_TAG_value_to_name (hash_data.tag);
                            if (tag_cstr)
                                strm.PutCString (tag_cstr);
                            else
                                strm.Printf ("DW_TAG_(0x%4.4x)", hash_data.tag);
                        }
                        break;

                    case eAtomTypeTypeFlags:    // Flags from enum TypeFlags
                        strm.Printf ("0x%2.2x", hash_data.type_flags);
                        if (hash_data.type_flags)
                        {
                            strm.PutCString (" (");
                            if (hash_data.type_flags & eTypeFlagNameIsFullyQualified)
                                strm.PutCString (" qualified");
                            
                            if (hash_data.type_flags & eTypeFlagClassIsImplementation)
                                strm.PutCString (" implementation");
                            strm.PutCString (" )");
                        }
                        break;
                        
                    default:
                        strm.Printf ("AtomType(0x%x)", header_data.atoms[i].type);
                        break;
                }
            }
        }

    };
    
//    class ExportTable
//    {
//    public:
//        ExportTable ();
//        
//        void
//        AppendNames (DWARFDebugPubnamesSet &pubnames_set,
//                     StringTable &string_table);
//        
//        void
//        AppendNamesEntry (SymbolFileDWARF *dwarf2Data,
//                          const DWARFCompileUnit* cu,
//                          const DWARFDebugInfoEntry* die,
//                          StringTable &string_table);
//        
//        void
//        AppendTypesEntry (DWARFData *dwarf2Data,
//                          const DWARFCompileUnit* cu,
//                          const DWARFDebugInfoEntry* die,
//                          StringTable &string_table);
//        
//        size_t
//        Save (BinaryStreamBuf &names_data, const StringTable &string_table);
//        
//        void
//        AppendName (const char *name, 
//                    uint32_t die_offset, 
//                    StringTable &string_table,
//                    dw_offset_t name_debug_str_offset = DW_INVALID_OFFSET); // If "name" has already been looked up, then it can be supplied
//        void
//        AppendType (const char *name, 
//                    uint32_t die_offset, 
//                    StringTable &string_table);
//        
//        
//    protected:
//        struct Entry
//        {
//            uint32_t hash;
//            uint32_t str_offset;
//            uint32_t die_offset;
//        };
//        
//        // Map uniqued .debug_str offset to the corresponding DIE offsets
//        typedef std::map<uint32_t, DIEInfoArray> NameInfo;
//        // Map a name hash to one or more name infos
//        typedef std::map<uint32_t, NameInfo> BucketEntry;
//        
//        static uint32_t
//        GetByteSize (const NameInfo &name_info);
//        
//        typedef std::vector<BucketEntry> BucketEntryColl;
//        typedef std::vector<Entry> EntryColl;
//        EntryColl m_entries;
//        
//    };
    
    
    // A class for reading and using a saved hash table from a block of data
    // in memory
    class MemoryTable : public MappedHash::MemoryTable<uint32_t, DWARFMappedHash::Header, DIEInfoArray>
    {
    public:
        
        MemoryTable (lldb_private::DataExtractor &table_data, 
                     const lldb_private::DataExtractor &string_table,
                     const char *name) :
            MappedHash::MemoryTable<uint32_t, Header, DIEInfoArray> (table_data),
            m_data (table_data),
            m_string_table (string_table),
            m_name (name)
        {
        }
    
        virtual 
        ~MemoryTable ()
        {
        }

        virtual const char *
        GetStringForKeyType (KeyType key) const
        {
            // The key in the DWARF table is the .debug_str offset for the string
            return m_string_table.PeekCStr (key);
        }
        
        virtual Result
        GetHashDataForName (const char *name,
                            uint32_t* hash_data_offset_ptr, 
                            Pair &pair) const
        {
            pair.key = m_data.GetU32 (hash_data_offset_ptr);
            pair.value.clear();

            // If the key is zero, this terminates our chain of HashData objects
            // for this hash value.
            if (pair.key == 0)
                return eResultEndOfHashData;

            // There definitely should be a string for this string offset, if
            // there isn't, there is something wrong, return and error
            const char *strp_cstr = m_string_table.PeekCStr (pair.key);
            if (strp_cstr == NULL)
            {
                *hash_data_offset_ptr = UINT32_MAX;
                return eResultError;
            }

            const uint32_t count = m_data.GetU32 (hash_data_offset_ptr);
            const uint32_t min_total_hash_data_size = count * m_header.header_data.GetMinumumHashDataByteSize();
            if (count > 0 && m_data.ValidOffsetForDataOfSize (*hash_data_offset_ptr, min_total_hash_data_size))
            {
                // We have at least one HashData entry, and we have enough
                // data to parse at leats "count" HashData enties.
                
                // First make sure the entire C string matches...
                const bool match = strcmp (name, strp_cstr) == 0;
                
                if (!match && m_header.header_data.HashDataHasFixedByteSize())
                {
                    // If the string doesn't match and we have fixed size data,
                    // we can just add the total byte size of all HashData objects
                    // to the hash data offset and be done...
                    *hash_data_offset_ptr += min_total_hash_data_size;
                }
                else
                {
                    // If the string does match, or we don't have fixed size data
                    // then we need to read the hash data as a stream. If the
                    // string matches we also append all HashData objects to the
                    // value array.
                    for (uint32_t i=0; i<count; ++i)
                    {
                        DIEInfo die_info;
                        if (m_header.Read(m_data, hash_data_offset_ptr, die_info))
                        {
                            // Only happend the HashData if the string matched...
                            if (match)
                                pair.value.push_back (die_info);
                        }
                        else
                        {
                            // Something went wrong while reading the data
                            *hash_data_offset_ptr = UINT32_MAX;
                            return eResultError;
                        }
                    }
                }
                // Return the correct response depending on if the string matched
                // or not...
                if (match)
                    return eResultKeyMatch;     // The key (cstring) matches and we have lookup results!
                else
                    return eResultKeyMismatch;  // The key doesn't match, this function will get called 
                                                // again for the next key/value or the key terminator
                                                // which in our case is a zero .debug_str offset.
            }
            else
            {
                *hash_data_offset_ptr = UINT32_MAX;
                return eResultError;
            }
        }

        virtual Result
        AppendHashDataForRegularExpression (const lldb_private::RegularExpression& regex,
                                            uint32_t* hash_data_offset_ptr, 
                                            Pair &pair) const
        {
            pair.key = m_data.GetU32 (hash_data_offset_ptr);
            // If the key is zero, this terminates our chain of HashData objects
            // for this hash value.
            if (pair.key == 0)
                return eResultEndOfHashData;
            
            // There definitely should be a string for this string offset, if
            // there isn't, there is something wrong, return and error
            const char *strp_cstr = m_string_table.PeekCStr (pair.key);
            if (strp_cstr == NULL)
                return eResultError;
            
            const uint32_t count = m_data.GetU32 (hash_data_offset_ptr);
            const uint32_t min_total_hash_data_size = count * m_header.header_data.GetMinumumHashDataByteSize();
            if (count > 0 && m_data.ValidOffsetForDataOfSize (*hash_data_offset_ptr, min_total_hash_data_size))
            {
                const bool match = regex.Execute(strp_cstr);
                
                if (!match && m_header.header_data.HashDataHasFixedByteSize())
                {
                    // If the regex doesn't match and we have fixed size data,
                    // we can just add the total byte size of all HashData objects
                    // to the hash data offset and be done...
                    *hash_data_offset_ptr += min_total_hash_data_size;
                }
                else
                {
                    // If the string does match, or we don't have fixed size data
                    // then we need to read the hash data as a stream. If the
                    // string matches we also append all HashData objects to the
                    // value array.
                    for (uint32_t i=0; i<count; ++i)
                    {
                        DIEInfo die_info;
                        if (m_header.Read(m_data, hash_data_offset_ptr, die_info))
                        {
                            // Only happend the HashData if the string matched...
                            if (match)
                                pair.value.push_back (die_info);
                        }
                        else
                        {
                            // Something went wrong while reading the data
                            *hash_data_offset_ptr = UINT32_MAX;
                            return eResultError;
                        }
                    }
                }
                // Return the correct response depending on if the string matched
                // or not...
                if (match)
                    return eResultKeyMatch;     // The key (cstring) matches and we have lookup results!
                else
                    return eResultKeyMismatch;  // The key doesn't match, this function will get called 
                                                // again for the next key/value or the key terminator
                                                // which in our case is a zero .debug_str offset.
            }
            else
            {
                *hash_data_offset_ptr = UINT32_MAX;
                return eResultError;
            }
        }

        size_t
        AppendAllDIEsThatMatchingRegex (const lldb_private::RegularExpression& regex, 
                                        DIEInfoArray &die_info_array) const
        {
            const uint32_t hash_count = m_header.hashes_count;
            Pair pair;
            for (uint32_t offset_idx=0; offset_idx<hash_count; ++offset_idx)
            {
                uint32_t hash_data_offset = GetHashDataOffset (offset_idx);
                while (hash_data_offset != UINT32_MAX)
                {
                    const uint32_t prev_hash_data_offset = hash_data_offset;
                    Result hash_result = AppendHashDataForRegularExpression (regex, &hash_data_offset, pair);
                    if (prev_hash_data_offset == hash_data_offset)
                        break;

                    // Check the result of getting our hash data
                    switch (hash_result)
                    {
                        case eResultKeyMatch:
                        case eResultKeyMismatch:
                            // Whether we matches or not, it doesn't matter, we
                            // keep looking.
                            break;
                            
                        case eResultEndOfHashData:
                        case eResultError:
                            hash_data_offset = UINT32_MAX;
                            break;
                    }
                }
            }
            die_info_array.swap (pair.value);
            return die_info_array.size();
        }
        
        size_t
        AppendAllDIEsInRange (const uint32_t die_offset_start, 
                              const uint32_t die_offset_end, 
                              DIEInfoArray &die_info_array) const
        {
            const uint32_t hash_count = m_header.hashes_count;
            for (uint32_t offset_idx=0; offset_idx<hash_count; ++offset_idx)
            {
                bool done = false;
                uint32_t hash_data_offset = GetHashDataOffset (offset_idx);
                while (!done && hash_data_offset != UINT32_MAX)
                {
                    KeyType key = m_data.GetU32 (&hash_data_offset);
                    // If the key is zero, this terminates our chain of HashData objects
                    // for this hash value.
                    if (key == 0)
                        break;
                    
                    const uint32_t count = m_data.GetU32 (&hash_data_offset);
                    for (uint32_t i=0; i<count; ++i)
                    {
                        DIEInfo die_info;
                        if (m_header.Read(m_data, &hash_data_offset, die_info))
                        {
                            if (die_info.offset == 0)
                                done = true;
                            if (die_offset_start <= die_info.offset && die_info.offset < die_offset_end)
                                die_info_array.push_back(die_info);
                        }
                    }
                }
            }
            return die_info_array.size();
        }

        size_t
        FindByName (const char *name, DIEArray &die_offsets)
        {
            DIEInfoArray die_info_array;
            if (FindByName(name, die_info_array))
                DWARFMappedHash::ExtractDIEArray (die_info_array, die_offsets);
            return die_info_array.size();
        }

        size_t
        FindByNameAndTag (const char *name, 
                          const dw_tag_t tag, 
                          DIEArray &die_offsets)
        {
            DIEInfoArray die_info_array;
            if (FindByName(name, die_info_array))
                DWARFMappedHash::ExtractDIEArray (die_info_array, tag, die_offsets);
            return die_info_array.size();
        }

        size_t
        FindCompleteObjCClassByName (const char *name, DIEArray &die_offsets)
        {
            DIEInfoArray die_info_array;
            if (FindByName(name, die_info_array))
            {
                if (GetHeader().header_data.atoms.size() == 2)
                {
                    // If we have two atoms, then we have the DIE offset and
                    // the type flags so we can find the objective C class
                    // efficiently.
                    DWARFMappedHash::ExtractTypesFromDIEArray (die_info_array, 
                                                               UINT32_MAX,
                                                               eTypeFlagNameIsFullyQualified | eTypeFlagClassIsImplementation,
                                                               die_offsets);
                }
                else
                {
                    // WE don't have the type flags, just return everything
                    DWARFMappedHash::ExtractDIEArray (die_info_array, die_offsets);
                }
            }
            return die_offsets.size();
        }

        size_t 
        FindByName (const char *name, DIEInfoArray &die_info_array)
        {
            Pair kv_pair;
            size_t old_size = die_info_array.size();
            if (Find (name, kv_pair))
            {
                die_info_array.swap(kv_pair.value);
                return die_info_array.size() - old_size;
            }
            return 0;
        }
        
    protected:
        const lldb_private::DataExtractor &m_data;
        const lldb_private::DataExtractor &m_string_table;
        std::string m_name;
    };
};


#endif  // SymbolFileDWARF_HashedNameToDIE_h_
