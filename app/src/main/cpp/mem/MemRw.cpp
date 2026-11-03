#include "mem/MemRw.h"

#include <link.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstring>
#include <string>

namespace texinject::mem
{
    namespace
    {
        constexpr std::size_t kMaxChunk = 1 << 20;   // 单次最多 1MB，避免一次爆栈

        bool VmRead(uintptr_t Address, void* Out, std::size_t Length)
        {
            struct iovec Local;
            Local.iov_base = Out;
            Local.iov_len = Length;
            struct iovec Remote;
            Remote.iov_base = reinterpret_cast<void*>(Address);
            Remote.iov_len = Length;
            const ssize_t Got = process_vm_readv(getpid(), &Local, 1, &Remote, 1, 0);
            return Got == static_cast<ssize_t>(Length);
        }

        bool VmWrite(uintptr_t Address, const void* Data, std::size_t Length)
        {
            struct iovec Local;
            Local.iov_base = const_cast<void*>(Data);
            Local.iov_len = Length;
            struct iovec Remote;
            Remote.iov_base = reinterpret_cast<void*>(Address);
            Remote.iov_len = Length;
            const ssize_t Put = process_vm_writev(getpid(), &Local, 1, &Remote, 1, 0);
            return Put == static_cast<ssize_t>(Length);
        }

        struct FindContext
        {
            const char* Name;
            uintptr_t Base;
        };

        int PhdrCallback(struct dl_phdr_info* Info, std::size_t, void* Data)
        {
            auto* Context = static_cast<FindContext*>(Data);
            if (!Info || !Info->dlpi_name || Info->dlpi_name[0] == '\0')
            {
                return 0;
            }
            const char* Slash = std::strrchr(Info->dlpi_name, '/');
            const char* Base = Slash ? Slash + 1 : Info->dlpi_name;
            if (std::strcmp(Base, Context->Name) == 0)
            {
                Context->Base = static_cast<uintptr_t>(Info->dlpi_addr);
                return 1;   // 找到就停
            }
            return 0;
        }
    }

    bool ReadRaw(uintptr_t Address, void* Out, std::size_t Length)
    {
        if (Address == 0 || Out == nullptr || Length == 0)
        {
            return false;
        }
        auto* Cursor = static_cast<uint8_t*>(Out);
        std::size_t Done = 0;
        while (Done < Length)
        {
            const std::size_t Step = (Length - Done) > kMaxChunk ? kMaxChunk : (Length - Done);
            if (!VmRead(Address + Done, Cursor + Done, Step))
            {
                return false;
            }
            Done += Step;
        }
        return true;
    }

    bool WriteRaw(uintptr_t Address, const void* Data, std::size_t Length)
    {
        if (Address == 0 || Data == nullptr || Length == 0)
        {
            return false;
        }
        const auto* Cursor = static_cast<const uint8_t*>(Data);
        std::size_t Done = 0;
        while (Done < Length)
        {
            const std::size_t Step = (Length - Done) > kMaxChunk ? kMaxChunk : (Length - Done);
            if (!VmWrite(Address + Done, Cursor + Done, Step))
            {
                return false;
            }
            Done += Step;
        }
        return true;
    }

    uintptr_t ModuleBase(const char* Name)
    {
        if (!Name || !*Name)
        {
            return 0;
        }
        FindContext Context{ Name, 0 };
        dl_iterate_phdr(PhdrCallback, &Context);
        return Context.Base;
    }

    // ------------------------------------------------------------------ 便捷读写

    bool ReadU8(uintptr_t Address, uint8_t& Out) { return ReadRaw(Address, &Out, sizeof(Out)); }
    bool ReadI32(uintptr_t Address, int32_t& Out) { return ReadRaw(Address, &Out, sizeof(Out)); }
    bool ReadI64(uintptr_t Address, int64_t& Out) { return ReadRaw(Address, &Out, sizeof(Out)); }
    bool ReadF32(uintptr_t Address, float& Out) { return ReadRaw(Address, &Out, sizeof(Out)); }
    bool ReadF64(uintptr_t Address, double& Out) { return ReadRaw(Address, &Out, sizeof(Out)); }
    bool ReadPtr(uintptr_t Address, uintptr_t& Out) { return ReadRaw(Address, &Out, sizeof(Out)); }

    bool WriteU8(uintptr_t Address, uint8_t Value) { return WriteRaw(Address, &Value, sizeof(Value)); }
    bool WriteI32(uintptr_t Address, int32_t Value) { return WriteRaw(Address, &Value, sizeof(Value)); }
    bool WriteI64(uintptr_t Address, int64_t Value) { return WriteRaw(Address, &Value, sizeof(Value)); }
    bool WriteF32(uintptr_t Address, float Value) { return WriteRaw(Address, &Value, sizeof(Value)); }
    bool WriteF64(uintptr_t Address, double Value) { return WriteRaw(Address, &Value, sizeof(Value)); }
    bool WritePtr(uintptr_t Address, uintptr_t Value) { return WriteRaw(Address, &Value, sizeof(Value)); }

    bool ReadCString(uintptr_t Address, std::string& Out, std::size_t Max)
    {        Out.clear();
        if (Address == 0 || Max == 0)
        {
            return false;
        }
        char Buffer[256];
        std::size_t Total = 0;
        while (Total < Max)
        {
            const std::size_t Step = (Max - Total) > sizeof(Buffer) ? sizeof(Buffer) : (Max - Total);
            if (!VmRead(Address + Total, Buffer, Step))
            {
                return false;
            }
            for (std::size_t Index = 0; Index < Step; ++Index)
            {
                if (Buffer[Index] == '\0')
                {
                    Out.append(Buffer, Index);
                    return true;
                }
            }
            Out.append(Buffer, Step);
            Total += Step;
        }
        return true;
    }
}

namespace texinject::mem
{
    bool Readable(uintptr_t Address)
    {
        if (Address < 0x1000)
        {
            return false;
        }
        uint32_t Probe = 0;
        return VmRead(Address, &Probe, sizeof(Probe));
    }
}
