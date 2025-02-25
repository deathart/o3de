/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzFramework/Viewport/ViewportControllerList.h>
#include <AzFramework/Viewport/ViewportColors.h>
#include <AzToolsFramework/Viewport/ViewportTypes.h>
#include <AzToolsFramework/Viewport/ViewportSettings.h>
#include <AzToolsFramework/Manipulators/ManipulatorManager.h>

#include <EMStudio/AtomRenderPlugin.h>
#include <EMStudio/AnimViewportRenderer.h>
#include <EMStudio/AnimViewportToolBar.h>
#include <EMStudio/AnimViewportInputController.h>

#include <Integration/Components/ActorComponent.h>
#include <EMotionFX/CommandSystem/Source/CommandManager.h>
#include <EMotionFX/Source/Allocators.h>
#include <EMotionFX/Tools/EMotionStudio/EMStudioSDK/Source/EMStudioManager.h>
#include <QHBoxLayout>
#include <QGuiApplication>


namespace EMStudio
{
    AZ_CLASS_ALLOCATOR_IMPL(AtomRenderPlugin, EMotionFX::EditorAllocator, 0);

    AtomRenderPlugin::AtomRenderPlugin()
        : DockWidgetPlugin()
        , m_translationManipulators(
              AzToolsFramework::TranslationManipulators::Dimensions::Three, AZ::Transform::Identity(), AZ::Vector3::CreateOne())
        , m_rotateManipulators(AZ::Transform::CreateIdentity())
        , m_scaleManipulators(AZ::Transform::CreateIdentity())
    {
    }

    AtomRenderPlugin::~AtomRenderPlugin()
    {
        SaveRenderOptions();
        GetCommandManager()->RemoveCommandCallback(m_importActorCallback, false);
        GetCommandManager()->RemoveCommandCallback(m_removeActorCallback, false);
        delete m_importActorCallback;
        delete m_removeActorCallback;

        AzToolsFramework::ViewportInteraction::ViewportMouseRequestBus::Handler::BusDisconnect();
    }

    const char* AtomRenderPlugin::GetName() const
    {
        return "Atom Render Window (Preview)";
    }

    uint32 AtomRenderPlugin::GetClassID() const
    {
        return static_cast<uint32>(AtomRenderPlugin::CLASS_ID);
    }

    const char* AtomRenderPlugin::GetCreatorName() const
    {
        return "O3DE";
    }

    float AtomRenderPlugin::GetVersion() const
    {
        return 1.0f;
    }

    bool AtomRenderPlugin::GetIsClosable() const
    {
        return true;
    }

    bool AtomRenderPlugin::GetIsFloatable() const
    {
        return true;
    }

    bool AtomRenderPlugin::GetIsVertical() const
    {
        return false;
    }

    EMStudioPlugin* AtomRenderPlugin::Clone()
    {
        return new AtomRenderPlugin();
    }

    EMStudioPlugin::EPluginType AtomRenderPlugin::GetPluginType() const
    {
        return EMStudioPlugin::PLUGINTYPE_RENDERING;
    }

    QWidget* AtomRenderPlugin::GetInnerWidget()
    {
        return m_innerWidget;
    }

    void AtomRenderPlugin::ReinitRenderer()
    {
        m_animViewportWidget->Reinit();
        SetManipulatorMode(m_renderOptions.GetManipulatorMode());
    }

    bool AtomRenderPlugin::Init()
    {
        LoadRenderOptions();

        m_innerWidget = new QWidget();
        m_dock->setWidget(m_innerWidget);

        QVBoxLayout* verticalLayout = new QVBoxLayout(m_innerWidget);
        verticalLayout->setSizeConstraint(QLayout::SetNoConstraint);
        verticalLayout->setSpacing(1);
        verticalLayout->setMargin(0);

        // Add the viewport widget
        m_animViewportWidget = new AnimViewportWidget(this);

        // Add the tool bar
        AnimViewportToolBar* toolBar = new AnimViewportToolBar(this, m_innerWidget);

        verticalLayout->addWidget(toolBar);
        verticalLayout->addWidget(m_animViewportWidget);

        m_manipulatorManager = AZStd::make_shared<AzToolsFramework::ManipulatorManager>(g_animManipulatorManagerId);
        SetupManipulators();

        // Register command callbacks.
        m_importActorCallback = new ImportActorCallback(false);
        m_removeActorCallback = new RemoveActorCallback(false);
        EMStudioManager::GetInstance()->GetCommandManager()->RegisterCommandCallback("ImportActor", m_importActorCallback);
        EMStudioManager::GetInstance()->GetCommandManager()->RegisterCommandCallback("RemoveActor", m_removeActorCallback);

        AzToolsFramework::ViewportInteraction::ViewportMouseRequestBus::Handler::BusConnect(
            m_animViewportWidget->GetViewportContext()->GetId());

        return true;
    }

