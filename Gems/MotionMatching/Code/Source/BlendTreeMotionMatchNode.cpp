/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/smart_ptr/make_shared.h>

#include <EMotionFX/Source/AnimGraph.h>
#include <EMotionFX/Source/AnimGraphManager.h>
#include <EMotionFX/Source/EventManager.h>
#include <EMotionFX/Source/Motion.h>
#include <BlendTreeMotionMatchNode.h>
#include <FeatureSchemaDefault.h>
#include <EMotionFX/Source/MotionSet.h>
#include <EMotionFX/Source/Node.h>
#include <EMotionFX/Source/Recorder.h>
#include <EMotionFX/Source/TransformData.h>

#include <FeaturePosition.h>

namespace EMotionFX::MotionMatching
{
    AZ_CLASS_ALLOCATOR_IMPL(BlendTreeMotionMatchNode, AnimGraphAllocator, 0)
    AZ_CLASS_ALLOCATOR_IMPL(BlendTreeMotionMatchNode::UniqueData, AnimGraphObjectUniqueDataAllocator, 0)

    BlendTreeMotionMatchNode::BlendTreeMotionMatchNode()
        : AnimGraphNode()
    {
        // Setup the input ports.
        InitInputPorts(2);
        SetupInputPort("Goal Pos", INPUTPORT_TARGETPOS, MCore::AttributeVector3::TYPE_ID, PORTID_INPUT_TARGETPOS);
        SetupInputPort("Goal Facing Dir", INPUTPORT_TARGETFACINGDIR, MCore::AttributeVector3::TYPE_ID, PORTID_INPUT_TARGETFACINGDIR);

        // Setup the output ports.
        InitOutputPorts(1);
        SetupOutputPortAsPose("Output Pose", OUTPUTPORT_POSE, PORTID_OUTPUT_POSE);
    }

    BlendTreeMotionMatchNode::~BlendTreeMotionMatchNode()
    {
    }

    bool BlendTreeMotionMatchNode::InitAfterLoading(AnimGraph* animGraph)
    {
        if (!AnimGraphNode::InitAfterLoading(animGraph))
        {
            return false;
        }

        // Automatically register the default feature schema in case the schema is empty after loading the node.
        if (m_featureSchema.GetNumFeatures() == 0)
        {
            AZStd::string rootJointName;
            if (m_animGraph->GetNumAnimGraphInstances() > 0)
            {
                const Actor* actor = m_animGraph->GetAnimGraphInstance(0)->GetActorInstance()->GetActor();
                const Node* rootJoint = actor->GetMotionExtractionNode();
                if (rootJoint)
                {
                    rootJointName = rootJoint->GetNameString();
                }
            }

            DefaultFeatureSchemaInitSettings defaultSettings;
            defaultSettings.m_rootJointName = rootJointName.c_str();
            defaultSettings.m_leftFootJointName = "L_foot_JNT";
            defaultSettings.m_rightFootJointName = "R_foot_JNT";
            defaultSettings.m_pelvisJointName = "C_pelvis_JNT";
            DefaultFeatureSchema(m_featureSchema, defaultSettings);
        }

        InitInternalAttributesForAllInstances();

        Reinit();
        return true;
    }

    const char* BlendTreeMotionMatchNode::GetPaletteName() const
    {
        return "Motion Matching";
    }

    AnimGraphObject::ECategory BlendTreeMotionMatchNode::GetPaletteCategory() const
    {
        return AnimGraphObject::CATEGORY_SOURCES;
    }

