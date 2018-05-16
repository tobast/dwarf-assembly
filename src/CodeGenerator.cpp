#include "CodeGenerator.hpp"
#include "gen_context_struct.hpp"
#include "settings.hpp"

#include <algorithm>
#include <limits>

using namespace std;

static const char* PRELUDE =
"#include <assert.h>\n"
"\n"
;

CodeGenerator::CodeGenerator(
        const SimpleDwarf& dwarf,
        std::ostream& os,
        NamingScheme naming_scheme):
    dwarf(dwarf), os(os), pc_list(nullptr), naming_scheme(naming_scheme)
{
    if(!settings::pc_list.empty()) {
        pc_list = make_unique<PcListReader>(settings::pc_list);
        pc_list->read();
    }
}

void CodeGenerator::generate() {
    gen_of_dwarf();
}

void CodeGenerator::gen_of_dwarf() {
    os << CONTEXT_STRUCT_STR << '\n'
       << PRELUDE << '\n' << endl;

    switch(settings::switch_generation_policy) {
        case settings::SGP_SwitchPerFunc:
        {
            vector<LookupEntry> lookup_entries;

            // A function per FDE
            for(const auto& fde: dwarf.fde_list) {
                LookupEntry cur_entry;
                cur_entry.name = naming_scheme(fde);
                cur_entry.beg = fde.beg_ip;
                cur_entry.end = fde.end_ip;
                lookup_entries.push_back(cur_entry);

                gen_function_of_fde(fde);
                os << endl;
            }

            gen_lookup(lookup_entries);
            break;
        }
        case settings::SGP_GlobalSwitch:
        {
            gen_unwind_func_header("_eh_elf");
            for(const auto& fde: dwarf.fde_list) {
                gen_switchpart_of_fde(fde);
            }
            gen_unwind_func_footer();
            break;
        }
    }
}

void CodeGenerator::gen_unwind_func_header(const std::string& name) {
    os << "unwind_context_t "
       << name
       << "(unwind_context_t ctx, uintptr_t pc) {\n"
       << "\tunwind_context_t out_ctx;\n"
       << "\tswitch(pc) {" << endl;
}

void CodeGenerator::gen_unwind_func_footer() {
    os << "\t\tdefault: assert(0);\n"
       << "\t}\n"
       << "}" << endl;
}

void CodeGenerator::gen_function_of_fde(const SimpleDwarf::Fde& fde) {
    gen_unwind_func_header(naming_scheme(fde));

    gen_switchpart_of_fde(fde);

    gen_unwind_func_footer();
}

void CodeGenerator::gen_switchpart_of_fde(const SimpleDwarf::Fde& fde) {
    os << "\t\t/********** FDE: 0x" << std::hex << fde.fde_offset
       << ", PC = 0x" << fde.beg_ip << std::dec << " */" << std::endl;
    for(size_t fde_row_id=0; fde_row_id < fde.rows.size(); ++fde_row_id)
    {
        uintptr_t up_bound = fde.end_ip - 1;
        if(fde_row_id != fde.rows.size() - 1)
            up_bound = fde.rows[fde_row_id + 1].ip - 1;

        gen_of_row(fde.rows[fde_row_id], up_bound);
    }
}

void CodeGenerator::gen_of_row(
        const SimpleDwarf::DwRow& row,
        uintptr_t row_end)
{
    gen_case(row.ip, row_end);

    os << "\t\t\t" << "out_ctx.rsp = ";
    gen_of_reg(row.cfa);
    os << ';' << endl;

    os << "\t\t\t" << "out_ctx.rbp = ";
    gen_of_reg(row.rbp);
    os << ';' << endl;

    os << "\t\t\t" << "out_ctx.rip = ";
    gen_of_reg(row.ra);
    os << ';' << endl;

    os << "\t\t\treturn " << "out_ctx" << ";" << endl;
}

void CodeGenerator::gen_case(uintptr_t low_bound, uintptr_t high_bound) {
    if(pc_list == nullptr) {
        os << "\t\tcase " << std::hex << "0x" << low_bound
           << " ... 0x" << high_bound << ":" << std::dec << endl;
    }
    else {
        const auto& first_it = lower_bound(
                pc_list->get_list().begin(),
                pc_list->get_list().end(),
                low_bound);
        const auto& last_it = upper_bound(
                pc_list->get_list().begin(),
                pc_list->get_list().end(),
                high_bound);

        if(first_it == pc_list->get_list().end())
            throw CodeGenerator::InvalidPcList();

        os << std::hex;
        for(auto it = first_it; it != last_it; ++it)
            os << "\t\tcase 0x" << *it << ":\n";
        os << std::dec;
    }
}

static const char* ctx_of_dw_name(SimpleDwarf::MachineRegister reg) {
    switch(reg) {
        case SimpleDwarf::REG_RIP:
            throw CodeGenerator::NotImplementedCase();
        case SimpleDwarf::REG_RSP:
            return "ctx.rsp";
        case SimpleDwarf::REG_RBP:
            return "ctx.rbp";
        case SimpleDwarf::REG_RA:
            throw CodeGenerator::NotImplementedCase();
    }
    return "";
}

void CodeGenerator::gen_of_reg(const SimpleDwarf::DwRegister& reg) {
    switch(reg.type) {
        case SimpleDwarf::DwRegister::REG_UNDEFINED:
            os << std::numeric_limits<uintptr_t>::max() << "ull";
            break;
        case SimpleDwarf::DwRegister::REG_REGISTER:
            os << ctx_of_dw_name(reg.reg)
                << " + (" << reg.offset << ")";
            break;
        case SimpleDwarf::DwRegister::REG_CFA_OFFSET:
            os << "*((uintptr_t*)(out_ctx.rsp + ("
               << reg.offset
               << ")))";
            break;
        case SimpleDwarf::DwRegister::REG_NOT_IMPLEMENTED:
            os << "0; assert(0)";
            break;
    }
}

void CodeGenerator::gen_lookup(const std::vector<LookupEntry>& entries) {
    os << "_fde_func_t _fde_lookup(uintptr_t pc) {\n"
       << "\tswitch(pc) {" << endl;
    for(const auto& entry: entries) {
        os << "\t\tcase " << std::hex << "0x" << entry.beg
           << " ... 0x" << entry.end - 1 << ":\n" << std::dec
           << "\t\t\treturn &" << entry.name << ";" << endl;
    }
    os << "\t\tdefault: assert(0);\n"
       << "\t}\n"
       << "}" << endl;
}
