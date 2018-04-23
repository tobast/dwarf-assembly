#include "DwarfReader.hpp"

#include <fstream>
#include <fileno.hpp>
#include <set>

using namespace std;
using namespace dwarf;

typedef std::set<std::pair<int, core::FrameSection::register_def> >
    dwarfpp_row_t;

DwarfReader::DwarfReader(const string& path):
    root(fileno(ifstream(path)))
{}

SimpleDwarf DwarfReader::read() const {
    const core::FrameSection& fs = root.get_frame_section();
    SimpleDwarf output;

    for(auto fde_it = fs.fde_begin(); fde_it != fs.fde_end(); ++fde_it) {
        SimpleDwarf::Fde parsed_fde = read_fde(*fde_it);
        output.fde_list.push_back(parsed_fde);
    }

    return output;
}

SimpleDwarf::Fde DwarfReader::read_fde(const core::Fde& fde) const {
    SimpleDwarf::Fde output;
    output.beg_ip = fde.get_low_pc();
    output.end_ip = fde.get_low_pc() + fde.get_func_length();

    auto rows = fde.decode().rows;
    const core::Cie& cie = *fde.find_cie();
    int ra_reg = cie.get_return_address_register_rule();

    for(const auto row_pair: rows) {
        SimpleDwarf::DwRow cur_row;

        cur_row.ip = row_pair.first.lower();

        const dwarfpp_row_t& row = row_pair.second;

        for(const auto& cell: row) {
            if(cell.first == DW_FRAME_CFA_COL3) {
                cur_row.cfa = read_register(cell.second);
            }
            else {
                try {
                    SimpleDwarf::MachineRegister reg_type =
                        from_dwarfpp_reg(cell.first, ra_reg);
                    switch(reg_type) {
                        case SimpleDwarf::REG_RBP:
                            cur_row.rbp = read_register(cell.second);
                            break;
                        case SimpleDwarf::REG_RA:
                            cur_row.ra = read_register(cell.second);
                            break;
                        default:
                            break;
                    }
                }
                catch(UnsupportedRegister) {} // Just ignore it.
            }
        }

        if(cur_row.cfa.type == SimpleDwarf::DwRegister::REG_UNDEFINED)
        {
            // Not set
            throw InvalidDwarf();
        }

        output.rows.push_back(cur_row);
    }

    return output;
}


SimpleDwarf::DwRegister DwarfReader::read_register(
        const core::FrameSection::register_def& reg
        ) const
{
    SimpleDwarf::DwRegister output;

    switch(reg.k) {
        case core::FrameSection::register_def::REGISTER:
            output.type = SimpleDwarf::DwRegister::REG_REGISTER;
            output.offset = reg.register_plus_offset_r().second;
            output.reg = from_dwarfpp_reg(
                    reg.register_plus_offset_r().first);
            break;

        case core::FrameSection::register_def::SAVED_AT_OFFSET_FROM_CFA:
            output.type = SimpleDwarf::DwRegister::REG_CFA_OFFSET;
            output.offset = reg.saved_at_offset_from_cfa_r();
            break;

        case core::FrameSection::register_def::INDETERMINATE:
        case core::FrameSection::register_def::UNDEFINED:
            output.type = SimpleDwarf::DwRegister::REG_UNDEFINED;
            break;

        default:
            output.type = SimpleDwarf::DwRegister::REG_NOT_IMPLEMENTED;
            break;
    }

    return output;
}

SimpleDwarf::MachineRegister DwarfReader::from_dwarfpp_reg(
        int reg_id,
        int ra_reg
        ) const
{
    if(reg_id == ra_reg)
        return SimpleDwarf::REG_RA;
    switch(reg_id) {
        case lib::DWARF_X86_64_RIP:
            return SimpleDwarf::REG_RIP;
        case lib::DWARF_X86_64_RSP:
            return SimpleDwarf::REG_RSP;
        case lib::DWARF_X86_64_RBP:
            return SimpleDwarf::REG_RBP;
        default:
            throw UnsupportedRegister();
    }
}
