/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/
#ifndef GUARD_MIOPEN_DRIVER_HPP
#define GUARD_MIOPEN_DRIVER_HPP

#include <half/half.hpp>
#include "random.hpp"

#include "InputFlags.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cfloat>
#include <memory>
#include <miopen/logger.hpp>
#include <miopen/miopen.h>
#include <miopen/bfloat16.hpp>
using half         = half_float::half;
using hip_bfloat16 = bfloat16;
#include <hip_float8.hpp>
using float16 = half_float::half;
using float8  = miopen_f8::hip_f8<miopen_f8::hip_f8_type::fp8>;
using bfloat8 = miopen_f8::hip_f8<miopen_f8::hip_f8_type::bf8>;
#include <numeric>
#include <vector>

#if MIOPEN_BACKEND_OPENCL
#if defined(__APPLE__) || defined(__MACOSX)
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif
#elif MIOPEN_BACKEND_HIP
#include <hip/hip_runtime_api.h>
#endif

#define UNPACK_VEC4(v) (v[0]), (v[1]), (v[2]), (v[3])

// Use values which are distinctively greater then miopenStatus_t,
// so that these can be ORed with any miopen status code
// without loss of information.
typedef enum
{
    // These four codes could be returned together, ORed:
    EC_VerifyFwd     = 0x100,
    EC_VerifyBwd     = 0x200,
    EC_VerifyWrw     = 0x400,
    EC_VerifyBwdBias = 0x800,
} errorCode_t;

struct GPUMem
{

#if MIOPEN_BACKEND_OPENCL
    GPUMem(){};
    GPUMem(cl_context& ctx, size_t psz, size_t pdata_sz) : sz(psz), data_sz(pdata_sz)
    {
        buf = clCreateBuffer(ctx, CL_MEM_READ_WRITE, data_sz * sz, nullptr, nullptr);
    }

    int ToGPU(cl_command_queue& q, void* p) const
    {
        return clEnqueueWriteBuffer(q, buf, CL_TRUE, 0, data_sz * sz, p, 0, nullptr, nullptr);
    }
    int FromGPU(cl_command_queue& q, void* p) const
    {
        return clEnqueueReadBuffer(q, buf, CL_TRUE, 0, data_sz * sz, p, 0, nullptr, nullptr);
    }

    cl_mem GetMem() const { return buf; }
    size_t GetSize() const { return sz * data_sz; }

    ~GPUMem() { clReleaseMemObject(buf); }

    cl_mem buf;
    size_t sz;
    size_t data_sz;

#elif MIOPEN_BACKEND_HIP

    GPUMem(){};
    GPUMem(uint32_t ctx, size_t psz, size_t pdata_sz) : _ctx(ctx), sz(psz), data_sz(pdata_sz)
    {
        auto status = hipMalloc(static_cast<void**>(&buf), GetSize());
        if(status != hipSuccess)
            MIOPEN_THROW_HIP_STATUS(status,
                                    "[MIOpenDriver] hipMalloc " + std::to_string(GetSize()));
        MIOPEN_LOG_CUSTOM(miopen::LoggingLevel::Info2,
                          "MIOpenDriver",
                          "hipMalloc " << GetSize() << " at " << buf << " Ok");
    }

    int ToGPU(hipStream_t q, void* p)
    {
        _q = q;
        return static_cast<int>(hipMemcpy(buf, p, GetSize(), hipMemcpyHostToDevice));
    }
    int FromGPU(hipStream_t q, void* p)
    {
        hipDeviceSynchronize();
        _q = q;
        return static_cast<int>(hipMemcpy(p, buf, GetSize(), hipMemcpyDeviceToHost));
    }

    void* GetMem() { return buf; }
    size_t GetSize() { return sz * data_sz; }

    ~GPUMem()
    {
        size_t size = 0;
        auto status = hipMemPtrGetInfo(buf, &size);
        if(status != hipSuccess)
            MIOPEN_LOG_CUSTOM(miopen::LoggingLevel::Warning,
                              "MIOpenDriver",
                              "hipMemPtrGetInfo at " << buf << ' '
                                                     << miopen::HIPErrorMessage(status, ""));
        status = hipFree(buf);
        if(status != hipSuccess)
            MIOPEN_LOG_CUSTOM(miopen::LoggingLevel::Error,
                              "MIOpenDriver",
                              "hipFree " << size << " at " << buf << ' '
                                         << miopen::HIPErrorMessage(status, ""));
        else
            MIOPEN_LOG_CUSTOM(miopen::LoggingLevel::Info2,
                              "MIOpenDriver",
                              "hipFree " << size << " at " << buf << " Ok");
    }

    hipStream_t _q; // Place holder for opencl context
    uint32_t _ctx;
    void* buf;
    size_t sz;
    size_t data_sz;
#endif
};

inline void PadBufferSize(size_t& sz, int datatype_sz)
{
    size_t page_sz = (2 * 1024 * 1024) / datatype_sz;
    if(sz % page_sz != 0)
    {
        sz = ((sz + page_sz) / page_sz) * page_sz;
    }
}

[[noreturn]] inline void Usage()
{
    printf("Usage: ./driver *base_arg* *other_args*\n");
    printf("Supported Base Arguments: conv[fp16|int8|bfp16|fp8|bfp8], CBAInfer[fp16], "
           "pool[fp16], lrn[fp16], "
           "activ[fp16], softmax[fp16], bnorm[fp16], rnn[fp16], gemm[fp16], ctc, dropout[fp16], "
           "tensorop[fp16], reduce[fp16|fp64], layernorm[bfp16|fp16], sum[bfp16|fp16], "
           "groupnorm[bfp16|fp16], cat[bfp16|fp16], addlayernorm[bfp16|fp16], "
           "t5layernorm[bfp16|fp16], adam[fp16], ampadam, reduceextreme[bfp16|fp16], "
           "adamw[fp16], ampadamw, transformersadamw[fp16], transformersampadamw\n");
    exit(0); // NOLINT (concurrency-mt-unsafe)
}

