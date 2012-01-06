//===-- DWARFCallFrameInfo.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DWARFCallFrameInfo_h_
#define liblldb_DWARFCallFrameInfo_h_

#include <map>

#include "lldb/lldb-private.h"
#include "lldb/Core/DataExtractor.h"
#include "lldb/Core/Flags.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Core/VMRange.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Symbol/ObjectFile.h"

namespace lldb_private {

// DWARFCallFrameInfo is a class which can read eh_frame and DWARF
// Call Frame Information FDEs.  It stores little information internally.
// Only two APIs are exported - one to find the high/low pc values
// of a function given a text address via the information in the
// eh_frame / debug_frame, and one to generate an UnwindPlan based
// on the FDE in the eh_frame / debug_frame section.

class DWARFCallFrameInfo
{
public:

    DWARFCallFrameInfo (ObjectFile& objfile, 
                        lldb::SectionSP& section, 
                        lldb::RegisterKind reg_kind, 
                        bool is_eh_frame);

    ~DWARFCallFrameInfo();

    // Locate an AddressRange that includes the provided Address in this 
    // object's eh_frame/debug_info
    // Returns true if a range is found to cover that address.
    bool
    GetAddressRange (Address addr, AddressRange &range);

    // Return an UnwindPlan based on the call frame information encoded 
    // in the FDE of this DWARFCallFrameInfo section.
    bool
    GetUnwindPlan (Address addr, UnwindPlan& unwind_plan);


private:
    enum
    {
        CFI_AUG_MAX_SIZE = 8,
        CFI_HEADER_SIZE = 8
    };

    struct CIE
    {
        dw_offset_t cie_offset;
        uint8_t     version;
        char        augmentation[CFI_AUG_MAX_SIZE];  // This is typically empty or very short.
        uint32_t    code_align;
        int32_t     data_align;
        uint32_t    return_addr_reg_num;
        dw_offset_t inst_offset;        // offset of CIE instructions in mCFIData
        uint32_t    inst_length;        // length of CIE instructions in mCFIData
        uint8_t     ptr_encoding;
        lldb_private::UnwindPlan::Row initial_row;

        CIE(dw_offset_t offset) : cie_offset(offset), version (-1), code_align (0),
                                  data_align (0), return_addr_reg_num (-1), inst_offset (0),
                                  inst_length (0), ptr_encoding (0), initial_row() {}
    };

    typedef lldb::SharedPtr<CIE>::Type CIESP;

    struct FDEEntry
    {
        AddressRange bounds;   // function bounds
        dw_offset_t offset;    // offset to this FDE within the Section

        FDEEntry () : bounds (), offset (0) { }

        inline bool
        operator<(const DWARFCallFrameInfo::FDEEntry& b) const
        {
            if (bounds.GetBaseAddress().GetOffset() < b.bounds.GetBaseAddress().GetOffset())
                return true;
            else
                return false;
        }
    };

    typedef std::map<off_t, CIESP> cie_map_t;

    bool
    IsEHFrame() const;

    bool
    GetFDEEntryByAddress (Address addr, FDEEntry& fde_entry);

    void
    GetFDEIndex ();

    bool
    FDEToUnwindPlan (uint32_t offset, Address startaddr, UnwindPlan& unwind_plan);

    const CIE* 
    GetCIE(dw_offset_t cie_offset);
    
    void
    GetCFIData();

    ObjectFile&                 m_objfile;
    lldb::SectionSP             m_section;
    lldb::RegisterKind          m_reg_kind;
    Flags                       m_flags;
    cie_map_t                   m_cie_map;

    DataExtractor               m_cfi_data;
    bool                        m_cfi_data_initialized;   // only copy the section into the DE once

    std::vector<FDEEntry>       m_fde_index;
    bool                        m_fde_index_initialized;  // only scan the section for FDEs once

    bool                        m_is_eh_frame;

    CIESP
    ParseCIE (const uint32_t cie_offset);

};

} // namespace lldb_private

#endif  // liblldb_DWARFCallFrameInfo_h_
