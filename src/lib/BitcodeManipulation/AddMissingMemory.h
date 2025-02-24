#pragma once

#include <llvm/IR/Module.h>
#include <vector>
#include <cstdint>
#include "../Prebuilt/Utils.h"

namespace BitcodeManipulation {

bool AddMissingMemory(llvm::Module &M, uint64_t addr, const std::vector<uint8_t>& page);

} // namespace BitcodeManipulation 