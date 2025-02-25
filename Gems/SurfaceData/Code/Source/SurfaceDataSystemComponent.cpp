/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Debug/Profiler.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/std/sort.h>

#include "SurfaceDataSystemComponent.h"
#include <SurfaceData/SurfaceDataConstants.h>
#include <SurfaceData/SurfaceTag.h>
#include <SurfaceData/SurfaceDataSystemNotificationBus.h>
#include <SurfaceData/SurfaceDataProviderRequestBus.h>
#include <SurfaceData/SurfaceDataModifierRequestBus.h>
#include <SurfaceData/Utility/SurfaceDataUtility.h>


namespace SurfaceData
{
    void SurfaceDataSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        SurfaceTag::Reflect(context);

        if (AZ::SerializeContext* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<SurfaceDataSystemComponent, AZ::Component>()
                ->Version(0)
                ;

            if (AZ::EditContext* ec = serialize->GetEditContext())
            {
                ec->Class<SurfaceDataSystemComponent>("Surface Data System", "Manages registration of surface data providers and forwards intersection data requests to them")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC("System", 0xc94d118b))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ;
            }
        }

        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->Class<SurfacePoint>()
                ->Constructor()
                ->Attribute(AZ::Script::Attributes::Category, "Vegetation")
                ->Attribute(AZ::Script::Attributes::Module, "surface_data")
                ->Property("entityId", BehaviorValueProperty(&SurfacePoint::m_entityId))
                ->Property("position", BehaviorValueProperty(&SurfacePoint::m_position))
                ->Property("normal", BehaviorValueProperty(&SurfacePoint::m_normal))
                ->Property("masks", BehaviorValueProperty(&SurfacePoint::m_masks))
                ;

            behaviorContext->Class<SurfaceDataSystemComponent>()
                ->RequestBus("SurfaceDataSystemRequestBus")
                ;

            behaviorContext->EBus<SurfaceDataSystemRequestBus>("SurfaceDataSystemRequestBus")
                ->Attribute(AZ::Script::Attributes::Category, "Vegetation")
                ->Attribute(AZ::Script::Attributes::Module, "surface_data")
                ->Event("GetSurfacePoints", &SurfaceDataSystemRequestBus::Events::GetSurfacePoints)
                ;

            behaviorContext->EBus<SurfaceDataSystemNotificationBus>("SurfaceDataSystemNotificationBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Vegetation")
                ->Attribute(AZ::Script::Attributes::Module, "surface_data")
                ->Event("OnSurfaceChanged", &SurfaceDataSystemNotificationBus::Events::OnSurfaceChanged)
                ;
        }
    }

    void SurfaceDataSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC("SurfaceDataSystemService", 0x1d44d25f));
    }

    void SurfaceDataSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC("SurfaceDataSystemService", 0x1d44d25f));
    }

    void SurfaceDataSystemComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        (void)required;
    }

    void SurfaceDataSystemComponent::GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
        (void)dependent;
    }

    void SurfaceDataSystemComponent::Init()
    {
    }

    void SurfaceDataSystemComponent::Activate()
    {
        SurfaceDataSystemRequestBus::Handler::BusConnect();
    }

    void SurfaceDataSystemComponent::Deactivate()
    {
        SurfaceDataSystemRequestBus::Handler::BusDisconnect();
    }

    SurfaceDataRegistryHandle SurfaceDataSystemComponent::RegisterSurfaceDataProvider(const SurfaceDataRegistryEntry& entry)
    {
        const SurfaceDataRegistryHandle handle = RegisterSurfaceDataProviderInternal(entry);
        if (handle != InvalidSurfaceDataRegistryHandle)
        {
            // Send in the entry's bounds as both the old and new bounds, since a null Aabb for old bounds
            // would cause *all* vegetation sectors to get marked as dirty.
            SurfaceDataSystemNotificationBus::Broadcast(&SurfaceDataSystemNotificationBus::Events::OnSurfaceChanged, entry.m_entityId, entry.m_bounds, entry.m_bounds);
        }
        return handle;
    }

    void SurfaceDataSystemComponent::UnregisterSurfaceDataProvider(const SurfaceDataRegistryHandle& handle)
    {
        const SurfaceDataRegistryEntry entry = UnregisterSurfaceDataProviderInternal(handle);
        if (entry.m_entityId.IsValid())
        {
            // Send in the entry's bounds as both the old and new bounds, since a null Aabb for new bounds
            // would cause *all* vegetation sectors to get marked as dirty.
            SurfaceDataSystemNotificationBus::Broadcast(&SurfaceDataSystemNotificationBus::Events::OnSurfaceChanged, entry.m_entityId, entry.m_bounds, entry.m_bounds);
        }
    }

    void SurfaceDataSystemComponent::UpdateSurfaceDataProvider(const SurfaceDataRegistryHandle& handle, const SurfaceDataRegistryEntry& entry)
    {
        AZ::Aabb oldBounds = AZ::Aabb::CreateNull();

        if (UpdateSurfaceDataProviderInternal(handle, entry, oldBounds))
        {
            SurfaceDataSystemNotificationBus::Broadcast(&SurfaceDataSystemNotificationBus::Events::OnSurfaceChanged, entry.m_entityId, oldBounds, entry.m_bounds);
        }
    }

    SurfaceDataRegistryHandle SurfaceDataSystemComponent::RegisterSurfaceDataModifier(const SurfaceDataRegistryEntry& entry)
    {
        const SurfaceDataRegistryHandle handle = RegisterSurfaceDataModifierInternal(entry);
        if (handle != InvalidSurfaceDataRegistryHandle)
        {
            // Send in the entry's bounds as both the old and new bounds, since a null Aabb for old bounds
            // would cause *all* vegetation sectors to get marked as dirty.
            SurfaceDataSystemNotificationBus::Broadcast(&SurfaceDataSystemNotificationBus::Events::OnSurfaceChanged, entry.m_entityId, entry.m_bounds, entry.m_bounds);
        }
        return handle;
    }

    void SurfaceDataSystemComponent::UnregisterSurfaceDataModifier(const SurfaceDataRegistryHandle& handle)
    {
        const SurfaceDataRegistryEntry entry = UnregisterSurfaceDataModifierInternal(handle);
        if (entry.m_entityId.IsValid())
        {
            // Send in the entry's bounds as both the old and new bounds, since a null Aabb for new bounds
            // would cause *all* vegetation sectors to get marked as dirty.
            SurfaceDataSystemNotificationBus::Broadcast(&SurfaceDataSystemNotificationBus::Events::OnSurfaceChanged, entry.m_entityId, entry.m_bounds, entry.m_bounds);
        }
    }

    void SurfaceDataSystemComponent::UpdateSurfaceDataModifier(const SurfaceDataRegistryHandle& handle, const SurfaceDataRegistryEntry& entry)
    {
        AZ::Aabb oldBounds = AZ::Aabb::CreateNull();

        if (UpdateSurfaceDataModifierInternal(handle, entry, oldBounds))
        {
            SurfaceDataSystemNotificationBus::Broadcast(&SurfaceDataSystemNotificationBus::Events::OnSurfaceChanged, entry.m_entityId, oldBounds, entry.m_bounds);
        }
    }

    void SurfaceDataSystemComponent::RefreshSurfaceData(const AZ::Aabb& dirtyBounds)
    {
        SurfaceDataSystemNotificationBus::Broadcast(&SurfaceDataSystemNotificationBus::Events::OnSurfaceChanged, AZ::EntityId(), dirtyBounds, dirtyBounds);
    }

    void SurfaceDataSystemComponent::GetSurfacePoints(const AZ::Vector3& inPosition, const SurfaceTagVector& desiredTags, SurfacePointList& surfacePointList) const
    {
        const bool useTagFilters = HasValidTags(desiredTags);
        const bool hasModifierTags = useTagFilters && HasMatchingTags(desiredTags, m_registeredModifierTags);

        AZStd::shared_lock<decltype(m_registrationMutex)> registrationLock(m_registrationMutex);

        surfacePointList.clear();

        //gather all intersecting points
        for (const auto& entryPair : m_registeredSurfaceDataProviders)
        {
            const AZ::u32 entryAddress = entryPair.first;
            const SurfaceDataRegistryEntry& entry = entryPair.second;
            if (!entry.m_bounds.IsValid() || AabbContains2D(entry.m_bounds, inPosition))
            {
                if (!useTagFilters || hasModifierTags || HasMatchingTags(desiredTags, entry.m_tags))
                {
                    SurfaceDataProviderRequestBus::Event(entryAddress, &SurfaceDataProviderRequestBus::Events::GetSurfacePoints, inPosition, surfacePointList);
                }
            }
        }

        if (!surfacePointList.empty())
        {
            //modify or annotate reported points
            for (const auto& entryPair : m_registeredSurfaceDataModifiers)
            {
                const AZ::u32 entryAddress = entryPair.first;
                const SurfaceDataRegistryEntry& entry = entryPair.second;
                if (!entry.m_bounds.IsValid() || AabbContains2D(entry.m_bounds, inPosition))
                {
                    SurfaceDataModifierRequestBus::Event(entryAddress, &SurfaceDataModifierRequestBus::Events::ModifySurfacePoints, surfacePointList);
                }
            }
            
            // After we've finished creating and annotating all the surface points, combine any points together that have effectively the
            // same XY coordinates and extremely similar Z values.  This produces results that are sorted in decreasing Z order.
            // Also, this filters out any remaining points that don't match the desired tag list.  This can happen when a surface provider
            // doesn't add a desired tag, and a surface modifier has the *potential* to add it, but then doesn't.
            if (useTagFilters)
            {
                FilterPoints(surfacePointList, desiredTags);
            }

            CombineAndSortNeighboringPoints(surfacePointList);
        }
    }

    void SurfaceDataSystemComponent::GetSurfacePointsFromRegion(const AZ::Aabb& inRegion, const AZ::Vector2 stepSize,
        const SurfaceTagVector& desiredTags, SurfacePointLists& surfacePointLists) const
    {
        const size_t totalQueryPositions = aznumeric_cast<size_t>(ceil(inRegion.GetXExtent() / stepSize.GetX())) *
            aznumeric_cast<size_t>(ceil(inRegion.GetYExtent() / stepSize.GetY()));

        AZStd::vector<AZ::Vector3> inPositions;
        inPositions.reserve(totalQueryPositions);

        // Initialize our list-per-position list with every input position to query from the region.
        // This is inclusive on the min sides of inRegion, and exclusive on the max sides.
        for (float y = inRegion.GetMin().GetY(); y < inRegion.GetMax().GetY(); y += stepSize.GetY())
        {
            for (float x = inRegion.GetMin().GetX(); x < inRegion.GetMax().GetX(); x += stepSize.GetX())
            {
                inPositions.emplace_back(x, y, AZ::Constants::FloatMax);
            }
        }

        GetSurfacePointsFromList(inPositions, desiredTags, surfacePointLists);
    }

    void SurfaceDataSystemComponent::GetSurfacePointsFromList(
        AZStd::span<const AZ::Vector3> inPositions, const SurfaceTagVector& desiredTags, SurfacePointLists& surfacePointLists) const
    {
        AZStd::shared_lock<decltype(m_registrationMutex)> registrationLock(m_registrationMutex);

        const size_t totalQueryPositions = inPositions.size();

        surfacePointLists.clear();
        surfacePointLists.resize(totalQueryPositions);

        const bool useTagFilters = HasValidTags(desiredTags);
        const bool hasModifierTags = useTagFilters && HasMatchingTags(desiredTags, m_registeredModifierTags);

        // Loop through each data provider, and query all the points for each one.  This allows us to check the tags and the overall
        // AABB bounds just once per provider, instead of once per point.  It also allows for an eventual optimization in which we could
        // send the list of points directly into each SurfaceDataProvider.
        for (const auto& [providerHandle, provider] : m_registeredSurfaceDataProviders)
        {
            bool hasInfiniteBounds = !provider.m_bounds.IsValid();

            if (!useTagFilters || hasModifierTags || HasMatchingTags(desiredTags, provider.m_tags))
            {
                for (size_t index = 0; index < totalQueryPositions; index++)
                {
                    bool inBounds = hasInfiniteBounds || AabbContains2D(provider.m_bounds, inPositions[index]);
                    if (inBounds)
                    {
                        SurfaceDataProviderRequestBus::Event(
                            providerHandle, &SurfaceDataProviderRequestBus::Events::GetSurfacePoints,
                            inPositions[index], surfacePointLists[index]);
                    }
                }
            }
        }

        // Once we have our list of surface points created, run through the list of surface data modifiers to potentially add
        // surface tags / values onto each point.  The difference between this and the above loop is that surface data *providers*
        // create new surface points, but surface data *modifiers* simply annotate points that have already been created.  The modifiers
        // are used to annotate points that occur within a volume.  A common example is marking points as "underwater" for points that occur
        // within a water volume.
        for (const auto& entryPair : m_registeredSurfaceDataModifiers)
        {
            const SurfaceDataRegistryEntry& entry = entryPair.second;
            bool hasInfiniteBounds = !entry.m_bounds.IsValid();

            for (size_t index = 0; index < totalQueryPositions; index++)
            {
                const auto& inPosition = inPositions[index];
                SurfacePointList& surfacePointList = surfacePointLists[index];
                if (!surfacePointList.empty())
                {
                    if (hasInfiniteBounds || AabbContains2D(entry.m_bounds, inPosition))
                    {
                        SurfaceDataModifierRequestBus::Event(
                            entryPair.first, &SurfaceDataModifierRequestBus::Events::ModifySurfacePoints,
                            surfacePointList);
                    }
                }
            }
        }

        // After we've finished creating and annotating all the surface points, combine any points together that have effectively the
        // same XY coordinates and extremely similar Z values.  This produces results that are sorted in decreasing Z order.
        // Also, this filters out any remaining points that don't match the desired tag list.  This can happen when a surface provider
        // doesn't add a desired tag, and a surface modifier has the *potential* to add it, but then doesn't.
        for (auto& surfacePointList : surfacePointLists)
        {
            if (useTagFilters)
            {
                FilterPoints(surfacePointList, desiredTags);
            }
            CombineAndSortNeighboringPoints(surfacePointList);
        }

    }

    void SurfaceDataSystemComponent::FilterPoints(SurfacePointList& sourcePointList, const SurfaceTagVector& desiredTags) const
    {
        // Before sorting and combining, filter out any points that don't match our search tags.
        sourcePointList.erase(
            AZStd::remove_if(
                sourcePointList.begin(), sourcePointList.end(),
                [desiredTags](SurfacePoint& point) -> bool
                {
                    return !HasMatchingTags(point.m_masks, desiredTags);
                }),
            sourcePointList.end());
    }

    void SurfaceDataSystemComponent::CombineAndSortNeighboringPoints(SurfacePointList& sourcePointList) const
    {
        // If there's only 0 or 1 point, there is no sorting or combining that needs to happen, so just return.
        if (sourcePointList.size() <= 1)
        {
            return;
        }

        // Efficient point consolidation requires the points to be pre-sorted so we are only comparing/combining neighbors.
        // Sort XY points together, with decreasing Z.
        AZStd::sort(sourcePointList.begin(), sourcePointList.end(), [](const SurfacePoint& a, const SurfacePoint& b)
        {
            // Our goal is to have identical XY values sorted adjacent to each other with decreasing Z.
            // We sort increasing Y, then increasing X, then decreasing Z, because we need to compare all 3 values for a
            // stable sort. The choice of increasing Y first is because we'll often generate the points as ranges of X values within
            // ranges of Y values, so this will produce the most usable and expected output sort.
            if (a.m_position.GetY() != b.m_position.GetY())
            {
                return a.m_position.GetY() < b.m_position.GetY();
            }
            if (a.m_position.GetX() != b.m_position.GetX())
            {
                return a.m_position.GetX() < b.m_position.GetX();
            }
            if (a.m_position.GetZ() != b.m_position.GetZ())
            {
                return a.m_position.GetZ() > b.m_position.GetZ();
            }

            // If we somehow ended up with two points with identical positions getting generated, use the entity ID as the tiebreaker
            // to guarantee a stable sort. We should never have two identical positions generated from the same entity.
            return a.m_entityId < b.m_entityId;
        });

        // iterate over subsequent source points for comparison and consolidation with the last added target/unique point
        for (auto pointItr = sourcePointList.begin() + 1; pointItr < sourcePointList.end();)
        {
            auto prevPointItr = pointItr - 1;

            // (Someday we should add a configurable tolerance for comparison)
            if (pointItr->m_position.IsClose(prevPointItr->m_position) && pointItr->m_normal.IsClose(prevPointItr->m_normal))
            {
                // consolidate points with similar attributes by adding masks/weights to the previous point and deleting this point.
                AddMaxValueForMasks(prevPointItr->m_masks, pointItr->m_masks);

                pointItr = sourcePointList.erase(pointItr);
            }
            else
            {
                pointItr++;
            }
        }
    }

    SurfaceDataRegistryHandle SurfaceDataSystemComponent::RegisterSurfaceDataProviderInternal(const SurfaceDataRegistryEntry& entry)
    {
        AZStd::unique_lock<decltype(m_registrationMutex)> registrationLock(m_registrationMutex);
        SurfaceDataRegistryHandle handle = ++m_registeredSurfaceDataProviderHandleCounter;
        m_registeredSurfaceDataProviders[handle] = entry;
        return handle;
    }

    SurfaceDataRegistryEntry SurfaceDataSystemComponent::UnregisterSurfaceDataProviderInternal(const SurfaceDataRegistryHandle& handle)
    {
        AZStd::unique_lock<decltype(m_registrationMutex)> registrationLock(m_registrationMutex);
        SurfaceDataRegistryEntry entry;
        auto entryItr = m_registeredSurfaceDataProviders.find(handle);
        if (entryItr != m_registeredSurfaceDataProviders.end())
        {
            entry = entryItr->second;
            m_registeredSurfaceDataProviders.erase(entryItr);
        }
        return entry;
    }

    bool SurfaceDataSystemComponent::UpdateSurfaceDataProviderInternal(const SurfaceDataRegistryHandle& handle, const SurfaceDataRegistryEntry& entry, AZ::Aabb& oldBounds)
    {
        AZStd::unique_lock<decltype(m_registrationMutex)> registrationLock(m_registrationMutex);
        auto entryItr = m_registeredSurfaceDataProviders.find(handle);
        if (entryItr != m_registeredSurfaceDataProviders.end())
        {
            oldBounds = entryItr->second.m_bounds;
            entryItr->second = entry;
            return true;
        }
        return false;
    }

    SurfaceDataRegistryHandle SurfaceDataSystemComponent::RegisterSurfaceDataModifierInternal(const SurfaceDataRegistryEntry& entry)
    {
        AZStd::unique_lock<decltype(m_registrationMutex)> registrationLock(m_registrationMutex);
        SurfaceDataRegistryHandle handle = ++m_registeredSurfaceDataModifierHandleCounter;
        m_registeredSurfaceDataModifiers[handle] = entry;
        m_registeredModifierTags.insert(entry.m_tags.begin(), entry.m_tags.end());
        return handle;
    }

    SurfaceDataRegistryEntry SurfaceDataSystemComponent::UnregisterSurfaceDataModifierInternal(const SurfaceDataRegistryHandle& handle)
    {
        AZStd::unique_lock<decltype(m_registrationMutex)> registrationLock(m_registrationMutex);
        SurfaceDataRegistryEntry entry;
        auto entryItr = m_registeredSurfaceDataModifiers.find(handle);
        if (entryItr != m_registeredSurfaceDataModifiers.end())
        {
            entry = entryItr->second;
            m_registeredSurfaceDataModifiers.erase(entryItr);
        }
        return entry;
    }

    bool SurfaceDataSystemComponent::UpdateSurfaceDataModifierInternal(const SurfaceDataRegistryHandle& handle, const SurfaceDataRegistryEntry& entry, AZ::Aabb& oldBounds)
    {
        AZStd::unique_lock<decltype(m_registrationMutex)> registrationLock(m_registrationMutex);
        auto entryItr = m_registeredSurfaceDataModifiers.find(handle);
        if (entryItr != m_registeredSurfaceDataModifiers.end())
        {
            oldBounds = entryItr->second.m_bounds;
            entryItr->second = entry;
            m_registeredModifierTags.insert(entry.m_tags.begin(), entry.m_tags.end());
            return true;
        }
        return false;
    }

}