    void BlendTreeMotionMatchNode::UniqueData::Update()
    {
        AZ_PROFILE_SCOPE(Animation, "BlendTreeMotionMatchNode::UniqueData::Update");

        auto animGraphNode = azdynamic_cast<BlendTreeMotionMatchNode*>(m_object);
        AZ_Assert(animGraphNode, "Unique data linked to incorrect node type.");

        ActorInstance* actorInstance = m_animGraphInstance->GetActorInstance();

        // Clear existing data.
        delete m_instance;
        delete m_data;

        m_data = aznew MotionMatching::MotionMatchingData(animGraphNode->m_featureSchema);
        m_instance = aznew MotionMatching::MotionMatchingInstance();

        MotionSet* motionSet = m_animGraphInstance->GetMotionSet();
        if (!motionSet)
        {
            SetHasError(true);
            return;
        }

        //---------------------------------
        AZ::Debug::Timer timer;
        timer.Stamp();

        // Build a list of motions we want to import the frames from.
        AZ_Printf("Motion Matching", "Importing motion database...");
        MotionMatching::MotionMatchingData::InitSettings settings;
        settings.m_actorInstance = actorInstance;
        settings.m_frameImportSettings.m_sampleRate = animGraphNode->m_sampleRate;
        settings.m_importMirrored = animGraphNode->m_mirror;
        settings.m_maxKdTreeDepth = animGraphNode->m_maxKdTreeDepth;
        settings.m_minFramesPerKdTreeNode = animGraphNode->m_minFramesPerKdTreeNode;
        settings.m_motionList.reserve(animGraphNode->m_motionIds.size());
        for (const AZStd::string& id : animGraphNode->m_motionIds)
        {
            Motion* motion = motionSet->RecursiveFindMotionById(id);
            if (motion)
            {
                settings.m_motionList.emplace_back(motion);
            }
            else
            {
                AZ_Warning("Motion Matching", false, "Failed to get motion for motionset entry id '%s'", id.c_str());
            }
        }

        // Initialize the motion matching data (slow).
        AZ_Printf("Motion Matching", "Initializing motion matching...");
        if (!m_data->Init(settings))
        {
            AZ_Warning("Motion Matching", false, "Failed to initialize motion matching for anim graph node '%s'!", animGraphNode->GetName());
            SetHasError(true);
            return;
        }

        // Initialize the instance.
        AZ_Printf("Motion Matching", "Initializing instance...");
        MotionMatching::MotionMatchingInstance::InitSettings initSettings;
        initSettings.m_actorInstance = actorInstance;
        initSettings.m_data = m_data;
        m_instance->Init(initSettings);

        const float initTime = timer.GetDeltaTimeInSeconds();
        const size_t memUsage = m_data->GetFrameDatabase().CalcMemoryUsageInBytes();
        AZ_Printf("Motion Matching", "Finished in %.2f seconds (mem usage=%d bytes or %.2f mb)", initTime, memUsage, memUsage / (float)(1024 * 1024));
        //---------------------------------

        SetHasError(false);
    }

    void BlendTreeMotionMatchNode::Update(AnimGraphInstance* animGraphInstance, float timePassedInSeconds)
    {
        AZ_PROFILE_SCOPE(Animation, "BlendTreeMotionMatchNode::Update");

        m_timer.Stamp();
            
        UniqueData* uniqueData = static_cast<UniqueData*>(FindOrCreateUniqueNodeData(animGraphInstance));
        UpdateAllIncomingNodes(animGraphInstance, timePassedInSeconds);
        uniqueData->Clear();
        if (uniqueData->GetHasError())
        {
            m_updateTimeInMs = 0.0f;
            m_postUpdateTimeInMs = 0.0f;
            m_outputTimeInMs = 0.0f;
            return;
        }

        AZ::Vector3 targetPos = AZ::Vector3::CreateZero();
        TryGetInputVector3(animGraphInstance, INPUTPORT_TARGETPOS, targetPos);

        AZ::Vector3 targetFacingDir = AZ::Vector3::CreateAxisY();
        TryGetInputVector3(animGraphInstance, INPUTPORT_TARGETFACINGDIR, targetFacingDir);

        MotionMatching::MotionMatchingInstance* instance = uniqueData->m_instance;
        instance->Update(timePassedInSeconds, targetPos, targetFacingDir, m_trajectoryQueryMode, m_pathRadius, m_pathSpeed);

        // set the current time to the new calculated time
        uniqueData->ClearInheritFlags();
        uniqueData->SetPreSyncTime(instance->GetMotionInstance()->GetCurrentTime());
        uniqueData->SetCurrentPlayTime(instance->GetNewMotionTime());

        if (uniqueData->GetPreSyncTime() > uniqueData->GetCurrentPlayTime())
        {
            uniqueData->SetPreSyncTime(uniqueData->GetCurrentPlayTime());
        }
            
        m_updateTimeInMs = m_timer.GetDeltaTimeInSeconds() * 1000.0f;
    }

    void BlendTreeMotionMatchNode::PostUpdate(AnimGraphInstance* animGraphInstance, float timePassedInSeconds)
    {
        AZ_PROFILE_SCOPE(Animation, "BlendTreeMotionMatchNode::PostUpdate");

        AZ_UNUSED(animGraphInstance);
        AZ_UNUSED(timePassedInSeconds);
        m_timer.Stamp();

        for (AZ::u32 i = 0; i < GetNumConnections(); ++i)
        {
            AnimGraphNode* node = GetConnection(i)->GetSourceNode();
            node->PerformPostUpdate(animGraphInstance, timePassedInSeconds);
        }

        UniqueData* uniqueData = static_cast<UniqueData*>(FindOrCreateUniqueNodeData(animGraphInstance));
        MotionMatching::MotionMatchingInstance* instance = uniqueData->m_instance;

        RequestRefDatas(animGraphInstance);
        AnimGraphRefCountedData* data = uniqueData->GetRefCountedData();
        data->ClearEventBuffer();
        data->ZeroTrajectoryDelta();

        if (uniqueData->GetHasError())
        {
            return;
        }

        MotionInstance* motionInstance = instance->GetMotionInstance();
        motionInstance->UpdateByTimeValues(uniqueData->GetPreSyncTime(), uniqueData->GetCurrentPlayTime(), &data->GetEventBuffer());

        uniqueData->SetCurrentPlayTime(motionInstance->GetCurrentTime());
        data->GetEventBuffer().UpdateEmitters(this);

        instance->PostUpdate(timePassedInSeconds);

        const Transform& trajectoryDelta = instance->GetMotionExtractionDelta();
        data->SetTrajectoryDelta(trajectoryDelta);
        data->SetTrajectoryDeltaMirrored(trajectoryDelta); // TODO: use a real mirrored version here.

        m_postUpdateTimeInMs = m_timer.GetDeltaTimeInSeconds() * 1000.0f;
    }