inline std::string ParseBaseArg(int argc, char* argv[])
{
    if(argc < 2)
    {
        printf("FAILED: Invalid Number of Input Arguments\n");
        Usage();
    }

    std::string arg = argv[1];

    if(arg != "conv" && arg != "convfp16" && arg != "convint8" && arg != "convbfp16" &&
       arg != "convfp8" && arg != "convbfp8" && arg != "CBAInfer" && arg != "CBAInferfp16" &&
       arg != "pool" && arg != "poolfp16" && arg != "lrn" && arg != "lrnfp16" && arg != "activ" &&
       arg != "activfp16" && arg != "softmax" && arg != "softmaxfp16" && arg != "bnorm" &&
       arg != "bnormfp16" && arg != "rnn" && arg != "rnnfp16" && arg != "rnn_seq" &&
       arg != "rnn_seqfp16" && arg != "gemm" && arg != "gemmfp16" && arg != "ctc" &&
       arg != "dropout" && arg != "dropoutfp16" && arg != "tensorop" && arg != "tensoropfp16" &&
       arg != "reduce" && arg != "reducefp16" && arg != "reducefp64" && arg != "layernorm" &&
       arg != "layernormfp16" && arg != "layernormbfp16" && arg != "sum" && arg != "sumfp16" &&
       arg != "sumbfp16" && arg != "groupnorm" && arg != "groupnormfp16" &&
       arg != "groupnormbfp16" && arg != "cat" && arg != "catfp16" && arg != "catbfp16" &&
       arg != "addlayernorm" && arg != "addlayernormfp16" && arg != "addlayernormbfp16" &&
       arg != "t5layernorm" && arg != "t5layernormfp16" && arg != "t5layernormbfp16" &&
       arg != "adam" && arg != "adamfp16" && arg != "ampadam" && arg != "reduceextreme" &&
       arg != "reduceextremefp16" && arg != "reduceextremebfp16" && arg != "adamw" &&
       arg != "adamwfp16" && arg != "ampadamw" && arg != "transformersadamw" &&
       arg != "transformersadamwfp16" && arg != "transformersampadamw" && arg != "--version")
    {
        printf("FAILED: Invalid Base Input Argument\n");
        Usage();
    }
    else if(arg == "-h" || arg == "--help" || arg == "-?")
        Usage();
    else
        return arg;
}

class Driver
{
public:
    Driver()
    {
        data_type = miopenFloat;
#if MIOPEN_BACKEND_OPENCL
        miopenCreate(&handle);
#elif MIOPEN_BACKEND_HIP
        hipStream_t s;
        hipStreamCreate(&s);
        miopenCreateWithStream(&handle, s);
#endif

        miopenGetStream(handle, &q);
    }

    miopenHandle_t GetHandle() { return handle; }
    miopenDataType_t GetDataType() { return data_type; }

#if MIOPEN_BACKEND_OPENCL
    cl_command_queue& GetStream() { return q; }
#elif MIOPEN_BACKEND_HIP
    hipStream_t& GetStream() { return q; }
#endif
    virtual ~Driver() { miopenDestroy(handle); }

    // TODO: add timing APIs
    virtual int AddCmdLineArgs()                         = 0;
    virtual int ParseCmdLineArgs(int argc, char* argv[]) = 0;
    virtual InputFlags& GetInputFlags()                  = 0;
    virtual int GetandSetData()                          = 0;
    virtual int AllocateBuffersAndCopy()                 = 0;
    virtual int RunForwardGPU()                          = 0;
    virtual int VerifyForward()                          = 0;
    virtual int RunBackwardGPU()                         = 0;
    virtual int VerifyBackward()                         = 0;

protected:
    template <typename Tgpu>
    void InitDataType();
    miopenHandle_t handle;
    miopenDataType_t data_type;

#if MIOPEN_BACKEND_OPENCL
    cl_command_queue q;
#elif MIOPEN_BACKEND_HIP
    hipStream_t q;
#endif
};

template <>
inline void Driver::InitDataType<int8_t>()
{
    data_type = miopenInt8;
}
template <>
inline void Driver::InitDataType<float>()
{
    data_type = miopenFloat;
}
template <>
inline void Driver::InitDataType<float16>()
{
    data_type = miopenHalf;
}
template <>
inline void Driver::InitDataType<bfloat16>()
{
    data_type = miopenBFloat16;
}
template <>
inline void Driver::InitDataType<float8>()
{
    data_type = miopenFloat8;
}
template <>
inline void Driver::InitDataType<bfloat8>()
{
    data_type = miopenBFloat8;
}
// "std::is_same<Tgpu, float>{}" used to avoid "static_assert" compilation error,
// which occurs when the condition does not depend in any way on the template parameters.
template <typename Tgpu>
inline void Driver::InitDataType()
{
    static_assert(std::is_same<Tgpu, float>{}, "unsupported Tgpu");
}

template <typename T>
inline std::ostream& operator<<(std::ostream& os, const std::vector<T>& vs)
{
    os << "{ size: " << vs.size() << ", entries: ";
    for(auto& v : vs)
        os << v << " ";
    os << "}";
    return os;
}

#endif // GUARD_MIOPEN_DRIVER_HPP
