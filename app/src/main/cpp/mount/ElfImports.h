#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace resource_mount::elf {

// ELF64 wire records, kept independent of Android headers for host fixtures.
// The adapter asserts these layouts against the platform's Elf64_* records.
struct Dynamic { int64_t tag; uint64_t value; };
struct Symbol {
    uint32_t name;
    uint8_t info, other;
    uint16_t section;
    uint64_t value, size;
};
struct Rel { uint64_t offset, info; };
struct Rela { uint64_t offset, info; int64_t addend; };
struct Segment { uintptr_t address; size_t size; uint32_t flags; };
struct Image {
    uintptr_t base = 0;
    Segment loads[32]{};
    size_t count = 0;
    uintptr_t dynamic = 0;
    size_t dynamic_size = 0;

    bool contains(uintptr_t address, size_t size, bool data_only = false) const {
        if (!size) return false;
        for (size_t i = 0; i < count; ++i) {
            const auto& s = loads[i];
            if (!(s.flags & 4U) || (data_only && (s.flags & 1U))) continue;
            if (address >= s.address && size <= s.size &&
                address - s.address <= s.size - size) return true;
        }
        return false;
    }

    uintptr_t rebase(uint64_t offset, size_t size) const {
        if (offset > std::numeric_limits<uintptr_t>::max() - base) return 0;
        const auto address = base + static_cast<uintptr_t>(offset);
        return contains(address, size) ? address : 0;
    }

    // Linkers may leave dynamic pointers image-relative or relocate them.
    uintptr_t pointer(uint64_t value, size_t size) const {
        if (contains(static_cast<uintptr_t>(value), size)) return value;
        return rebase(value, size);
    }
};

enum class Status { Missing, Found, Invalid, Ambiguous };
struct Import {
    Status status = Status::Missing;
    uintptr_t slot = 0;
    bool rela = false;
    size_t index = 0;
};

// Reader copies only ranges already checked against this module's PT_LOADs.
// Runtime: memcpy; host: file-backed virtual-address mapping. No ELF header,
// section names, RVA cache or game signature is needed by the imported-libc ABI.
template<class Reader>
Import FindSymbol(const Image& image, const char* wanted, Reader read) {
    const size_t name_size = wanted ? std::strlen(wanted) + 1 : 0;
    if (name_size < 2 || name_size > 64) return {Status::Invalid};
    constexpr int64_t Null = 0, PltSize = 2, Strings = 5, Symbols = 6;
    constexpr int64_t RelaTable = 7, RelaSize = 8, RelaEntry = 9, StringSize = 10;
    constexpr int64_t SymbolEntry = 11, RelTable = 17, RelSize = 18, RelEntry = 19;
    constexpr int64_t PltType = 20, JumpTable = 23;
    uint64_t symbols = 0, strings = 0, strings_size = 0;
    uint64_t jump = 0, jump_size = 0, jump_type = 0;
    uint64_t rela = 0, rela_size = 0, rel = 0, rel_size = 0;
    uint64_t symbol_entry = sizeof(Symbol), rela_entry = sizeof(Rela), rel_entry = sizeof(Rel);
    bool terminated = false;
    const auto invalid = [] { return Import{Status::Invalid}; };
    if (!image.contains(image.dynamic, image.dynamic_size) ||
        image.dynamic_size % sizeof(Dynamic) != 0) return invalid();
    for (size_t i = 0; i < image.dynamic_size / sizeof(Dynamic); ++i) {
        Dynamic d{};
        if (!read(image.dynamic + i * sizeof(d), &d, sizeof(d))) return invalid();
        if (d.tag == Null) { terminated = true; break; }
        switch (d.tag) {
            case Symbols: symbols = d.value; break;
            case Strings: strings = d.value; break;
            case StringSize: strings_size = d.value; break;
            case SymbolEntry: symbol_entry = d.value; break;
            case JumpTable: jump = d.value; break;
            case PltSize: jump_size = d.value; break;
            case PltType: jump_type = d.value; break;
            case RelaTable: rela = d.value; break;
            case RelaSize: rela_size = d.value; break;
            case RelaEntry: rela_entry = d.value; break;
            case RelTable: rel = d.value; break;
            case RelSize: rel_size = d.value; break;
            case RelEntry: rel_entry = d.value; break;
        }
    }
    if (!terminated || !symbols || !strings || strings_size < name_size ||
        symbol_entry != sizeof(Symbol) || rela_entry != sizeof(Rela) ||
        rel_entry != sizeof(Rel)) return invalid();
    const auto symtab = image.pointer(symbols, sizeof(Symbol));
    const auto strtab = image.pointer(strings, strings_size);
    if (!symtab || !strtab) return invalid();

    const auto search = [&](uint64_t table, uint64_t size, bool with_addend) -> Import {
        if (!size) return {};
        const size_t stride = with_addend ? sizeof(Rela) : sizeof(Rel);
        const uintptr_t start = table ? image.pointer(table, size) : 0;
        if (!start || size % stride) return invalid();
        Import found{};
        for (size_t i = 0; i < size / stride; ++i) {
            Rel r{};
            // RELA begins with the same two fields; skip its explicit addend.
            if (!read(start + i * stride, &r, sizeof(r))) return invalid();
            const auto type = static_cast<uint32_t>(r.info);
            if (type != 1026U && type != 1025U) continue; // AArch64 JUMP_SLOT / GLOB_DAT
            const uint64_t index = r.info >> 32U;
            if (!index) continue;
            if (index > (std::numeric_limits<uintptr_t>::max() - symtab) / sizeof(Symbol))
                return invalid();
            const uintptr_t address = symtab + index * sizeof(Symbol);
            Symbol s{};
            if (!image.contains(address, sizeof(s)) || !read(address, &s, sizeof(s)))
                return invalid();
            if (s.name >= strings_size) return invalid();
            if (strings_size - s.name < name_size) continue;
            char name[64]{};
            if (!read(strtab + s.name, name, name_size)) return invalid();
            if (std::memcmp(name, wanted, name_size) != 0) continue;
            const uintptr_t slot = image.rebase(r.offset, sizeof(uintptr_t));
            if (!slot || slot % alignof(uintptr_t) ||
                !image.contains(slot, sizeof(uintptr_t), true)) return invalid();
            if (found.slot && found.slot != slot) return Import{Status::Ambiguous};
            found = {Status::Found, slot, with_addend, i};
        }
        return found;
    };

    // Normal imported calls are in PLT. Avoid a second million-entry walk of
    // .rela.dyn after finding the verified callable import in this table.
    if (jump_size) {
        if (jump_type != RelaTable && jump_type != RelTable) return invalid();
        const auto result = search(jump, jump_size, jump_type == RelaTable);
        if (result.status != Status::Missing) return result;
    }
    const auto result = search(rela, rela_size, true);
    return result.status == Status::Missing ? search(rel, rel_size, false) : result;
}


} // namespace resource_mount::elf