    void BlendTreeMotionMatchNode::Output(AnimGraphInstance* animGraphInstance)
    {
        AZ_PROFILE_SCOPE(Animation, "BlendTreeMotionMatchNode::Output");

        AZ_UNUSED(animGraphInstance);
        m_timer.Stamp();

        AnimGraphPose* outputPose;

        // Initialize to bind pose.
        ActorInstance* actorInstance = animGraphInstance->GetActorInstance();
        RequestPoses(animGraphInstance);
        outputPose = GetOutputPose(animGraphInstance, OUTPUTPORT_POSE)->GetValue();
        outputPose->InitFromBindPose(actorInstance);

        if (m_disabled)
        {
            return;
        }

        UniqueData* uniqueData = static_cast<UniqueData*>(FindOrCreateUniqueNodeData(animGraphInstance));
        if (GetEMotionFX().GetIsInEditorMode())
        {
            SetHasError(uniqueData, uniqueData->GetHasError());
        }

        if (uniqueData->GetHasError())
        {
            return;
        }

        OutputIncomingNode(animGraphInstance, GetInputNode(INPUTPORT_TARGETPOS));
        OutputIncomingNode(animGraphInstance, GetInputNode(INPUTPORT_TARGETFACINGDIR));

        MotionMatching::MotionMatchingInstance* instance = uniqueData->m_instance;
        instance->SetLowestCostSearchFrequency(m_lowestCostSearchFrequency);

        Pose& outTransformPose = outputPose->GetPose();
        instance->Output(outTransformPose);

        // Performance metrics
        m_outputTimeInMs = m_timer.GetDeltaTimeInSeconds() * 1000.0f;
        {
            //AZ_Printf("MotionMatch", "Update = %.2f, PostUpdate = %.2f, Output = %.2f", m_updateTime, m_postUpdateTime, m_outputTime);
#ifdef IMGUI_ENABLED
            ImGuiMonitorRequestBus::Broadcast(&ImGuiMonitorRequests::PushPerformanceHistogramValue, "Update", m_updateTimeInMs);
            ImGuiMonitorRequestBus::Broadcast(&ImGuiMonitorRequests::PushPerformanceHistogramValue, "Post Update", m_postUpdateTimeInMs);
            ImGuiMonitorRequestBus::Broadcast(&ImGuiMonitorRequests::PushPerformanceHistogramValue, "Output", m_outputTimeInMs);
#endif
        }

        instance->DebugDraw();
    }

