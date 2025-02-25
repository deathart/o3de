/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzTest/AzTest.h>
#include <Common/RPITestFixture.h>
#include <Common/JsonTestUtils.h>
#include <Common/ShaderAssetTestUtils.h>
#include <Common/ErrorMessageFinder.h>
#include <Common/SerializeTester.h>
#include <Material/MaterialAssetTestUtils.h>

#include <Atom/RPI.Reflect/Material/MaterialAsset.h>
#include <Atom/RPI.Edit/Material/MaterialSourceData.h>
#include <Atom/RPI.Edit/Material/MaterialTypeSourceData.h>
#include <Atom/RPI.Reflect/Material/MaterialTypeAssetCreator.h>
#include <Atom/RPI.Reflect/Material/MaterialPropertiesLayout.h>
#include <Atom/RPI.Edit/Material/MaterialUtils.h>

#include <AzCore/Math/Vector2.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Math/Vector4.h>
#include <AzCore/Math/Color.h>
#include <AzCore/Utils/Utils.h>

#include <AzFramework/IO/LocalFileIO.h>

namespace UnitTest
{
    using namespace AZ;
    using namespace RPI;

    class MaterialSourceDataTests
        : public RPITestFixture
    {
    protected:
        RHI::Ptr<RHI::ShaderResourceGroupLayout> m_testMaterialSrgLayout;
        Data::Asset<ShaderAsset> m_testShaderAsset;
        Data::Asset<MaterialTypeAsset> m_testMaterialTypeAsset;
        Data::Asset<ImageAsset> m_testImageAsset;

        void Reflect(AZ::ReflectContext* context) override
        {
            RPITestFixture::Reflect(context);
            MaterialTypeSourceData::Reflect(context);
            MaterialSourceData::Reflect(context);
        }

        void SetUp() override
        {
            EXPECT_EQ(nullptr, IO::FileIOBase::GetInstance());

            RPITestFixture::SetUp();

            auto localFileIO = AZ::IO::FileIOBase::GetInstance();
            EXPECT_NE(nullptr, localFileIO);
            char rootPath[AZ_MAX_PATH_LEN];
            AZ::Utils::GetExecutableDirectory(rootPath, AZ_MAX_PATH_LEN);
            localFileIO->SetAlias("@exefolder@", rootPath);

            m_testMaterialSrgLayout = CreateCommonTestMaterialSrgLayout();
            m_testShaderAsset = CreateTestShaderAsset(Uuid::CreateRandom(), m_testMaterialSrgLayout);
            m_assetSystemStub.RegisterSourceInfo("@exefolder@/Temp/test.shader", m_testShaderAsset.GetId());

            m_testMaterialTypeAsset = CreateTestMaterialTypeAsset(Uuid::CreateRandom());

            // Since this test doesn't actually instantiate a Material, it won't need to instantiate this ImageAsset, so all we
            // need is an asset reference with a valid ID.
            m_testImageAsset = Data::Asset<ImageAsset>{ Data::AssetId{Uuid::CreateRandom(), StreamingImageAsset::GetImageAssetSubId()}, azrtti_typeid<StreamingImageAsset>() };

            // Register the test assets with the AssetSystemStub so CreateMaterialAsset() can use AssetUtils.
            m_assetSystemStub.RegisterSourceInfo("@exefolder@/Temp/test.materialtype", m_testMaterialTypeAsset.GetId());
            m_assetSystemStub.RegisterSourceInfo("@exefolder@/Temp/test.streamingimage", m_testImageAsset.GetId());
        }

        void TearDown() override
        {
            m_testMaterialTypeAsset.Reset();
            m_testMaterialSrgLayout = nullptr;
            m_testShaderAsset.Reset();
            m_testImageAsset.Reset();

            RPITestFixture::TearDown();
        }
        
        Data::Asset<MaterialTypeAsset> CreateTestMaterialTypeAsset(Data::AssetId assetId)
        {
            const char* materialTypeJson = R"(
                    {
                        "version": 10,
                        "propertyLayout": {
                            "propertyGroups": [
                                {
                                    "name": "general",
                                    "properties": [
                                        {"name": "MyBool", "type": "bool"},
                                        {"name": "MyInt", "type": "Int"},
                                        {"name": "MyUInt", "type": "UInt"},
                                        {"name": "MyFloat", "type": "Float"},
                                        {"name": "MyFloat2", "type": "Vector2"},
                                        {"name": "MyFloat3", "type": "Vector3"},
                                        {"name": "MyFloat4", "type": "Vector4"},
                                        {"name": "MyColor", "type": "Color"},
                                        {"name": "MyImage", "type": "Image"},
                                        {"name": "MyEnum", "type": "Enum", "enumValues": ["Enum0", "Enum1", "Enum2"], "defaultValue": "Enum0"}
                                    ]
                                }
                            ]
                        },
                        "shaders": [
                            {
                                "file": "@exefolder@/Temp/test.shader"
                            }
                        ],
                        "versionUpdates": [
                            {
                                "toVersion": 2,
                                "actions": [
                                    {"op": "rename", "from": "general.testColorNameA", "to": "general.testColorNameB"}
                                ]
                            },
                            {
                                "toVersion": 4,
                                "actions": [
                                    {"op": "rename", "from": "general.testColorNameB", "to": "general.testColorNameC"}
                                ]
                            },
                            {
                                "toVersion": 6,
                                "actions": [
                                    {"op": "rename", "from": "oldGroup.MyFloat", "to": "general.MyFloat"},
                                    {"op": "rename", "from": "oldGroup.MyIntOldName", "to": "general.MyInt"}
                                ]
                            },
                            {
                                "toVersion": 10,
                                "actions": [
                                    {"op": "rename", "from": "general.testColorNameC", "to": "general.MyColor"}
                                ]
                            }
                        ]
                    }
                )";


            MaterialTypeSourceData materialTypeSourceData;
            LoadTestDataFromJson(materialTypeSourceData, materialTypeJson);
            return materialTypeSourceData.CreateMaterialTypeAsset(assetId).TakeValue();
        }
    };

    void AddPropertyGroup(MaterialSourceData& material, AZStd::string_view groupName)
    {
        material.m_properties.insert(groupName);
    }

    void AddProperty(MaterialSourceData& material, AZStd::string_view groupName, AZStd::string_view propertyName, const MaterialPropertyValue& anyValue)
    {
        material.m_properties[groupName][propertyName].m_value = anyValue;
    }

    TEST_F(MaterialSourceDataTests, CreateMaterialAsset_BasicProperties)
    {
        MaterialSourceData sourceData;

        sourceData.m_materialType = "@exefolder@/Temp/test.materialtype";
        AddPropertyGroup(sourceData, "general");
        AddProperty(sourceData, "general", "MyBool", true);
        AddProperty(sourceData, "general", "MyInt", -10);
        AddProperty(sourceData, "general", "MyUInt", 25u);
        AddProperty(sourceData, "general", "MyFloat", 1.5f);
        AddProperty(sourceData, "general", "MyColor", AZ::Color{0.1f, 0.2f, 0.3f, 0.4f});
        AddProperty(sourceData, "general", "MyFloat2", AZ::Vector2(2.1f, 2.2f));
        AddProperty(sourceData, "general", "MyFloat3", AZ::Vector3(3.1f, 3.2f, 3.3f));
        AddProperty(sourceData, "general", "MyFloat4", AZ::Vector4(4.1f, 4.2f, 4.3f, 4.4f));
        AddProperty(sourceData, "general", "MyImage", AZStd::string("@exefolder@/Temp/test.streamingimage"));
        AddProperty(sourceData, "general", "MyEnum", AZStd::string("Enum1"));

        auto materialAssetOutcome = sourceData.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::PreBake, true);
        EXPECT_TRUE(materialAssetOutcome.IsSuccess());

        Data::Asset<MaterialAsset> materialAsset = materialAssetOutcome.GetValue();

        EXPECT_TRUE(materialAsset->WasPreFinalized());
        EXPECT_EQ(0, materialAsset->GetRawPropertyValues().size()); // A pre-baked material has no need for the original raw property names and values

        // The order here is based on the order in the MaterialTypeSourceData, as added to the MaterialTypeAssetCreator.
        EXPECT_EQ(materialAsset->GetPropertyValues()[0].GetValue<bool>(), true);
        EXPECT_EQ(materialAsset->GetPropertyValues()[1].GetValue<int32_t>(), -10);
        EXPECT_EQ(materialAsset->GetPropertyValues()[2].GetValue<uint32_t>(), 25u);
        EXPECT_EQ(materialAsset->GetPropertyValues()[3].GetValue<float>(), 1.5f);
        EXPECT_EQ(materialAsset->GetPropertyValues()[4].GetValue<Vector2>(), Vector2(2.1f, 2.2f));
        EXPECT_EQ(materialAsset->GetPropertyValues()[5].GetValue<Vector3>(), Vector3(3.1f, 3.2f, 3.3f));
        EXPECT_EQ(materialAsset->GetPropertyValues()[6].GetValue<Vector4>(), Vector4(4.1f, 4.2f, 4.3f, 4.4f));
        EXPECT_EQ(materialAsset->GetPropertyValues()[7].GetValue<Color>(), Color(0.1f, 0.2f, 0.3f, 0.4f));
        EXPECT_EQ(materialAsset->GetPropertyValues()[8].GetValue<Data::Asset<ImageAsset>>(), m_testImageAsset);
        EXPECT_EQ(materialAsset->GetPropertyValues()[9].GetValue<uint32_t>(), 1u);
    }
    
    TEST_F(MaterialSourceDataTests, CreateMaterialAsset_DeferredBake)
    {
        // This test is similar to CreateMaterialAsset_BasicProperties but uses MaterialAssetProcessingMode::DeferredBake instead of PreBake.

        Data::AssetId materialTypeAssetId = Uuid::CreateRandom();

        // This material type asset will be known by the asset system (stub) but doesn't exist in the AssetManager.
        // This demonstrates that the CreateMaterialAsset does not attempt to access the MaterialTypeAsset data in MaterialAssetProcessingMode::DeferredBake.
        m_assetSystemStub.RegisterSourceInfo("testDeferredBake.materialtype", materialTypeAssetId);

        MaterialSourceData sourceData;

        sourceData.m_materialType = "testDeferredBake.materialtype";
        AddPropertyGroup(sourceData, "general");
        AddProperty(sourceData, "general", "MyBool"  , true);
        AddProperty(sourceData, "general", "MyInt"   , -10);
        AddProperty(sourceData, "general", "MyUInt"  , 25u);
        AddProperty(sourceData, "general", "MyFloat" , 1.5f);
        AddProperty(sourceData, "general", "MyColor" , AZ::Color{0.1f, 0.2f, 0.3f, 0.4f});
        AddProperty(sourceData, "general", "MyFloat2", AZ::Vector2(2.1f, 2.2f));
        AddProperty(sourceData, "general", "MyFloat3", AZ::Vector3(3.1f, 3.2f, 3.3f));
        AddProperty(sourceData, "general", "MyFloat4", AZ::Vector4(4.1f, 4.2f, 4.3f, 4.4f));
        AddProperty(sourceData, "general", "MyImage" , AZStd::string("@exefolder@/Temp/test.streamingimage"));
        AddProperty(sourceData, "general", "MyEnum"  , AZStd::string("Enum1"));

        auto materialAssetOutcome = sourceData.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::DeferredBake, true);
        EXPECT_TRUE(materialAssetOutcome.IsSuccess());

        Data::Asset<MaterialAsset> materialAsset = materialAssetOutcome.GetValue();

        EXPECT_FALSE(materialAsset->WasPreFinalized());

        // Note we avoid calling  GetPropertyValues() because that will auto-finalize the material. We want to check its raw property values first.

        auto findRawPropertyValue = [materialAsset](const char* propertyId)
        {
            auto iter = AZStd::find_if(materialAsset->GetRawPropertyValues().begin(), materialAsset->GetRawPropertyValues().end(), [propertyId](const AZStd::pair<Name, MaterialPropertyValue>& pair)
                {
                    return pair.first == AZ::Name{propertyId};
                });

            if (iter == materialAsset->GetRawPropertyValues().end())
            {
                return MaterialPropertyValue{};
            }
            else
            {
                return iter->second;
            }
        };

        auto checkRawPropertyValues = [findRawPropertyValue, this]()
        {
            EXPECT_EQ(findRawPropertyValue("general.MyBool"  ).GetValue<bool>(), true);
            EXPECT_EQ(findRawPropertyValue("general.MyInt"   ).GetValue<int32_t>(), -10);
            EXPECT_EQ(findRawPropertyValue("general.MyUInt"  ).GetValue<uint32_t>(), 25u);
            EXPECT_EQ(findRawPropertyValue("general.MyFloat" ).GetValue<float>(), 1.5f);
            EXPECT_EQ(findRawPropertyValue("general.MyFloat2").GetValue<Vector2>(), Vector2(2.1f, 2.2f));
            EXPECT_EQ(findRawPropertyValue("general.MyFloat3").GetValue<Vector3>(), Vector3(3.1f, 3.2f, 3.3f));
            EXPECT_EQ(findRawPropertyValue("general.MyFloat4").GetValue<Vector4>(), Vector4(4.1f, 4.2f, 4.3f, 4.4f));
            EXPECT_EQ(findRawPropertyValue("general.MyColor" ).GetValue<Color>(), Color(0.1f, 0.2f, 0.3f, 0.4f));
            EXPECT_EQ(findRawPropertyValue("general.MyImage" ).GetValue<Data::Asset<ImageAsset>>(), m_testImageAsset);
            // The raw value for an enum is the original string, not the numerical value, because the material type holds the necessary metadata to match the name to the value.
            EXPECT_EQ(findRawPropertyValue("general.MyEnum"  ).GetValue<AZStd::string>(), AZStd::string("Enum1")); 
        };

        // We check the raw property values before the material type asset is even available
        checkRawPropertyValues();

        // Now we'll create the material type asset in memory so the material will have what it needs to finalize itself.
        Data::Asset<MaterialTypeAsset> testMaterialTypeAsset = CreateTestMaterialTypeAsset(materialTypeAssetId);

        // The MaterialAsset is still holding an reference to an unloaded asset, so we run it through the serializer which causes the loaded MaterialAsset
        // to have access to the testMaterialTypeAsset. This is similar to how the AP would save the MaterialAsset to the cache and the runtime would load it.
        SerializeTester<RPI::MaterialAsset> tester(GetSerializeContext());
        tester.SerializeOut(materialAsset.Get());
        materialAsset = tester.SerializeIn(Uuid::CreateRandom(), ObjectStream::FilterDescriptor{AZ::Data::AssetFilterNoAssetLoading});

        // We check that the asset is still in the original un-finalized state after going through the serialization process.
        EXPECT_FALSE(materialAsset->WasPreFinalized());
        checkRawPropertyValues();

        // Now all the property values should be available through the main GetPropertyValues() API.
        EXPECT_EQ(materialAsset->GetPropertyValues()[0].GetValue<bool>(), true);
        EXPECT_EQ(materialAsset->GetPropertyValues()[1].GetValue<int32_t>(), -10);
        EXPECT_EQ(materialAsset->GetPropertyValues()[2].GetValue<uint32_t>(), 25u);
        EXPECT_EQ(materialAsset->GetPropertyValues()[3].GetValue<float>(), 1.5f);
        EXPECT_EQ(materialAsset->GetPropertyValues()[4].GetValue<Vector2>(), Vector2(2.1f, 2.2f));
        EXPECT_EQ(materialAsset->GetPropertyValues()[5].GetValue<Vector3>(), Vector3(3.1f, 3.2f, 3.3f));
        EXPECT_EQ(materialAsset->GetPropertyValues()[6].GetValue<Vector4>(), Vector4(4.1f, 4.2f, 4.3f, 4.4f));
        EXPECT_EQ(materialAsset->GetPropertyValues()[7].GetValue<Color>(), Color(0.1f, 0.2f, 0.3f, 0.4f));
        EXPECT_EQ(materialAsset->GetPropertyValues()[8].GetValue<Data::Asset<ImageAsset>>(), m_testImageAsset);
        EXPECT_EQ(materialAsset->GetPropertyValues()[9].GetValue<uint32_t>(), 1u);
        
        // The raw property values are still available (because they are needed if a hot-reload of the MaterialTypeAsset occurs)
        EXPECT_FALSE(materialAsset->WasPreFinalized());
        checkRawPropertyValues();
    }

    // Can return a Vector4 or a Color as a Vector4
    Vector4 GetAsVector4(const MaterialPropertyValue& value)
    {
        if (value.GetTypeId() == azrtti_typeid<Vector4>())
        {
            return value.GetValue<Vector4>();
        }
        else if (value.GetTypeId() == azrtti_typeid<Color>())
        {
            return value.GetValue<Color>().GetAsVector4();
        }
        else
        {
            return Vector4::CreateZero();
        }
    }
    
    // Can return a Int or a UInt as a Int
    int32_t GetAsInt(const MaterialPropertyValue& value)
    {
        if (value.GetTypeId() == azrtti_typeid<int32_t>())
        {
            return value.GetValue<int32_t>();
        }
        else if (value.GetTypeId() == azrtti_typeid<uint32_t>())
        {
            return aznumeric_cast<int32_t>(value.GetValue<uint32_t>());
        }
        else
        {
            return 0;
        }
    }

    template<typename TargetTypeT>
    bool AreTypesCompatible(const MaterialPropertyValue& a, const MaterialPropertyValue& b)
    {
        auto fixupType = [](TypeId t)
        {
            if (t == azrtti_typeid<uint32_t>())
            {
                return azrtti_typeid<int32_t>();
            }

            if (t == azrtti_typeid<Color>())
            {
                return azrtti_typeid<Vector4>();
            }

            return t;
        };

        TypeId targetTypeId = azrtti_typeid<TargetTypeT>();

        return fixupType(a.GetTypeId()) == fixupType(targetTypeId) && fixupType(b.GetTypeId()) == fixupType(targetTypeId);
    }

    void CheckEqual(MaterialSourceData& a, MaterialSourceData& b)
    {
        EXPECT_STREQ(a.m_materialType.data(), b.m_materialType.data());
        EXPECT_STREQ(a.m_description.data(), b.m_description.data());
        EXPECT_STREQ(a.m_parentMaterial.data(), b.m_parentMaterial.data());
        EXPECT_EQ(a.m_materialTypeVersion, b.m_materialTypeVersion);

        EXPECT_EQ(a.m_properties.size(), b.m_properties.size());
        for (auto& groupA : a.m_properties)
        {
            AZStd::string groupName = groupA.first;

            auto groupIterB = b.m_properties.find(groupName);
            if (groupIterB == b.m_properties.end())
            {
                EXPECT_TRUE(false) << "groupB[" << groupName.c_str() << "] not found";
                continue;
            }

            auto& groupB = *groupIterB;

            EXPECT_EQ(groupA.second.size(), groupB.second.size()) << " for group[" << groupName.c_str() << "]";

            for (auto& propertyIterA : groupA.second)
            {
                AZStd::string propertyName = propertyIterA.first;

                auto propertyIterB = groupB.second.find(propertyName);
                if (propertyIterB == groupB.second.end())
                {
                    EXPECT_TRUE(false) << "groupB[" << groupName.c_str() << "][" << propertyName.c_str() << "] not found";
                    continue;
                }

                auto& propertyA = propertyIterA.second;
                auto& propertyB = propertyIterB->second;

                AZStd::string propertyReference = AZStd::string::format(" for property '%s.%s'", groupName.c_str(), propertyName.c_str());
                
                // We allow some types like Vector4 and Color or Int and UInt to be interchangeable since they serialize the same and can be converted when the MaterialAsset is finalized.

                if (AreTypesCompatible<bool>(propertyA.m_value, propertyB.m_value))
                {
                    EXPECT_EQ(propertyA.m_value.GetValue<bool>(), propertyB.m_value.GetValue<bool>()) << propertyReference.c_str();
                }
                else if (AreTypesCompatible<int32_t>(propertyA.m_value, propertyB.m_value))
                {
                    EXPECT_EQ(GetAsInt(propertyA.m_value), GetAsInt(propertyB.m_value)) << propertyReference.c_str();
                }
                else if (AreTypesCompatible<float>(propertyA.m_value, propertyB.m_value))
                {
                    EXPECT_NEAR(propertyA.m_value.GetValue<float>(),     propertyB.m_value.GetValue<float>(), 0.01) << propertyReference.c_str();
                }
                else if (AreTypesCompatible<Vector2>(propertyA.m_value, propertyB.m_value))
                {
                    EXPECT_TRUE(propertyA.m_value.GetValue<Vector2>().IsClose(propertyB.m_value.GetValue<Vector2>())) << propertyReference.c_str();
                }
                else if (AreTypesCompatible<Vector3>(propertyA.m_value, propertyB.m_value))
                {
                    EXPECT_TRUE(propertyA.m_value.GetValue<Vector3>().IsClose(propertyB.m_value.GetValue<Vector3>())) << propertyReference.c_str();
                }
                else if (AreTypesCompatible<Vector4>(propertyA.m_value, propertyB.m_value))
                {
                    EXPECT_TRUE(GetAsVector4(propertyA.m_value).IsClose(GetAsVector4(propertyB.m_value))) << propertyReference.c_str();
                }
                else if (AreTypesCompatible<AZStd::string>(propertyA.m_value, propertyB.m_value))
                {
                    EXPECT_STREQ(propertyA.m_value.GetValue<AZStd::string>().c_str(), propertyB.m_value.GetValue<AZStd::string>().c_str()) << propertyReference.c_str();
                }
                else
                {
                    ADD_FAILURE();
                }
            }
        }

    }

    TEST_F(MaterialSourceDataTests, TestJsonRoundTrip)
    {
        const char* materialTypeFilePath = "@exefolder@/Temp/roundTripTest.materialtype";

        MaterialSourceData sourceDataOriginal;
        sourceDataOriginal.m_materialType = materialTypeFilePath;
        sourceDataOriginal.m_parentMaterial = materialTypeFilePath;
        sourceDataOriginal.m_description = "This is a description";
        sourceDataOriginal.m_materialTypeVersion = 7;
        AddPropertyGroup(sourceDataOriginal, "groupA");
        AddProperty(sourceDataOriginal, "groupA", "MyBool", true);
        AddProperty(sourceDataOriginal, "groupA", "MyInt", -10);
        AddProperty(sourceDataOriginal, "groupA", "MyUInt", 25u);
        AddPropertyGroup(sourceDataOriginal, "groupB");
        AddProperty(sourceDataOriginal, "groupB", "MyFloat", 1.5f);
        AddProperty(sourceDataOriginal, "groupB", "MyFloat2", AZ::Vector2(2.1f, 2.2f));
        AddProperty(sourceDataOriginal, "groupB", "MyFloat3", AZ::Vector3(3.1f, 3.2f, 3.3f));
        AddPropertyGroup(sourceDataOriginal, "groupC");
        AddProperty(sourceDataOriginal, "groupC", "MyFloat4", AZ::Vector4(4.1f, 4.2f, 4.3f, 4.4f));
        AddProperty(sourceDataOriginal, "groupC", "MyColor", AZ::Color{0.1f, 0.2f, 0.3f, 0.4f});
        AddProperty(sourceDataOriginal, "groupC", "MyImage", AZStd::string("@exefolder@/Temp/test.streamingimage"));

        AZStd::string sourceDataSerialized;
        JsonTestResult storeResult = StoreTestDataToJson(sourceDataOriginal, sourceDataSerialized);

        MaterialSourceData sourceDataCopy;
        JsonTestResult loadResult = LoadTestDataFromJson(sourceDataCopy, sourceDataSerialized);

        CheckEqual(sourceDataOriginal, sourceDataCopy);
    }

    TEST_F(MaterialSourceDataTests, Load_MaterialTypeAfterPropertyList)
    {
        const AZStd::string simpleMaterialTypeJson = R"(
            {
                "propertyLayout": {
                    "propertyGroups":
                    [
                        {
                            "name": "general",
                            "properties": [
                                {
                                    "name": "testValue",
                                    "type": "Float"
                                }
                            ]
                        }
                    ]
                }
            }
        )";

        const char* materialTypeFilePath = "@exefolder@/Temp/simpleMaterialType.materialtype";

        AZ::IO::FileIOStream file;
        EXPECT_TRUE(file.Open(materialTypeFilePath, AZ::IO::OpenMode::ModeWrite | AZ::IO::OpenMode::ModeCreatePath));
        file.Write(simpleMaterialTypeJson.size(), simpleMaterialTypeJson.data());
        file.Close();

        // It shouldn't matter whether the materialType field appears before the property value list. This allows for the possibility
        // that customer scripts generate material data and happen to use an unexpected order.
        const AZStd::string inputJson = R"(
        {
            "properties": {
                "general": {
                    "testValue": 1.2
                }
            },
            "materialType": "@exefolder@/Temp/simpleMaterialType.materialtype"
        }
        )";

        MaterialSourceData material;
        JsonTestResult loadResult = LoadTestDataFromJson(material, inputJson);

        EXPECT_EQ(AZ::JsonSerializationResult::Tasks::ReadField, loadResult.m_jsonResultCode.GetTask());
        EXPECT_EQ(AZ::JsonSerializationResult::Processing::Completed, loadResult.m_jsonResultCode.GetProcessing());

        float testValue = material.m_properties["general"]["testValue"].m_value.GetValue<float>();
        EXPECT_FLOAT_EQ(1.2f, testValue);
    }
    
    TEST_F(MaterialSourceDataTests, CreateMaterialAsset_NoMaterialType)
    {
        const AZStd::string inputJson = R"(
            {
                "materialTypeVersion": 1,
                "properties": {
                    "baseColor": {
                        "color": [1.0,1.0,1.0]
                    }
                }
            }
        )";

        MaterialSourceData material;
        JsonTestResult loadResult = LoadTestDataFromJson(material, inputJson);

        const bool elevateWarnings = false;

        ErrorMessageFinder errorMessageFinder;

        errorMessageFinder.AddExpectedErrorMessage("materialType was not specified");
        auto result = material.CreateMaterialAsset(AZ::Uuid::CreateRandom(), "test.material", AZ::RPI::MaterialAssetProcessingMode::DeferredBake, elevateWarnings);
        EXPECT_FALSE(result.IsSuccess());
        errorMessageFinder.CheckExpectedErrorsFound();

        errorMessageFinder.Reset();
        errorMessageFinder.AddExpectedErrorMessage("materialType was not specified");
        result = material.CreateMaterialAsset(AZ::Uuid::CreateRandom(), "test.material", AZ::RPI::MaterialAssetProcessingMode::PreBake, elevateWarnings);
        EXPECT_FALSE(result.IsSuccess());
        errorMessageFinder.CheckExpectedErrorsFound();
        
        errorMessageFinder.Reset();
        errorMessageFinder.AddExpectedErrorMessage("materialType was not specified");
        result = material.CreateMaterialAssetFromSourceData(AZ::Uuid::CreateRandom(), "test.material", elevateWarnings);
        EXPECT_FALSE(result.IsSuccess());
        errorMessageFinder.CheckExpectedErrorsFound();
    }
    
    TEST_F(MaterialSourceDataTests, CreateMaterialAsset_MaterialTypeDoesNotExist)
    {
        const AZStd::string inputJson = R"(
            {
                "materialType": "DoesNotExist.materialtype",
                "materialTypeVersion": 1,
                "properties": {
                    "baseColor": {
                        "color": [1.0,1.0,1.0]
                    }
                }
            }
        )";

        MaterialSourceData material;
        JsonTestResult loadResult = LoadTestDataFromJson(material, inputJson);

        const bool elevateWarnings = false;

        ErrorMessageFinder errorMessageFinder;

        errorMessageFinder.AddExpectedErrorMessage("Could not find asset [DoesNotExist.materialtype]");
        auto result = material.CreateMaterialAsset(AZ::Uuid::CreateRandom(), "test.material", AZ::RPI::MaterialAssetProcessingMode::DeferredBake, elevateWarnings);
        EXPECT_FALSE(result.IsSuccess());
        errorMessageFinder.CheckExpectedErrorsFound();

        errorMessageFinder.Reset();
        errorMessageFinder.AddExpectedErrorMessage("Could not find asset [DoesNotExist.materialtype]");
        result = material.CreateMaterialAsset(AZ::Uuid::CreateRandom(), "test.material", AZ::RPI::MaterialAssetProcessingMode::PreBake, elevateWarnings);
        EXPECT_FALSE(result.IsSuccess());
        errorMessageFinder.CheckExpectedErrorsFound();
        
        errorMessageFinder.Reset();
        errorMessageFinder.AddExpectedErrorMessage("Could not find asset [DoesNotExist.materialtype]");
        errorMessageFinder.AddIgnoredErrorMessage("Failed to create material type asset ID", true);
        result = material.CreateMaterialAssetFromSourceData(AZ::Uuid::CreateRandom(), "test.material", elevateWarnings);
        EXPECT_FALSE(result.IsSuccess());
        errorMessageFinder.CheckExpectedErrorsFound();
    }
    
    TEST_F(MaterialSourceDataTests, CreateMaterialAsset_MaterialPropertyNotFound)
    {
        MaterialSourceData material;
        material.m_materialType = "@exefolder@/Temp/test.materialtype";
        AddPropertyGroup(material, "general");
        AddProperty(material, "general", "FieldDoesNotExist", 1.5f);
        
        const bool elevateWarnings = true;

        ErrorMessageFinder errorMessageFinder("\"general.FieldDoesNotExist\" is not found");
        errorMessageFinder.AddIgnoredErrorMessage("Failed to build MaterialAsset", true);
        auto result = material.CreateMaterialAsset(AZ::Uuid::CreateRandom(), "test.material", AZ::RPI::MaterialAssetProcessingMode::PreBake, elevateWarnings);
        EXPECT_FALSE(result.IsSuccess());
        errorMessageFinder.CheckExpectedErrorsFound();
    }

    TEST_F(MaterialSourceDataTests, CreateMaterialAsset_MultiLevelDataInheritance)
    {
        MaterialSourceData sourceDataLevel1;
        sourceDataLevel1.m_materialType = "@exefolder@/Temp/test.materialtype";
        AddPropertyGroup(sourceDataLevel1, "general");
        AddProperty(sourceDataLevel1, "general", "MyFloat", 1.5f);
        AddProperty(sourceDataLevel1, "general", "MyColor", AZ::Color{0.1f, 0.2f, 0.3f, 0.4f});

        MaterialSourceData sourceDataLevel2;
        sourceDataLevel2.m_materialType = "@exefolder@/Temp/test.materialtype";
        sourceDataLevel2.m_parentMaterial = "level1.material";
        AddPropertyGroup(sourceDataLevel2, "general");
        AddProperty(sourceDataLevel2, "general", "MyColor", AZ::Color{0.15f, 0.25f, 0.35f, 0.45f});
        AddProperty(sourceDataLevel2, "general", "MyFloat2", AZ::Vector2{4.1f, 4.2f});

        MaterialSourceData sourceDataLevel3;
        sourceDataLevel3.m_materialType = "@exefolder@/Temp/test.materialtype";
        sourceDataLevel3.m_parentMaterial = "level2.material";
        AddPropertyGroup(sourceDataLevel3, "general");
        AddProperty(sourceDataLevel3, "general", "MyFloat", 3.5f);

        auto materialAssetLevel1 = sourceDataLevel1.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::PreBake, true);
        EXPECT_TRUE(materialAssetLevel1.IsSuccess());
        EXPECT_TRUE(materialAssetLevel1.GetValue()->WasPreFinalized());

        m_assetSystemStub.RegisterSourceInfo("level1.material", materialAssetLevel1.GetValue().GetId());

        auto materialAssetLevel2 = sourceDataLevel2.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::PreBake, true);
        EXPECT_TRUE(materialAssetLevel2.IsSuccess());
        EXPECT_TRUE(materialAssetLevel2.GetValue()->WasPreFinalized());

        m_assetSystemStub.RegisterSourceInfo("level2.material", materialAssetLevel2.GetValue().GetId());

        auto materialAssetLevel3 = sourceDataLevel3.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::PreBake, true);
        EXPECT_TRUE(materialAssetLevel3.IsSuccess());
        EXPECT_TRUE(materialAssetLevel3.GetValue()->WasPreFinalized());

        auto layout = m_testMaterialTypeAsset->GetMaterialPropertiesLayout();
        MaterialPropertyIndex myFloat = layout->FindPropertyIndex(Name("general.MyFloat"));
        MaterialPropertyIndex myFloat2 = layout->FindPropertyIndex(Name("general.MyFloat2"));
        MaterialPropertyIndex myColor = layout->FindPropertyIndex(Name("general.MyColor"));

        AZStd::span<const MaterialPropertyValue> properties;

        // Check level 1 properties
        properties = materialAssetLevel1.GetValue()->GetPropertyValues();
        EXPECT_EQ(properties[myFloat.GetIndex()].GetValue<float>(), 1.5f);
        EXPECT_EQ(properties[myFloat2.GetIndex()].GetValue<Vector2>(), Vector2(0.0f, 0.0f));
        EXPECT_EQ(properties[myColor.GetIndex()].GetValue<Color>(), Color(0.1f, 0.2f, 0.3f, 0.4f));

        // Check level 2 properties
        properties = materialAssetLevel2.GetValue()->GetPropertyValues();
        EXPECT_EQ(properties[myFloat.GetIndex()].GetValue<float>(), 1.5f);
        EXPECT_EQ(properties[myFloat2.GetIndex()].GetValue<Vector2>(), Vector2(4.1f, 4.2f));
        EXPECT_EQ(properties[myColor.GetIndex()].GetValue<Color>(), Color(0.15f, 0.25f, 0.35f, 0.45f));

        // Check level 3 properties
        properties = materialAssetLevel3.GetValue()->GetPropertyValues();
        EXPECT_EQ(properties[myFloat.GetIndex()].GetValue<float>(), 3.5f);
        EXPECT_EQ(properties[myFloat2.GetIndex()].GetValue<Vector2>(), Vector2(4.1f, 4.2f));
        EXPECT_EQ(properties[myColor.GetIndex()].GetValue<Color>(), Color(0.15f, 0.25f, 0.35f, 0.45f));
    }
    
    TEST_F(MaterialSourceDataTests, CreateMaterialAsset_MultiLevelDataInheritance_DeferredBake)
    {
        // This test is similar to CreateMaterialAsset_MultiLevelDataInheritance but uses MaterialAssetProcessingMode::DeferredBake instead of PreBake.

        Data::AssetId materialTypeAssetId = Uuid::CreateRandom();

        // This material type asset will be known by the asset system (stub) but doesn't exist in the AssetManager.
        // This demonstrates that the CreateMaterialAsset does not attempt to access the MaterialTypeAsset data in MaterialAssetProcessingMode::DeferredBake.
        m_assetSystemStub.RegisterSourceInfo("testDeferredBake.materialtype", materialTypeAssetId);

        MaterialSourceData sourceDataLevel1;
        sourceDataLevel1.m_materialType = "testDeferredBake.materialtype";
        AddPropertyGroup(sourceDataLevel1, "general");
        AddProperty(sourceDataLevel1, "general", "MyFloat", 1.5f);
        AddProperty(sourceDataLevel1, "general", "MyColor", AZ::Color{0.1f, 0.2f, 0.3f, 0.4f});

        MaterialSourceData sourceDataLevel2;
        sourceDataLevel2.m_materialType = "testDeferredBake.materialtype";
        sourceDataLevel2.m_parentMaterial = "level1.material";
        AddPropertyGroup(sourceDataLevel2, "general");
        AddProperty(sourceDataLevel2, "general", "MyColor", AZ::Color{0.15f, 0.25f, 0.35f, 0.45f});
        AddProperty(sourceDataLevel2, "general", "MyFloat2", AZ::Vector2{4.1f, 4.2f});

        MaterialSourceData sourceDataLevel3;
        sourceDataLevel3.m_materialType = "testDeferredBake.materialtype";
        sourceDataLevel3.m_parentMaterial = "level2.material";
        AddPropertyGroup(sourceDataLevel3, "general");
        AddProperty(sourceDataLevel3, "general", "MyFloat", 3.5f);

        auto materialAssetLevel1Result = sourceDataLevel1.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::DeferredBake, true);
        EXPECT_TRUE(materialAssetLevel1Result.IsSuccess());
        Data::Asset<MaterialAsset> materialAssetLevel1 = materialAssetLevel1Result.TakeValue();
        EXPECT_FALSE(materialAssetLevel1->WasPreFinalized());

        m_assetSystemStub.RegisterSourceInfo("level1.material", materialAssetLevel1.GetId());

        auto materialAssetLevel2Result = sourceDataLevel2.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::DeferredBake, true);
        EXPECT_TRUE(materialAssetLevel2Result.IsSuccess());
        Data::Asset<MaterialAsset> materialAssetLevel2 = materialAssetLevel2Result.TakeValue();
        EXPECT_FALSE(materialAssetLevel2->WasPreFinalized());

        m_assetSystemStub.RegisterSourceInfo("level2.material", materialAssetLevel2.GetId());

        auto materialAssetLevel3Result = sourceDataLevel3.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::DeferredBake, true);
        EXPECT_TRUE(materialAssetLevel3Result.IsSuccess());
        Data::Asset<MaterialAsset> materialAssetLevel3 = materialAssetLevel3Result.TakeValue();
        EXPECT_FALSE(materialAssetLevel3->WasPreFinalized());

        // Now we'll create the material type asset in memory so the materials will have what they need to finalize.
        Data::Asset<MaterialTypeAsset> testMaterialTypeAsset = CreateTestMaterialTypeAsset(materialTypeAssetId);

        auto layout = testMaterialTypeAsset->GetMaterialPropertiesLayout();
        MaterialPropertyIndex myFloat = layout->FindPropertyIndex(Name("general.MyFloat"));
        MaterialPropertyIndex myFloat2 = layout->FindPropertyIndex(Name("general.MyFloat2"));
        MaterialPropertyIndex myColor = layout->FindPropertyIndex(Name("general.MyColor"));

        
        // The MaterialAsset is still holding an reference to an unloaded asset, so we run it through the serializer which causes the loaded MaterialAsset
        // to have access to the testMaterialTypeAsset. This is similar to how the AP would save the MaterialAsset to the cache and the runtime would load it.
        SerializeTester<RPI::MaterialAsset> tester(GetSerializeContext());
        tester.SerializeOut(materialAssetLevel1.Get());
        materialAssetLevel1 = tester.SerializeIn(Uuid::CreateRandom(), ObjectStream::FilterDescriptor{AZ::Data::AssetFilterNoAssetLoading});
        tester.SerializeOut(materialAssetLevel2.Get());
        materialAssetLevel2 = tester.SerializeIn(Uuid::CreateRandom(), ObjectStream::FilterDescriptor{AZ::Data::AssetFilterNoAssetLoading});
        tester.SerializeOut(materialAssetLevel3.Get());
        materialAssetLevel3 = tester.SerializeIn(Uuid::CreateRandom(), ObjectStream::FilterDescriptor{AZ::Data::AssetFilterNoAssetLoading});

        // The properties will finalize automatically when we call GetPropertyValues()...

        AZStd::span<const MaterialPropertyValue> properties;

        // Check level 1 properties
        properties = materialAssetLevel1->GetPropertyValues();
        EXPECT_EQ(properties[myFloat.GetIndex()].GetValue<float>(), 1.5f);
        EXPECT_EQ(properties[myFloat2.GetIndex()].GetValue<Vector2>(), Vector2(0.0f, 0.0f));
        EXPECT_EQ(properties[myColor.GetIndex()].GetValue<Color>(), Color(0.1f, 0.2f, 0.3f, 0.4f));

        // Check level 2 properties
        properties = materialAssetLevel2->GetPropertyValues();
        EXPECT_EQ(properties[myFloat.GetIndex()].GetValue<float>(), 1.5f);
        EXPECT_EQ(properties[myFloat2.GetIndex()].GetValue<Vector2>(), Vector2(4.1f, 4.2f));
        EXPECT_EQ(properties[myColor.GetIndex()].GetValue<Color>(), Color(0.15f, 0.25f, 0.35f, 0.45f));

        // Check level 3 properties
        properties = materialAssetLevel3->GetPropertyValues();
        EXPECT_EQ(properties[myFloat.GetIndex()].GetValue<float>(), 3.5f);
        EXPECT_EQ(properties[myFloat2.GetIndex()].GetValue<Vector2>(), Vector2(4.1f, 4.2f));
        EXPECT_EQ(properties[myColor.GetIndex()].GetValue<Color>(), Color(0.15f, 0.25f, 0.35f, 0.45f));
    }

    TEST_F(MaterialSourceDataTests, CreateMaterialAsset_MultiLevelDataInheritance_Error_MaterialTypesDontMatch)
    {
        Data::Asset<MaterialTypeAsset> otherMaterialType;
        MaterialTypeAssetCreator materialTypeCreator;
        materialTypeCreator.Begin(Uuid::CreateRandom());
        materialTypeCreator.AddShader(m_testShaderAsset);
        AddCommonTestMaterialProperties(materialTypeCreator, "general.");
        EXPECT_TRUE(materialTypeCreator.End(otherMaterialType));
        m_assetSystemStub.RegisterSourceInfo("otherBase.materialtype", otherMaterialType.GetId());

        MaterialSourceData sourceDataLevel1;
        sourceDataLevel1.m_materialType = "@exefolder@/Temp/test.materialtype";

        MaterialSourceData sourceDataLevel2;
        sourceDataLevel2.m_materialType = "@exefolder@/Temp/test.materialtype";
        sourceDataLevel2.m_parentMaterial = "level1.material";

        MaterialSourceData sourceDataLevel3;
        sourceDataLevel3.m_materialType = "@exefolder@/Temp/otherBase.materialtype";
        sourceDataLevel3.m_parentMaterial = "level2.material";

        auto materialAssetLevel1 = sourceDataLevel1.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::PreBake, true);
        EXPECT_TRUE(materialAssetLevel1.IsSuccess());

        m_assetSystemStub.RegisterSourceInfo("level1.material", materialAssetLevel1.GetValue().GetId());

        auto materialAssetLevel2 = sourceDataLevel2.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::PreBake, true);
        EXPECT_TRUE(materialAssetLevel2.IsSuccess());

        m_assetSystemStub.RegisterSourceInfo("level2.material", materialAssetLevel2.GetValue().GetId());

        AZ_TEST_START_ASSERTTEST;
        auto materialAssetLevel3 = sourceDataLevel3.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::PreBake, true);
        AZ_TEST_STOP_ASSERTTEST(1);
        EXPECT_FALSE(materialAssetLevel3.IsSuccess());
    }

    TEST_F(MaterialSourceDataTests, CreateMaterialAsset_Error_BadInput)
    {
        // We use local functions to easily start a new MaterialAssetCreator for each test case because
        // the AssetCreator would just skip subsequent operations after the first failure is detected.

        auto expectWarning = [](const char* expectedErrorMessage, AZStd::function<void(MaterialSourceData& materialSourceData)> setOneBadInput, bool warningOccursBeforeFinalize = false)
        {
            MaterialSourceData sourceData;

            sourceData.m_materialType = "@exefolder@/Temp/test.materialtype";

            AddPropertyGroup(sourceData, "general");

            setOneBadInput(sourceData);

            // Check with MaterialAssetProcessingMode::PreBake
            {
                ErrorMessageFinder errorFinder;
                errorFinder.AddExpectedErrorMessage(expectedErrorMessage);
                errorFinder.AddIgnoredErrorMessage("Failed to build", true);
                auto materialAssetOutcome = sourceData.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::PreBake, true);
                errorFinder.CheckExpectedErrorsFound();

                EXPECT_FALSE(materialAssetOutcome.IsSuccess());
            }
            
            // Check with MaterialAssetProcessingMode::DeferredBake, no validation occurs because the MaterialTypeAsset cannot be used and so the MaterialAsset is not finalized
            if(!warningOccursBeforeFinalize)
            {
                auto materialAssetOutcome = sourceData.CreateMaterialAsset(Uuid::CreateRandom(), "", MaterialAssetProcessingMode::DeferredBake, true);
                EXPECT_TRUE(materialAssetOutcome.IsSuccess());
            }
        };

        // Test property does not exist...

        expectWarning("\"general.DoesNotExist\" is not found in the material properties layout",
            [](MaterialSourceData& materialSourceData)
            {
                AddProperty(materialSourceData, "general", "DoesNotExist", true);
            });

        expectWarning("\"general.DoesNotExist\" is not found in the material properties layout",
            [](MaterialSourceData& materialSourceData)
            {
                AddProperty(materialSourceData, "general", "DoesNotExist", -10);
            });

        expectWarning("\"general.DoesNotExist\" is not found in the material properties layout",
            [](MaterialSourceData& materialSourceData)
            {
                AddProperty(materialSourceData, "general", "DoesNotExist", 25u);
            });

        expectWarning("\"general.DoesNotExist\" is not found in the material properties layout",
            [](MaterialSourceData& materialSourceData)
            {
                AddProperty(materialSourceData, "general", "DoesNotExist", 1.5f);
            });

        expectWarning("\"general.DoesNotExist\" is not found in the material properties layout",
            [](MaterialSourceData& materialSourceData)
            {
                AddProperty(materialSourceData, "general", "DoesNotExist", AZ::Color{ 0.1f, 0.2f, 0.3f, 0.4f });
            });

        expectWarning("\"general.DoesNotExist\" is not found in the material properties layout",
            [](MaterialSourceData& materialSourceData)
            {
                AddProperty(materialSourceData, "general", "DoesNotExist", AZStd::string("@exefolder@/Temp/test.streamingimage"));
            });

        // Missing image reference
        expectWarning("Could not find the image 'doesNotExist.streamingimage'",
            [](MaterialSourceData& materialSourceData)
            {
                AddProperty(materialSourceData, "general", "MyImage", AZStd::string("doesNotExist.streamingimage"));
            }, true); // In this case, the warning does happen even when the asset is not finalized, because the image path is checked earlier than that
    }
    
    template<typename PropertyTypeT>
    void CheckSimilar(PropertyTypeT a, PropertyTypeT b);
    
    template<> void CheckSimilar<float>(float a, float b) { EXPECT_FLOAT_EQ(a, b); }
    template<> void CheckSimilar<Vector2>(Vector2 a, Vector2 b) { EXPECT_TRUE(a.IsClose(b)); }
    template<> void CheckSimilar<Vector3>(Vector3 a, Vector3 b) { EXPECT_TRUE(a.IsClose(b)); }
    template<> void CheckSimilar<Vector4>(Vector4 a, Vector4 b) { EXPECT_TRUE(a.IsClose(b)); }
    template<> void CheckSimilar<Color>(Color a, Color b) { EXPECT_TRUE(a.IsClose(b)); }

    template<typename PropertyTypeT> void CheckSimilar(PropertyTypeT a, PropertyTypeT b) { EXPECT_EQ(a, b); }

    template<typename PropertyTypeT>
    void CheckEndToEndDataTypeResolution(const char* propertyName, const char* jsonValue, PropertyTypeT expectedFinalValue)
    {
        const char* groupName = "general";

        const AZStd::string inputJson = AZStd::string::format(R"(
            {
                "materialType": "@exefolder@/Temp/test.materialtype",
                "properties": {
                    "%s": {
                        "%s": %s
                    }
                }
            }
        )", groupName, propertyName, jsonValue);

        MaterialSourceData material;
        JsonTestResult loadResult = LoadTestDataFromJson(material, inputJson);
        auto materialAssetResult = material.CreateMaterialAsset(Uuid::CreateRandom(), "test.material", AZ::RPI::MaterialAssetProcessingMode::PreBake);
        EXPECT_TRUE(materialAssetResult);
        MaterialPropertyIndex propertyIndex = materialAssetResult.GetValue()->GetMaterialPropertiesLayout()->FindPropertyIndex(MaterialPropertyId{groupName, propertyName});
        CheckSimilar(expectedFinalValue, materialAssetResult.GetValue()->GetPropertyValues()[propertyIndex.GetIndex()].GetValue<PropertyTypeT>());
    }

    TEST_F(MaterialSourceDataTests, TestEndToEndDataTypeResolution)
    {
        // Data types in .material files don't have to exactly match the types in .materialtype files as specified in the properties layout.
        // The exact location of the data type resolution has moved around over the life of the project, but the important thing is that
        // the data type in the source .material file gets applied correctly by the time a finalized MaterialAsset comes out the other side.
        
        CheckEndToEndDataTypeResolution("MyBool", "true", true);
        CheckEndToEndDataTypeResolution("MyBool", "false", false);
        CheckEndToEndDataTypeResolution("MyBool", "1", true);
        CheckEndToEndDataTypeResolution("MyBool", "0", false);
        CheckEndToEndDataTypeResolution("MyBool", "1.0", true);
        CheckEndToEndDataTypeResolution("MyBool", "0.0", false);
        
        CheckEndToEndDataTypeResolution("MyInt", "5", 5);
        CheckEndToEndDataTypeResolution("MyInt", "-6", -6);
        CheckEndToEndDataTypeResolution("MyInt", "-7.0", -7);
        CheckEndToEndDataTypeResolution("MyInt", "false", 0);
        CheckEndToEndDataTypeResolution("MyInt", "true", 1);
        
        CheckEndToEndDataTypeResolution("MyUInt", "8", 8u);
        CheckEndToEndDataTypeResolution("MyUInt", "9.0", 9u);
        CheckEndToEndDataTypeResolution("MyUInt", "false", 0u);
        CheckEndToEndDataTypeResolution("MyUInt", "true", 1u);
        
        CheckEndToEndDataTypeResolution("MyFloat", "2", 2.0f);
        CheckEndToEndDataTypeResolution("MyFloat", "-2", -2.0f);
        CheckEndToEndDataTypeResolution("MyFloat", "2.1", 2.1f);
        CheckEndToEndDataTypeResolution("MyFloat", "false", 0.0f);
        CheckEndToEndDataTypeResolution("MyFloat", "true", 1.0f);
        
        CheckEndToEndDataTypeResolution("MyColor", "[0.1,0.2,0.3]", Color{0.1f, 0.2f, 0.3f, 1.0});
        CheckEndToEndDataTypeResolution("MyColor", "[0.1, 0.2, 0.3, 0.5]", Color{0.1f, 0.2f, 0.3f, 0.5f});
        CheckEndToEndDataTypeResolution("MyColor", "{\"RGB8\": [255, 0, 255, 0]}", Color{1.0f, 0.0f, 1.0f, 0.0f});
        
        CheckEndToEndDataTypeResolution("MyFloat2", "[0.1,0.2]", Vector2{0.1f, 0.2f});
        CheckEndToEndDataTypeResolution("MyFloat2", "{\"y\":0.2, \"x\":0.1}", Vector2{0.1f, 0.2f});
        CheckEndToEndDataTypeResolution("MyFloat2", "{\"y\":0.2, \"x\":0.1, \"Z\":0.3}", Vector2{0.1f, 0.2f});
        CheckEndToEndDataTypeResolution("MyFloat2", "{\"y\":0.2, \"W\":0.4, \"x\":0.1, \"Z\":0.3}", Vector2{0.1f, 0.2f});
        
        CheckEndToEndDataTypeResolution("MyFloat3", "[0.1,0.2,0.3]", Vector3{0.1f, 0.2f, 0.3f});
        CheckEndToEndDataTypeResolution("MyFloat3", "{\"y\":0.2, \"x\":0.1}", Vector3{0.1f, 0.2f, 0.0f});
        CheckEndToEndDataTypeResolution("MyFloat3", "{\"y\":0.2, \"x\":0.1, \"Z\":0.3}", Vector3{0.1f, 0.2f, 0.3f});
        CheckEndToEndDataTypeResolution("MyFloat3", "{\"y\":0.2, \"W\":0.4, \"x\":0.1, \"Z\":0.3}", Vector3{0.1f, 0.2f, 0.3f});
        
        CheckEndToEndDataTypeResolution("MyFloat4", "[0.1,0.2,0.3,0.4]", Vector4{0.1f, 0.2f, 0.3f, 0.4f});
        CheckEndToEndDataTypeResolution("MyFloat4", "{\"y\":0.2, \"x\":0.1}", Vector4{0.1f, 0.2f, 0.0f, 0.0f});
        CheckEndToEndDataTypeResolution("MyFloat4", "{\"y\":0.2, \"x\":0.1, \"Z\":0.3}", Vector4{0.1f, 0.2f, 0.3f, 0.0f});
        CheckEndToEndDataTypeResolution("MyFloat4", "{\"y\":0.2, \"W\":0.4, \"x\":0.1, \"Z\":0.3}", Vector4{0.1f, 0.2f, 0.3f, 0.4f});
    }
    
}


