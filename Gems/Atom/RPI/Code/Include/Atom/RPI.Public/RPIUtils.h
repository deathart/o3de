/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

// RPIUtils is for dumping common functionality that is used in several places across the RPI
 
#include <AtomCore/Instance/Instance.h>

#include <Atom/RHI/DispatchItem.h>
#include <Atom/RPI.Public/Base.h>
#include <Atom/RPI.Public/Image/StreamingImage.h>
#include <Atom/RPI.Reflect/Shader/ShaderAsset.h>

#include <AzCore/std/containers/span.h>

namespace AZ
{
    namespace RPI
    {
        class Shader;

        //! Get the asset ID for a given shader file path
        Data::AssetId GetShaderAssetId(const AZStd::string& shaderFilePath, bool isCritical = false);

        //! Finds a shader asset for the given shader asset ID. Optional shaderFilePath param for debugging.
        Data::Asset<ShaderAsset> FindShaderAsset(Data::AssetId shaderAssetId, const AZStd::string& shaderFilePath = "");

        //! Finds a shader asset for the given shader file path
        Data::Asset<ShaderAsset> FindShaderAsset(const AZStd::string& shaderFilePath);
        Data::Asset<ShaderAsset> FindCriticalShaderAsset(const AZStd::string& shaderFilePath);

        //! Loads a shader for the given shader asset ID. Optional shaderFilePath param for debugging.
        Data::Instance<Shader> LoadShader(Data::AssetId shaderAssetId, const AZStd::string& shaderFilePath = "", const AZStd::string& supervariantName = "");

        //! Loads a shader for the given shader file path
        Data::Instance<Shader> LoadShader(const AZStd::string& shaderFilePath, const AZStd::string& supervariantName = "");
        Data::Instance<Shader> LoadCriticalShader(const AZStd::string& shaderFilePath, const AZStd::string& supervariantName = "");

        //! Loads a streaming image asset for the given file path
        Data::Instance<RPI::StreamingImage> LoadStreamingTexture(AZStd::string_view path);

        //! Looks for a three arguments attribute named @attributeName in the given shader asset.
        //! Assigns the value to each non-null output variables.
        //! @param shaderAsset
        //! @param attributeName
        //! @param numThreadsX Can be NULL. If not NULL it takes the value of the 1st argument of the attribute. Becomes 1 on error.
        //! @param numThreadsY Can be NULL. If not NULL it takes the value of the 2nd argument of the attribute. Becomes 1 on error.
        //! @param numThreadsZ Can be NULL. If not NULL it takes the value of the 3rd argument of the attribute. Becomes 1 on error.
        //! @returns An Outcome instance with error message in case of error.
        AZ::Outcome<void, AZStd::string> GetComputeShaderNumThreads(const Data::Asset<ShaderAsset>& shaderAsset, const AZ::Name& attributeName, uint16_t* numThreadsX, uint16_t* numThreadsY, uint16_t* numThreadsZ);

        //! Same as above, but assumes the name of the attribute to be 'numthreads'.
        AZ::Outcome<void, AZStd::string> GetComputeShaderNumThreads(const Data::Asset<ShaderAsset>& shaderAsset, uint16_t* numThreadsX, uint16_t* numThreadsY, uint16_t* numThreadsZ);

        //! Same as above. Provided as a convenience when all arguments of the 'numthreads' attributes should be assigned to RHI::DispatchDirect::m_threadsPerGroup* variables.
        AZ::Outcome<void, AZStd::string> GetComputeShaderNumThreads(const Data::Asset<ShaderAsset>& shaderAsset, RHI::DispatchDirect& dispatchDirect);

        //! Get single image pixel value for specified mip and slice
        template<typename T>
        T GetSubImagePixelValue(const AZ::Data::Asset<AZ::RPI::StreamingImageAsset>& imageAsset, uint32_t x, uint32_t y, uint32_t componentIndex = 0, uint32_t mip = 0, uint32_t slice = 0);

        //! Retrieve a region of image pixel values (float) for specified mip and slice
        //! NOTE: The topLeft coordinate is inclusive, whereas the bottomRight is exclusive
        bool GetSubImagePixelValues(const AZ::Data::Asset<AZ::RPI::StreamingImageAsset>& imageAsset, AZStd::pair<uint32_t, uint32_t> topLeft, AZStd::pair<uint32_t, uint32_t> bottomRight, AZStd::span<float> outValues, uint32_t componentIndex = 0, uint32_t mip = 0, uint32_t slice = 0);

        //! Retrieve a region of image pixel values (uint) for specified mip and slice
        //! NOTE: The topLeft coordinate is inclusive, whereas the bottomRight is exclusive
        bool GetSubImagePixelValues(const AZ::Data::Asset<AZ::RPI::StreamingImageAsset>& imageAsset, AZStd::pair<uint32_t, uint32_t> topLeft, AZStd::pair<uint32_t, uint32_t> bottomRight, AZStd::span<AZ::u32> outValues, uint32_t componentIndex = 0, uint32_t mip = 0, uint32_t slice = 0);

        //! Retrieve a region of image pixel values (int) for specified mip and slice
        //! NOTE: The topLeft coordinate is inclusive, whereas the bottomRight is exclusive
        bool GetSubImagePixelValues(const AZ::Data::Asset<AZ::RPI::StreamingImageAsset>& imageAsset, AZStd::pair<uint32_t, uint32_t> topLeft, AZStd::pair<uint32_t, uint32_t> bottomRight, AZStd::span<AZ::s32> outValues, uint32_t componentIndex = 0, uint32_t mip = 0, uint32_t slice = 0);
    }   // namespace RPI
}   // namespace AZ

