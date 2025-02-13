#include "recycle.h"
#include <glog/logging.h>

extern "C" {
#define XED_DLL
#include <xed/xed-interface.h>
}

XEDDisassembler::XEDDisassembler() {
    Initialize();
}

XEDDisassembler::~XEDDisassembler() = default;

void XEDDisassembler::Initialize() {
    xed_tables_init();
}

DecodedInstruction 
XEDDisassembler::DecodeInstruction(const uint8_t* bytes, size_t max_size, uint64_t addr) {
    DecodedInstruction result;
    result.address = addr;

    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero(&xedd);
    xed_decoded_inst_set_mode(&xedd, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
    
    xed_error_enum_t error = xed_decode(&xedd, bytes, max_size);
    if (error != XED_ERROR_NONE) {
        LOG(ERROR) << "Failed to decode instruction at 0x" << std::hex << addr;
        return result;
    }

    result.length = xed_decoded_inst_get_length(&xedd);
    result.bytes = std::vector<uint8_t>(bytes, bytes + result.length);

    // Get instruction category
    xed_category_enum_t category = xed_decoded_inst_get_category(&xedd);

    result.is_branch = (category == XED_CATEGORY_COND_BR || 
                       category == XED_CATEGORY_UNCOND_BR);
    result.is_call = (category == XED_CATEGORY_CALL);
    result.is_ret = (category == XED_CATEGORY_RET);

    // Get assembly text
    char buffer[256];
    if (xed_format_context(XED_SYNTAX_INTEL, &xedd, buffer, sizeof(buffer), addr, nullptr, nullptr)) {
        result.assembly = buffer;
    } else {
        result.assembly = "<decode error>";
    }

    return result;
}

bool XEDDisassembler::IsTerminator(const DecodedInstruction& inst) const {
    return inst.is_branch || inst.is_call || inst.is_ret;
} 