    void AtomRenderPlugin::SetupManipulators()
    {
        // Add the manipulator controller
        m_animViewportWidget->GetControllerList()->Add(AZStd::make_shared<AnimViewportInputController>());

        // Gather information about the entity
        AZ::Transform worldTransform = AZ::Transform::CreateIdentity();
        worldTransform.SetTranslation(m_animViewportWidget->GetAnimViewportRenderer()->GetCharacterCenter());

        // Setup the translation manipulator
        m_translationManipulators.SetSpace(worldTransform);
        AzToolsFramework::ConfigureTranslationManipulatorAppearance3d(&m_translationManipulators);
        m_translationManipulators.InstallLinearManipulatorMouseMoveCallback(
            [this](const AzToolsFramework::LinearManipulator::Action& action)
            {
                OnManipulatorMoved(action.LocalPosition());
            });

        m_translationManipulators.InstallPlanarManipulatorMouseMoveCallback(
            [this](const AzToolsFramework::PlanarManipulator::Action& action)
            {
                OnManipulatorMoved(action.LocalPosition());
            });

        m_translationManipulators.InstallSurfaceManipulatorMouseMoveCallback(
            [this](const AzToolsFramework::SurfaceManipulator::Action& action)
            {
                OnManipulatorMoved(action.LocalPosition());
            });

        // Setup the rotation manipulator
        m_rotateManipulators.SetSpace(worldTransform);
        m_rotateManipulators.SetLocalAxes(AZ::Vector3::CreateAxisX(), AZ::Vector3::CreateAxisY(), AZ::Vector3::CreateAxisZ());
        m_rotateManipulators.ConfigureView(
            AzToolsFramework::RotationManipulatorRadius(), AzFramework::ViewportColors::XAxisColor, AzFramework::ViewportColors::YAxisColor,
            AzFramework::ViewportColors::ZAxisColor);
        m_rotateManipulators.InstallMouseMoveCallback(
            [this](const AzToolsFramework::AngularManipulator::Action& action)
            {
                OnManipulatorRotated(action.LocalOrientation());
            });

        // Setup the scale manipulator
        m_scaleManipulators.SetSpace(worldTransform);
        m_scaleManipulators.SetAxes(AZ::Vector3::CreateAxisX(), AZ::Vector3::CreateAxisY(), AZ::Vector3::CreateAxisZ());
        m_scaleManipulators.ConfigureView(
            AzToolsFramework::LinearManipulatorAxisLength(), AzFramework::ViewportColors::XAxisColor,
            AzFramework::ViewportColors::YAxisColor, AzFramework::ViewportColors::ZAxisColor);
        m_scaleManipulators.InstallAxisMouseMoveCallback(
            [this](const AzToolsFramework::LinearManipulator::Action& action)
            {
                OnManipulatorScaled(action.LocalScale(), action.LocalScaleOffset());
            });
    }