    void BlendTreeMotionMatchNode::Reflect(AZ::ReflectContext* context)
    {
        AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (!serializeContext)
        {
            return;
        }

        serializeContext->Class<BlendTreeMotionMatchNode, AnimGraphNode>()
            ->Version(9)
            ->Field("sampleRate", &BlendTreeMotionMatchNode::m_sampleRate)
            ->Field("lowestCostSearchFrequency", &BlendTreeMotionMatchNode::m_lowestCostSearchFrequency)
            ->Field("maxKdTreeDepth", &BlendTreeMotionMatchNode::m_maxKdTreeDepth)
            ->Field("minFramesPerKdTreeNode", &BlendTreeMotionMatchNode::m_minFramesPerKdTreeNode)
            ->Field("mirror", &BlendTreeMotionMatchNode::m_mirror)
            ->Field("controlSplineMode", &BlendTreeMotionMatchNode::m_trajectoryQueryMode)
            ->Field("pathRadius", &BlendTreeMotionMatchNode::m_pathRadius)
            ->Field("pathSpeed", &BlendTreeMotionMatchNode::m_pathSpeed)
            ->Field("featureSchema", &BlendTreeMotionMatchNode::m_featureSchema)
            ->Field("motionIds", &BlendTreeMotionMatchNode::m_motionIds)
            ;

        AZ::EditContext* editContext = serializeContext->GetEditContext();
        if (!editContext)
        {
            return;
        }

        editContext->Class<BlendTreeMotionMatchNode>("Motion Matching Node", "Motion Matching Attributes")
            ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                ->Attribute(AZ::Edit::Attributes::AutoExpand, "")
                ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
            ->DataElement(AZ::Edit::UIHandlers::Default, &BlendTreeMotionMatchNode::m_sampleRate, "Feature sample rate", "The sample rate (in Hz) used for extracting the features from the animations. The higher the sample rate, the more data will be used and the more options the motion matching search has available for the best matching frame.")
                ->Attribute(AZ::Edit::Attributes::Min, 1)
                ->Attribute(AZ::Edit::Attributes::Max, 240)
                ->Attribute(AZ::Edit::Attributes::ChangeNotify, &BlendTreeMotionMatchNode::Reinit)
            ->DataElement(AZ::Edit::UIHandlers::Default, &BlendTreeMotionMatchNode::m_lowestCostSearchFrequency, "Search frequency", "How often per second we apply the motion matching search and find the lowest cost / best matching frame, and start to blend towards it.")
                ->Attribute(AZ::Edit::Attributes::Min, 0.001f)
                ->Attribute(AZ::Edit::Attributes::Max, std::numeric_limits<float>::max())
                ->Attribute(AZ::Edit::Attributes::Step, 0.05f)
            ->DataElement(AZ::Edit::UIHandlers::Default, &BlendTreeMotionMatchNode::m_maxKdTreeDepth, "Max kdTree depth", "The maximum number of hierarchy levels in the kdTree.")
            ->Attribute(AZ::Edit::Attributes::Min, 1)
            ->Attribute(AZ::Edit::Attributes::Max, 20)
            ->Attribute(AZ::Edit::Attributes::ChangeNotify, &BlendTreeMotionMatchNode::Reinit)
            ->DataElement(AZ::Edit::UIHandlers::Default, &BlendTreeMotionMatchNode::m_minFramesPerKdTreeNode, "Min kdTree node size", "The minimum number of frames to store per kdTree node.")
            ->Attribute(AZ::Edit::Attributes::Min, 1)
            ->Attribute(AZ::Edit::Attributes::Max, 100000)
            ->Attribute(AZ::Edit::Attributes::ChangeNotify, &BlendTreeMotionMatchNode::Reinit)
            ->DataElement(AZ::Edit::UIHandlers::Default, &BlendTreeMotionMatchNode::m_pathRadius, "Path radius", "")
            ->Attribute(AZ::Edit::Attributes::Min, 0.0001f)
            ->Attribute(AZ::Edit::Attributes::Max, std::numeric_limits<float>::max())
            ->Attribute(AZ::Edit::Attributes::Step, 0.01f)
            ->DataElement(AZ::Edit::UIHandlers::Default, &BlendTreeMotionMatchNode::m_pathSpeed, "Path speed", "")
            ->Attribute(AZ::Edit::Attributes::Min, 0.0001f)
            ->Attribute(AZ::Edit::Attributes::Max, std::numeric_limits<float>::max())
            ->Attribute(AZ::Edit::Attributes::Step, 0.01f)
            ->DataElement(AZ::Edit::UIHandlers::ComboBox, &BlendTreeMotionMatchNode::m_trajectoryQueryMode, "Trajectory mode", "Desired future trajectory generation mode.")
                ->EnumAttribute(TrajectoryQuery::MODE_TARGETDRIVEN, "Target driven")
                ->EnumAttribute(TrajectoryQuery::MODE_ONE, "Mode one")
                ->EnumAttribute(TrajectoryQuery::MODE_TWO, "Mode two")
                ->EnumAttribute(TrajectoryQuery::MODE_THREE, "Mode three")
                ->EnumAttribute(TrajectoryQuery::MODE_FOUR, "Mode four")
            ->DataElement(AZ::Edit::UIHandlers::Default, &BlendTreeMotionMatchNode::m_featureSchema, "FeatureSchema", "")
                ->Attribute(AZ::Edit::Attributes::AutoExpand, "")
                ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                ->Attribute(AZ::Edit::Attributes::ChangeNotify, &BlendTreeMotionMatchNode::Reinit)
            ->DataElement(AZ_CRC("MotionSetMotionIds", 0x8695c0fa), &BlendTreeMotionMatchNode::m_motionIds, "Motions", "")
                ->Attribute(AZ::Edit::Attributes::ChangeNotify, &BlendTreeMotionMatchNode::Reinit)
                ->Attribute(AZ::Edit::Attributes::ContainerCanBeModified, false)
                ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::HideChildren)
            ;
    }
} // namespace EMotionFX::MotionMatching
