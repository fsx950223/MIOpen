/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
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

#include "bn.hpp"

struct GPU_BNFwdTrain_FP16
    : BNFwdTrainTest<half_float::half, half_float::half, half_float::half, half_float::half, float>
{
};

struct GPU_BNFwdTrain_FP32 : BNFwdTrainTest<float, float, float, float, float>
{
};

struct GPU_BNFwdTrain_FP64 : BNFwdTrainTest<double, double, double, double, double>
{
};

struct GPU_BNFwdTrain_BFP16 : BNFwdTrainTest<bfloat16, bfloat16, bfloat16, bfloat16, float>
{
};

TEST_P(GPU_BNFwdTrain_FP16, BnFwdTrainCKHalf) {}

TEST_P(GPU_BNFwdTrain_FP32, BnFwdTrainCKFloat) {}

TEST_P(GPU_BNFwdTrain_FP64, BnFwdTrainCKDouble) {}
TEST_P(GPU_BNFwdTrain_BFP16, BnFwdTrainCKBFloat16) {}

INSTANTIATE_TEST_SUITE_P(Smoke,
                         GPU_BNFwdTrain_FP16,
                         testing::Combine(testing::ValuesIn(Network1<BNTestCase>()),
                                          testing::Values(miopenTensorNHWC)));

INSTANTIATE_TEST_SUITE_P(Smoke,
                         GPU_BNFwdTrain_FP32,
                         testing::Combine(testing::ValuesIn(Network1<BNTestCase>()),
                                          testing::Values(miopenTensorNHWC)));

INSTANTIATE_TEST_SUITE_P(Smoke,
                         GPU_BNFwdTrain_FP64,
                         testing::Combine(testing::ValuesIn(Network1<BNTestCase>()),
                                          testing::Values(miopenTensorNHWC)));

INSTANTIATE_TEST_SUITE_P(Smoke,
                         GPU_BNFwdTrain_BFP16,
                         testing::Combine(testing::ValuesIn(Network1<BNTestCase>()),
                                          testing::Values(miopenTensorNHWC)));