    void AtomRenderPlugin::SetManipulatorMode(RenderOptions::ManipulatorMode mode)
    {
        if (!m_manipulatorManager)
        {
            return;
        }

        if (mode == RenderOptions::ManipulatorMode::SELECT)
        {
            m_translationManipulators.Unregister();
            m_rotateManipulators.Unregister();
            m_scaleManipulators.Unregister();
            return;
        }

        const AZ::EntityId entityId = m_animViewportWidget->GetAnimViewportRenderer()->GetEntityId();
        if (!entityId.IsValid())
        {
            return;
        }

        AZ::Vector3 localPos;
        AZ::TransformBus::EventResult(localPos, entityId, &AZ::TransformBus::Events::GetLocalTranslation);

        switch (mode)
        {
        case RenderOptions::ManipulatorMode::SELECT:
            // The AtomRenderPlugin doesn't implement a select mode
            break;
        case RenderOptions::ManipulatorMode::TRANSLATE:
            {
                m_translationManipulators.Register(g_animManipulatorManagerId);
                m_translationManipulators.SetLocalPosition(localPos);
                m_rotateManipulators.Unregister();
                m_scaleManipulators.Unregister();
            }
            break;
        case RenderOptions::ManipulatorMode::ROTATE:
            {
                m_translationManipulators.Unregister();
                m_rotateManipulators.Register(g_animManipulatorManagerId);
                m_rotateManipulators.SetLocalPosition(localPos);
                m_scaleManipulators.Unregister();
            }
            break;
        case RenderOptions::ManipulatorMode::SCALE:
            {
                m_translationManipulators.Unregister();
                m_rotateManipulators.Unregister();
                m_scaleManipulators.Register(g_animManipulatorManagerId);
                m_scaleManipulators.SetLocalPosition(localPos);
            }
            break;
        }
    }

    void AtomRenderPlugin::OnManipulatorMoved(const AZ::Vector3& position)
    {
        m_translationManipulators.SetLocalPosition(position);
        const AZ::EntityId entityId = m_animViewportWidget->GetAnimViewportRenderer()->GetEntityId();
        AZ::TransformBus::Event(entityId, &AZ::TransformBus::Events::SetLocalTranslation, position);
    }

    void AtomRenderPlugin::OnManipulatorRotated(const AZ::Quaternion& rotation)
    {
        const AZ::EntityId entityId = m_animViewportWidget->GetAnimViewportRenderer()->GetEntityId();
        AZ::TransformBus::Event(entityId, &AZ::TransformBus::Events::SetLocalRotationQuaternion, rotation);
    }

    void AtomRenderPlugin::OnManipulatorScaled(
        const AZ::Vector3& scale, const AZ::Vector3& scaleOffset)
    {
        // Use the scaleOffset to determine which axis to use on the uniform scale.
        float localScale = 1.0f;
        if (scaleOffset.GetX() != 0.0f)
        {
            localScale = scale.GetX();
        }
        else if (scaleOffset.GetY() != 0.0f)
        {
            localScale = scale.GetY();
        }
        else if (scaleOffset.GetZ() != 0.0f)
        {
            localScale = scale.GetZ();
        }
        const AZ::EntityId entityId = m_animViewportWidget->GetAnimViewportRenderer()->GetEntityId();
        AZ::TransformBus::Event(entityId, &AZ::TransformBus::Events::SetLocalUniformScale, localScale);
    }

    void AtomRenderPlugin::LoadRenderOptions()
    {
        AZStd::string renderOptionsFilename(GetManager()->GetAppDataFolder());
        renderOptionsFilename += "EMStudioRenderOptions.cfg";
        QSettings settings(renderOptionsFilename.c_str(), QSettings::IniFormat, this);
        m_renderOptions = RenderOptions::Load(&settings);
    }

    void AtomRenderPlugin::SaveRenderOptions()
    {
        AZStd::string renderOptionsFilename(GetManager()->GetAppDataFolder());
        renderOptionsFilename += "EMStudioRenderOptions.cfg";
        QSettings settings(renderOptionsFilename.c_str(), QSettings::IniFormat, this);
        m_renderOptions.Save(&settings);
    }

    RenderOptions* AtomRenderPlugin::GetRenderOptions()
    {
        return &m_renderOptions;
    }

