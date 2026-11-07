#pragma once
#include "common/PyFunctionsAddress.h"
#include "common/Arm64SignatureScanner.h"
#include "Python3105Dispatch.h"
#include <array>
#include <cstring>
#include <limits>

namespace local_python::abi {

struct Targets {
    uintptr_t marshalRead=0,evalCode=0,addModule=0,moduleDict=0,gilEnsure=0,gilRelease=0;
    uintptr_t decRef=0,errorFetch=0,errorClear=0,objectStr=0,stringData=0,initialized=0;
    uintptr_t codeNew=0,stringNew=0,unicodeNew=0,intNew=0,longNew=0,floatNew=0;
    uintptr_t tupleNew=0,tupleSet=0,frozenNew=0,getAttr=0,errorNormalize=0;
};

template<class ResolveCode,class ResolveData,class Read>
bool Verify(uintptr_t base,Targets* output,ResolveCode code,ResolveData data,Read read) noexcept {
    if (!base || !output) return false;
    *output={};
    std::array<uintptr_t,30> resolved{};
    const std::array<uintptr_t,30> fields{{
        PyFunctionsAddress::PythonApi_marshalRead,PyFunctionsAddress::PythonApi_evalCode,
        PyFunctionsAddress::PythonApi_addModule,PyFunctionsAddress::PythonApi_moduleDict,
        PyFunctionsAddress::PythonApi_gilEnsure,PyFunctionsAddress::PythonApi_gilRelease,
        PyFunctionsAddress::PythonApi_decRef,PyFunctionsAddress::PythonApi_errorFetch,
        PyFunctionsAddress::PythonApi_errorClear,PyFunctionsAddress::PythonApi_objectStr,
        PyFunctionsAddress::PythonApi_stringData,PyFunctionsAddress::PythonApi_initialized,
        PyFunctionsAddress::PythonApi_frameWitness,PyFunctionsAddress::PythonApi_codeCtorWitness,
        PyFunctionsAddress::PythonApi_readerWitness,PyFunctionsAddress::PythonApi_evalExWitness,
        PyFunctionsAddress::PythonApi_dispatch,PyFunctionsAddress::PythonApi_initializeWitness,
        PyFunctionsAddress::PythonApi_stringNew,PyFunctionsAddress::PythonApi_unicodeNew,
        PyFunctionsAddress::PythonApi_intNew,PyFunctionsAddress::PythonApi_longNew,PyFunctionsAddress::PythonApi_floatNew,
        PyFunctionsAddress::PythonApi_tupleNew,PyFunctionsAddress::PythonApi_tupleSet,PyFunctionsAddress::PythonApi_frozenNew,
        PyFunctionsAddress::PythonApi_getAttr,PyFunctionsAddress::PythonApi_errorNormalize,
        PyFunctionsAddress::PythonApi_unicodeStatefulWitness,PyFunctionsAddress::PythonApi_frozenCtorWitness
    }};
    for (size_t i=0;i<fields.size();++i) {
        const bool valid=i==11 ? data(fields[i],1,&resolved[i]) :
            i==16 ? data(fields[i],sizeof(kDispatch),&resolved[i]) : code(fields[i],&resolved[i]);
        if(!valid) return false;
    }
    const auto call=[&](uintptr_t owner,uintptr_t offset,uintptr_t callee,bool tail=false) {
        if(offset>0x10000U || offset>UINTPTR_MAX-owner || offset%4U) return false;
        const uintptr_t pc=owner+offset;
        uint32_t word=0;
        if(!read(pc,&word,sizeof(word)) || (word&0xfc000000U)!=(tail?0x14000000U:0x94000000U)) return false;
        const int64_t displacement=(word&0x02000000U ? int64_t(word&0x03ffffffU)-(1LL<<26) : int64_t(word&0x03ffffffU))*4;
        if(displacement<0 && uint64_t(-displacement)>pc) return false;
        if(displacement>=0 && uint64_t(displacement)>UINTPTR_MAX-pc) return false;
        return (displacement<0 ? pc-uintptr_t(-displacement) : pc+uintptr_t(displacement))==callee;
    };
    if(!call(resolved[0],PyFunctionsAddress::PythonApi_marshalObjectCall,resolved[14]) ||
       !call(resolved[14],PyFunctionsAddress::PythonApi_objectCodeCtorCall,resolved[13]) ||
       !call(resolved[1],PyFunctionsAddress::PythonApi_evalCodeExCall,resolved[15]) ||
       !call(resolved[15],PyFunctionsAddress::PythonApi_evalFrameCall,resolved[12]) ||
       !call(resolved[19],PyFunctionsAddress::PythonApi_unicodeTailOffset,resolved[28],true) ||
       !call(resolved[25],PyFunctionsAddress::PythonApi_frozenTailOffset,resolved[29],true)) return false;
    const uintptr_t pageOffset=PyFunctionsAddress::PythonApi_dispatchPageOffset;
    const uintptr_t addOffset=PyFunctionsAddress::PythonApi_dispatchAddOffset;
    if(pageOffset>0x10000U || addOffset!=pageOffset+4U || addOffset>UINTPTR_MAX-resolved[12]) return false;
    uint32_t page=0,add=0;
    uintptr_t dispatch=0;
    if(!read(resolved[12]+pageOffset,&page,4) || !read(resolved[12]+addOffset,&add,4) ||
       !arm64_signature::DecodeAdrpReference(resolved[12]+pageOffset,page,add,&dispatch) ||
       dispatch!=resolved[16]) return false;
    std::array<uint8_t,sizeof(kDispatch)> bytes{};
    if(!read(dispatch,bytes.data(),bytes.size()) || std::memcmp(bytes.data(),kDispatch,bytes.size())) return false;
    *output={resolved[0],resolved[1],resolved[2],resolved[3],resolved[4],resolved[5],
             resolved[6],resolved[7],resolved[8],resolved[9],resolved[10],resolved[11],resolved[13],
             resolved[18],resolved[19],resolved[20],resolved[21],resolved[22],resolved[23],resolved[24],
             resolved[25],resolved[26],resolved[27]};
    return true;
}
}
