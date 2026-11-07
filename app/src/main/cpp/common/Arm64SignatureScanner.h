#ifndef PYLOADER_ARM64_SIGNATURE_SCANNER_H
#define PYLOADER_ARM64_SIGNATURE_SCANNER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace arm64_signature {

struct Instruction {
    uint32_t value = 0;
    uint32_t mask = 0xffffffffU;
};

struct CodeRange {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uintptr_t address = 0;
};

struct Signature {
    const Instruction* instructions = nullptr;
    size_t count = 0;

    int32_t entry_delta = 0;
};

class ScanIndex {
public:
    struct Location { size_t range; size_t offset; };
    ScanIndex() noexcept;
    ~ScanIndex();
    ScanIndex(const ScanIndex&) = delete;
    ScanIndex& operator=(const ScanIndex&) = delete;
    bool Build(const CodeRange* ranges, size_t range_count,
               const Signature* signatures, size_t signature_count) noexcept;
    bool Candidates(const CodeRange* ranges, size_t range_count,
                    const Signature& signature, size_t* anchor,
                    const std::vector<Location>** locations) const noexcept;
    size_t PositionCount() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class ScanStatus : uint8_t {
    Unique,
    NotFound,
    Ambiguous,
    InvalidInput,
};

struct ScanResult {
    ScanStatus status = ScanStatus::InvalidInput;
    uintptr_t address = 0;

    size_t matches = 0;
};

ScanResult Scan(const CodeRange* ranges, size_t range_count,
                const Signature& signature, const ScanIndex* index = nullptr) noexcept;

ScanResult ScanWithCallIdentity(const CodeRange* ranges, size_t range_count,
                               const Signature& signature, size_t branch_word,
                               const Signature& callee_body, const ScanIndex* index = nullptr) noexcept;
ScanResult ScanWithCallAddress(const CodeRange* ranges,size_t range_count,
                              const Signature& signature,size_t branch_word,
                              uintptr_t callee_address, const ScanIndex* index = nullptr) noexcept;

ScanResult ScanWithReferenceAddress(const CodeRange* ranges,size_t range_count,
                                   const Signature& signature,size_t adrp_word,
                                   size_t consumer_word,uintptr_t address, const ScanIndex* index = nullptr) noexcept;

uint32_t InstructionMask(uint32_t instruction) noexcept;

bool DecodeAdrpReference(uintptr_t adrp_address, uint32_t adrp,
                         uint32_t consumer, uintptr_t* target) noexcept;
bool DecodeAdrReference(uintptr_t instruction_address,uint32_t adr,uintptr_t* target) noexcept;

}

#endif