    void AtomRenderPlugin::Render([[maybe_unused]]EMotionFX::ActorRenderFlagBitset renderFlags)
    {
        if (!m_animViewportWidget)
        {
            return;
        }

        AzFramework::DebugDisplayRequestBus::BusPtr debugDisplayBus;
        AzFramework::DebugDisplayRequestBus::Bind(debugDisplayBus, m_animViewportWidget->GetViewportContext()->GetId());
        AzFramework::DebugDisplayRequests* debugDisplay = AzFramework::DebugDisplayRequestBus::FindFirstHandler(debugDisplayBus);

        namespace AztfVi = AzToolsFramework::ViewportInteraction;
        AztfVi::KeyboardModifiers keyboardModifiers;
        AztfVi::EditorModifierKeyRequestBus::BroadcastResult(
            keyboardModifiers, &AztfVi::EditorModifierKeyRequestBus::Events::QueryKeyboardModifiers);

        debugDisplay->DepthTestOff();
        const AzFramework::ScreenPoint screenPoint = AztfVi::ScreenPointFromQPoint(m_animViewportWidget->mapFromGlobal(QCursor::pos()));
        m_manipulatorManager->DrawManipulators(
            *debugDisplay, m_animViewportWidget->GetCameraState(),
            AztfVi::BuildMouseInteraction(
                AztfVi::BuildMousePick(m_animViewportWidget->GetCameraState(), screenPoint),
                AztfVi::MouseButtons(AztfVi::TranslateMouseButtons(QGuiApplication::mouseButtons())),
                AztfVi::InteractionId(AZ::EntityId(), m_animViewportWidget->GetViewportContext()->GetId()), keyboardModifiers ));
        debugDisplay->DepthTestOn();
    }

    bool AtomRenderPlugin::HandleMouseInteraction(
        const AzToolsFramework::ViewportInteraction::MouseInteractionEvent& mouseInteractionEvent)
    {
        if (!m_manipulatorManager)
        {
            return false;
        }

        using AzToolsFramework::ViewportInteraction::MouseEvent;
        const auto& mouseInteraction = mouseInteractionEvent.m_mouseInteraction;

        switch (mouseInteractionEvent.m_mouseEvent)
        {
        case MouseEvent::Down:
            return m_manipulatorManager->ConsumeViewportMousePress(mouseInteraction);
        case MouseEvent::DoubleClick:
            return false;
        case MouseEvent::Move:
            {
                const AzToolsFramework::ManipulatorManager::ConsumeMouseMoveResult mouseMoveResult =
                    m_manipulatorManager->ConsumeViewportMouseMove(mouseInteraction);
                return mouseMoveResult == AzToolsFramework::ManipulatorManager::ConsumeMouseMoveResult::Interacting;
            }
        case MouseEvent::Up:
            return m_manipulatorManager->ConsumeViewportMouseRelease(mouseInteraction);
        case MouseEvent::Wheel:
            return m_manipulatorManager->ConsumeViewportMouseWheel(mouseInteraction);
        default:
            return false;
        }
    }

    // Command callbacks
    bool ReinitAtomRenderPlugin()
    {
        EMStudioPlugin* plugin = EMStudio::GetPluginManager()->FindActivePlugin(static_cast<uint32>(AtomRenderPlugin::CLASS_ID));
        if (!plugin)
        {
            AZ_Error("AtomRenderPlugin", false, "Cannot execute command callback. Atom render plugin does not exist.");
            return false;
        }

        AtomRenderPlugin* atomRenderPlugin = static_cast<AtomRenderPlugin*>(plugin);
        atomRenderPlugin->ReinitRenderer();

        return true;
    }

    bool AtomRenderPlugin::ImportActorCallback::Execute(
        [[maybe_unused]] MCore::Command* command, [[maybe_unused]] const MCore::CommandLine& commandLine)
    {
        return ReinitAtomRenderPlugin();
    }
    bool AtomRenderPlugin::ImportActorCallback::Undo(
        [[maybe_unused]] MCore::Command* command, [[maybe_unused]] const MCore::CommandLine& commandLine)
    {
        return ReinitAtomRenderPlugin();
    }

    bool AtomRenderPlugin::RemoveActorCallback::Execute(
        [[maybe_unused]] MCore::Command* command, [[maybe_unused]] const MCore::CommandLine& commandLine)
    {
        return ReinitAtomRenderPlugin();
    }
    bool AtomRenderPlugin::RemoveActorCallback::Undo(
        [[maybe_unused]] MCore::Command* command, [[maybe_unused]] const MCore::CommandLine& commandLine)
    {
        return ReinitAtomRenderPlugin();
    }
}// namespace EMStudio
