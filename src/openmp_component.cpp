/*
 * Copyright (C) 2017 Incognito
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Stage 1 open.mp component entry point for the streamer.
// Replaces the old SA-MP Load/Unload/AmxLoad/AmxUnload/ProcessTick exports and the earlier
// fake "StreamerComponentBase" bridge. Goal of Stage 1: component loads cleanly, natives
// register with every loaded script, tick runs, and a minimal set of player events reach the
// streamer's internal callbacks. Subsystem work (objects/pickups/actors/...) lives in
// streamer_api.cpp routing to the corresponding open.mp components.

// Pull in open.mp SDK before our own headers so values.hpp's constexpr constants
// (INVALID_PLAYER_ID, INVALID_VEHICLE_ID, ...) aren't clobbered by the SA-MP-style macros
// that streamer_api.h still exposes for the rest of the streamer sources.
#include <sdk.hpp>
#include <Server/Components/Actors/actors.hpp>
#include <Server/Components/Checkpoints/checkpoints.hpp>
#include <Server/Components/Classes/classes.hpp>
#include <Server/Components/Objects/objects.hpp>
#include <Server/Components/Pawn/pawn.hpp>
#include <Server/Components/Pickups/pickups.hpp>

#include "main.h"

#include "core.h"
#include "natives.h"
#include "openmp_component.h"
#include "streamer_callbacks.h"

extern void *pAMXFunctions;

namespace
{
	// UID picked fresh (different from the unrelated 0x73747265616d6572 placeholder used in the
	// earlier non-functional bridge).
	constexpr UID kStreamerComponentUID = UID(0x53744d72506c674eULL); // "StMrPlgN"

	AMX_NATIVE_INFO kNatives[] =
	{
		// Settings
		{ "Streamer_GetTickRate", Natives::Streamer_GetTickRate },
		{ "Streamer_SetTickRate", Natives::Streamer_SetTickRate },
		{ "Streamer_GetPlayerTickRate", Natives::Streamer_GetPlayerTickRate },
		{ "Streamer_SetPlayerTickRate", Natives::Streamer_SetPlayerTickRate },
		{ "Streamer_ToggleChunkStream", Natives::Streamer_ToggleChunkStream },
		{ "Streamer_IsToggleChunkStream", Natives::Streamer_IsToggleChunkStream },
		{ "Streamer_GetChunkTickRate", Natives::Streamer_GetChunkTickRate },
		{ "Streamer_SetChunkTickRate", Natives::Streamer_SetChunkTickRate },
		{ "Streamer_GetChunkSize", Natives::Streamer_GetChunkSize },
		{ "Streamer_SetChunkSize", Natives::Streamer_SetChunkSize },
		{ "Streamer_GetMaxItems", Natives::Streamer_GetMaxItems },
		{ "Streamer_SetMaxItems", Natives::Streamer_SetMaxItems },
		{ "Streamer_GetVisibleItems", Natives::Streamer_GetVisibleItems },
		{ "Streamer_SetVisibleItems", Natives::Streamer_SetVisibleItems },
		{ "Streamer_GetRadiusMultiplier", Natives::Streamer_GetRadiusMultiplier },
		{ "Streamer_SetRadiusMultiplier", Natives::Streamer_SetRadiusMultiplier },
		{ "Streamer_GetTypePriority", Natives::Streamer_GetTypePriority },
		{ "Streamer_SetTypePriority", Natives::Streamer_SetTypePriority },
		{ "Streamer_GetCellDistance", Natives::Streamer_GetCellDistance },
		{ "Streamer_SetCellDistance", Natives::Streamer_SetCellDistance },
		{ "Streamer_GetCellSize", Natives::Streamer_GetCellSize },
		{ "Streamer_SetCellSize", Natives::Streamer_SetCellSize },
		{ "Streamer_ToggleItemStatic", Natives::Streamer_ToggleItemStatic },
		{ "Streamer_IsToggleItemStatic", Natives::Streamer_IsToggleItemStatic },
		{ "Streamer_ToggleItemInvAreas", Natives::Streamer_ToggleItemInvAreas },
		{ "Streamer_IsToggleItemInvAreas", Natives::Streamer_IsToggleItemInvAreas },
		{ "Streamer_ToggleItemCallbacks", Natives::Streamer_ToggleItemCallbacks },
		{ "Streamer_IsToggleItemCallbacks", Natives::Streamer_IsToggleItemCallbacks },
		{ "Streamer_ToggleErrorCallback", Natives::Streamer_ToggleErrorCallback },
		{ "Streamer_IsToggleErrorCallback", Natives::Streamer_IsToggleErrorCallback },
		{ "Streamer_AmxUnloadDestroyItems", Natives::Streamer_AmxUnloadDestroyItems },
		// Updates
		{ "Streamer_ProcessActiveItems", Natives::Streamer_ProcessActiveItems },
		{ "Streamer_ToggleIdleUpdate", Natives::Streamer_ToggleIdleUpdate },
		{ "Streamer_IsToggleIdleUpdate", Natives::Streamer_IsToggleIdleUpdate },
		{ "Streamer_ToggleCameraUpdate", Natives::Streamer_ToggleCameraUpdate },
		{ "Streamer_IsToggleCameraUpdate", Natives::Streamer_IsToggleCameraUpdate },
		{ "Streamer_ToggleItemUpdate", Natives::Streamer_ToggleItemUpdate },
		{ "Streamer_IsToggleItemUpdate", Natives::Streamer_IsToggleItemUpdate },
		{ "Streamer_GetLastUpdateTime", Natives::Streamer_GetLastUpdateTime },
		{ "Streamer_Update", Natives::Streamer_Update },
		{ "Streamer_UpdateEx", Natives::Streamer_UpdateEx },
		// Data Manipulation
		{ "Streamer_GetFloatData", Natives::Streamer_GetFloatData },
		{ "Streamer_SetFloatData", Natives::Streamer_SetFloatData },
		{ "Streamer_GetIntData", Natives::Streamer_GetIntData },
		{ "Streamer_SetIntData", Natives::Streamer_SetIntData },
		{ "Streamer_RemoveIntData", Natives::Streamer_RemoveIntData },
		{ "Streamer_HasIntData", Natives::Streamer_HasIntData },
		{ "Streamer_GetArrayData", Natives::Streamer_GetArrayData },
		{ "Streamer_SetArrayData", Natives::Streamer_SetArrayData },
		{ "Streamer_IsInArrayData", Natives::Streamer_IsInArrayData },
		{ "Streamer_AppendArrayData", Natives::Streamer_AppendArrayData },
		{ "Streamer_RemoveArrayData", Natives::Streamer_RemoveArrayData },
		{ "Streamer_HasArrayData", Natives::Streamer_HasIntData }, // Alias.
		{ "Streamer_GetArrayDataLength", Natives::Streamer_GetArrayDataLength },
		{ "Streamer_GetUpperBound", Natives::Streamer_GetUpperBound },
		// Miscellaneous
		{ "Streamer_GetDistanceToItem", Natives::Streamer_GetDistanceToItem },
		{ "Streamer_ToggleItem", Natives::Streamer_ToggleItem },
		{ "Streamer_IsToggleItem", Natives::Streamer_IsToggleItem },
		{ "Streamer_ToggleAllItems", Natives::Streamer_ToggleAllItems },
		{ "Streamer_GetItemInternalID", Natives::Streamer_GetItemInternalID },
		{ "Streamer_GetItemStreamerID", Natives::Streamer_GetItemStreamerID },
		{ "Streamer_IsItemVisible", Natives::Streamer_IsItemVisible },
		{ "Streamer_DestroyAllVisibleItems", Natives::Streamer_DestroyAllVisibleItems },
		{ "Streamer_CountVisibleItems", Natives::Streamer_CountVisibleItems },
		{ "Streamer_DestroyAllItems", Natives::Streamer_DestroyAllItems },
		{ "Streamer_CountItems", Natives::Streamer_CountItems },
		{ "Streamer_GetNearbyItems", Natives::Streamer_GetNearbyItems },
		{ "Streamer_GetAllVisibleItems", Natives::Streamer_GetAllVisibleItems },
		{ "Streamer_GetItemPos", Natives::Streamer_GetItemPos },
		{ "Streamer_SetItemPos", Natives::Streamer_SetItemPos },
		{ "Streamer_GetItemOffset", Natives::Streamer_GetItemOffset },
		{ "Streamer_SetItemOffset", Natives::Streamer_SetItemOffset },
		// Objects
		{ "CreateDynamicObject", Natives::CreateDynamicObject },
		{ "DestroyDynamicObject", Natives::DestroyDynamicObject },
		{ "IsValidDynamicObject", Natives::IsValidDynamicObject },
		{ "GetDynamicObjectPos", Natives::GetDynamicObjectPos },
		{ "SetDynamicObjectPos", Natives::SetDynamicObjectPos },
		{ "GetDynamicObjectRot", Natives::GetDynamicObjectRot },
		{ "SetDynamicObjectRot", Natives::SetDynamicObjectRot },
		{ "GetDynamicObjectNoCameraCol", Natives::GetDynamicObjectNoCameraCol },
		{ "SetDynamicObjectNoCameraCol", Natives::SetDynamicObjectNoCameraCol },
		{ "MoveDynamicObject", Natives::MoveDynamicObject },
		{ "StopDynamicObject", Natives::StopDynamicObject },
		{ "IsDynamicObjectMoving", Natives::IsDynamicObjectMoving },
		{ "AttachCameraToDynamicObject", Natives::AttachCameraToDynamicObject },
		{ "AttachDynamicObjectToObject", Natives::AttachDynamicObjectToObject },
		{ "AttachDynamicObjectToPlayer", Natives::AttachDynamicObjectToPlayer },
		{ "AttachDynamicObjectToVehicle", Natives::AttachDynamicObjectToVehicle },
		{ "EditDynamicObject", Natives::EditDynamicObject },
		{ "IsDynamicObjectMaterialUsed", Natives::IsDynamicObjectMaterialUsed },
		{ "RemoveDynamicObjectMaterial", Natives::RemoveDynamicObjectMaterial },
		{ "GetDynamicObjectMaterial", Natives::GetDynamicObjectMaterial },
		{ "SetDynamicObjectMaterial", Natives::SetDynamicObjectMaterial },
		{ "IsDynamicObjectMaterialTextUsed", Natives::IsDynamicObjectMaterialTextUsed },
		{ "RemoveDynamicObjectMaterialText", Natives::RemoveDynamicObjectMaterialText, },
		{ "GetDynamicObjectMaterialText", Natives::GetDynamicObjectMaterialText },
		{ "SetDynamicObjectMaterialText", Natives::SetDynamicObjectMaterialText },
		{ "GetPlayerCameraTargetDynObject", Natives::GetPlayerCameraTargetDynObject },
		// Pickups
		{ "CreateDynamicPickup", Natives::CreateDynamicPickup },
		{ "DestroyDynamicPickup", Natives::DestroyDynamicPickup },
		{ "IsValidDynamicPickup", Natives::IsValidDynamicPickup },
		// Checkpoints
		{ "CreateDynamicCP", Natives::CreateDynamicCP },
		{ "DestroyDynamicCP", Natives::DestroyDynamicCP },
		{ "IsValidDynamicCP", Natives::IsValidDynamicCP },
		{ "IsPlayerInDynamicCP", Natives::IsPlayerInDynamicCP },
		{ "GetPlayerVisibleDynamicCP", Natives::GetPlayerVisibleDynamicCP },
		// Race Checkpoints
		{ "CreateDynamicRaceCP", Natives::CreateDynamicRaceCP },
		{ "DestroyDynamicRaceCP", Natives::DestroyDynamicRaceCP },
		{ "IsValidDynamicRaceCP", Natives::IsValidDynamicRaceCP },
		{ "IsPlayerInDynamicRaceCP", Natives::IsPlayerInDynamicRaceCP },
		{ "GetPlayerVisibleDynamicRaceCP", Natives::GetPlayerVisibleDynamicRaceCP },
		// Map Icons
		{ "CreateDynamicMapIcon", Natives::CreateDynamicMapIcon },
		{ "DestroyDynamicMapIcon", Natives::DestroyDynamicMapIcon },
		{ "IsValidDynamicMapIcon", Natives::IsValidDynamicMapIcon },
		// 3D Text Labels
		{ "CreateDynamic3DTextLabel", Natives::CreateDynamic3DTextLabel },
		{ "DestroyDynamic3DTextLabel", Natives::DestroyDynamic3DTextLabel },
		{ "IsValidDynamic3DTextLabel", Natives::IsValidDynamic3DTextLabel },
		{ "GetDynamic3DTextLabelText", Natives::GetDynamic3DTextLabelText },
		{ "UpdateDynamic3DTextLabelText", Natives::UpdateDynamic3DTextLabelText },
		// Areas
		{ "CreateDynamicCircle", Natives::CreateDynamicCircle },
		{ "CreateDynamicCylinder", Natives::CreateDynamicCylinder },
		{ "CreateDynamicSphere", Natives::CreateDynamicSphere },
		{ "CreateDynamicRectangle", Natives::CreateDynamicRectangle },
		{ "CreateDynamicCuboid", Natives::CreateDynamicCuboid },
		{ "CreateDynamicCube", Natives::CreateDynamicCuboid },
		{ "CreateDynamicPolygon", Natives::CreateDynamicPolygon },
		{ "DestroyDynamicArea", Natives::DestroyDynamicArea },
		{ "IsValidDynamicArea", Natives::IsValidDynamicArea },
		{ "GetDynamicAreaType", Natives::GetDynamicAreaType },
		{ "GetDynamicPolygonPoints", Natives::GetDynamicPolygonPoints },
		{ "GetDynamicPolygonNumberPoints", Natives::GetDynamicPolygonNumberPoints },
		{ "IsPlayerInDynamicArea", Natives::IsPlayerInDynamicArea },
		{ "IsPlayerInAnyDynamicArea", Natives::IsPlayerInAnyDynamicArea },
		{ "IsAnyPlayerInDynamicArea", Natives::IsAnyPlayerInDynamicArea },
		{ "IsAnyPlayerInAnyDynamicArea", Natives::IsAnyPlayerInAnyDynamicArea },
		{ "GetPlayerDynamicAreas", Natives::GetPlayerDynamicAreas },
		{ "GetPlayerNumberDynamicAreas", Natives::GetPlayerNumberDynamicAreas },
		{ "IsPointInDynamicArea", Natives::IsPointInDynamicArea },
		{ "IsPointInAnyDynamicArea", Natives::IsPointInAnyDynamicArea },
		{ "IsLineInDynamicArea", Natives::IsLineInDynamicArea },
		{ "IsLineInAnyDynamicArea", Natives::IsLineInAnyDynamicArea },
		{ "GetDynamicAreasForPoint", Natives::GetDynamicAreasForPoint },
		{ "GetNumberDynamicAreasForPoint", Natives::GetNumberDynamicAreasForPoint },
		{ "GetDynamicAreasForLine", Natives::GetDynamicAreasForLine },
		{ "GetNumberDynamicAreasForLine", Natives::GetNumberDynamicAreasForLine },
		{ "AttachDynamicAreaToObject", Natives::AttachDynamicAreaToObject },
		{ "AttachDynamicAreaToPlayer", Natives::AttachDynamicAreaToPlayer },
		{ "AttachDynamicAreaToVehicle", Natives::AttachDynamicAreaToVehicle },
		{ "ToggleDynAreaSpectateMode", Natives::ToggleDynAreaSpectateMode },
		{ "IsToggleDynAreaSpectateMode", Natives::IsToggleDynAreaSpectateMode },
		// Actors
		{ "CreateDynamicActor", Natives::CreateDynamicActor },
		{ "DestroyDynamicActor", Natives::DestroyDynamicActor },
		{ "IsValidDynamicActor", Natives::IsValidDynamicActor },
		{ "IsDynamicActorStreamedIn", Natives::IsDynamicActorStreamedIn },
		{ "GetDynamicActorVirtualWorld", Natives::GetDynamicActorVirtualWorld },
		{ "SetDynamicActorVirtualWorld", Natives::SetDynamicActorVirtualWorld },
		{ "GetDynamicActorAnimation", Natives::GetDynamicActorAnimation },
		{ "ApplyDynamicActorAnimation", Natives::ApplyDynamicActorAnimation },
		{ "ClearDynamicActorAnimations", Natives::ClearDynamicActorAnimations },
		{ "GetDynamicActorFacingAngle", Natives::GetDynamicActorFacingAngle },
		{ "SetDynamicActorFacingAngle", Natives::SetDynamicActorFacingAngle },
		{ "GetDynamicActorPos", Natives::GetDynamicActorPos },
		{ "SetDynamicActorPos", Natives::SetDynamicActorPos },
		{ "GetDynamicActorHealth", Natives::GetDynamicActorHealth },
		{ "SetDynamicActorHealth", Natives::SetDynamicActorHealth },
		{ "SetDynamicActorInvulnerable", Natives::SetDynamicActorInvulnerable },
		{ "IsDynamicActorInvulnerable", Natives::IsDynamicActorInvulnerable },
		{ "GetPlayerTargetDynamicActor", Natives::GetPlayerTargetDynamicActor },
		{ "GetPlayerCameraTargetDynActor", Natives::GetPlayerCameraTargetDynActor },
		// Extended
		{ "CreateDynamicObjectEx", Natives::CreateDynamicObjectEx },
		{ "CreateDynamicPickupEx", Natives::CreateDynamicPickupEx },
		{ "CreateDynamicCPEx", Natives::CreateDynamicCPEx },
		{ "CreateDynamicRaceCPEx", Natives::CreateDynamicRaceCPEx },
		{ "CreateDynamicMapIconEx", Natives::CreateDynamicMapIconEx },
		{ "CreateDynamic3DTextLabelEx", Natives::CreateDynamic3DTextLabelEx },
		{ "CreateDynamicCircleEx", Natives::CreateDynamicCircleEx },
		{ "CreateDynamicCylinderEx", Natives::CreateDynamicCylinderEx },
		{ "CreateDynamicSphereEx", Natives::CreateDynamicSphereEx },
		{ "CreateDynamicRectangleEx", Natives::CreateDynamicRectangleEx },
		{ "CreateDynamicCuboidEx", Natives::CreateDynamicCuboidEx },
		{ "CreateDynamicCubeEx", Natives::CreateDynamicCuboidEx },
		{ "CreateDynamicPolygonEx", Natives::CreateDynamicPolygonEx },
		{ "CreateDynamicActorEx", Natives::CreateDynamicActorEx },
		// Deprecated
		{ "Streamer_TickRate", Natives::Streamer_SetTickRate },
		{ "Streamer_MaxItems", Natives::Streamer_SetMaxItems },
		{ "Streamer_VisibleItems", Natives::Streamer_SetVisibleItems },
		{ "Streamer_CellDistance", Natives::Streamer_SetCellDistance },
		{ "Streamer_CellSize", Natives::Streamer_SetCellSize },
		{ "Streamer_CallbackHook", Natives::Streamer_CallbackHook },
		{ "DestroyAllDynamicObjects", Natives::DestroyAllDynamicObjects },
		{ "CountDynamicObjects", Natives::CountDynamicObjects },
		{ "DestroyAllDynamicPickups", Natives::DestroyAllDynamicPickups },
		{ "CountDynamicPickups", Natives::CountDynamicPickups },
		{ "DestroyAllDynamicCPs", Natives::DestroyAllDynamicCPs },
		{ "CountDynamicCPs", Natives::CountDynamicCPs },
		{ "DestroyAllDynamicRaceCPs", Natives::DestroyAllDynamicRaceCPs },
		{ "CountDynamicRaceCPs", Natives::CountDynamicRaceCPs },
		{ "DestroyAllDynamicMapIcons", Natives::DestroyAllDynamicMapIcons },
		{ "CountDynamicMapIcons", Natives::CountDynamicMapIcons },
		{ "DestroyAllDynamic3DTextLabels", Natives::DestroyAllDynamic3DTextLabels },
		{ "CountDynamic3DTextLabels", Natives::CountDynamic3DTextLabels },
		{ "DestroyAllDynamicAreas", Natives::DestroyAllDynamicAreas },
		{ "CountDynamicAreas", Natives::CountDynamicAreas },
		{ "TogglePlayerDynamicCP", Natives::TogglePlayerDynamicCP },
		{ "TogglePlayerAllDynamicCPs", Natives::TogglePlayerAllDynamicCPs },
		{ "TogglePlayerDynamicRaceCP", Natives::TogglePlayerDynamicRaceCP },
		{ "TogglePlayerAllDynamicRaceCPs", Natives::TogglePlayerAllDynamicRaceCPs },
		{ "TogglePlayerDynamicArea", Natives::TogglePlayerDynamicArea },
		{ "TogglePlayerAllDynamicAreas", Natives::TogglePlayerAllDynamicAreas },
		{ nullptr, nullptr }
	};

	class StreamerComponent final
		: public IComponent
		, public CoreEventHandler
		, public PawnEventHandler
		, public PlayerConnectEventHandler
		, public PlayerSpawnEventHandler
		, public PickupEventHandler
		, public ActorEventHandler
		, public PlayerCheckpointEventHandler
		, public ClassEventHandler
		, public ObjectEventHandler
		, public PlayerShotEventHandler
	{
	public:
		PROVIDE_UID(kStreamerComponentUID)

		StringView componentName() const override
		{
			return "Streamer";
		}

		SemanticVersion componentVersion() const override
		{
			return SemanticVersion(2, 9, 6, 0);
		}

		void onLoad(ICore *c) override
		{
			core_ = c;
			core.reset(new Core);
			c->getEventDispatcher().addEventHandler(this);
			c->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
			c->getPlayers().getPlayerSpawnDispatcher().addEventHandler(this);
			c->printLn("*** Streamer Plugin v%s by Incognito loaded ***", PLUGIN_VERSION);
		}

		void onInit(IComponentList *list) override
		{
			pawn_ = list->queryComponent<IPawnComponent>();
			vehicles_ = list->queryComponent<IVehiclesComponent>();
			objects_ = list->queryComponent<IObjectsComponent>();
			pickups_ = list->queryComponent<IPickupsComponent>();
			actors_ = list->queryComponent<IActorsComponent>();
			checkpoints_ = list->queryComponent<ICheckpointsComponent>();
			classes_ = list->queryComponent<IClassesComponent>();
			if (pawn_)
			{
				pAMXFunctions = const_cast<void *>(static_cast<const void *>(pawn_->getAmxFunctions().data()));
				pawn_->getEventDispatcher().addEventHandler(this);
			}
			else if (core_)
			{
				core_->logLn(LogLevel::Error, "streamer: Pawn component not available; Streamer_* natives will not be registered");
			}
			if (pickups_)
			{
				pickups_->getEventDispatcher().addEventHandler(this);
			}
			if (actors_)
			{
				actors_->getEventDispatcher().addEventHandler(this);
			}
			if (checkpoints_)
			{
				checkpoints_->getEventDispatcher().addEventHandler(this);
			}
			if (classes_)
			{
				classes_->getEventDispatcher().addEventHandler(this);
			}
			if (objects_)
			{
				objects_->getEventDispatcher().addEventHandler(this);
			}
			if (core_)
			{
				core_->getPlayers().getPlayerShotDispatcher().addEventHandler(this);
			}
		}

		void onReady() override { }

		void onFree(IComponent * /*component*/) override { }

		void free() override
		{
			if (core_)
			{
				core_->getPlayers().getPlayerShotDispatcher().removeEventHandler(this);
			}
			if (objects_)
			{
				objects_->getEventDispatcher().removeEventHandler(this);
			}
			if (classes_)
			{
				classes_->getEventDispatcher().removeEventHandler(this);
			}
			if (checkpoints_)
			{
				checkpoints_->getEventDispatcher().removeEventHandler(this);
			}
			if (actors_)
			{
				actors_->getEventDispatcher().removeEventHandler(this);
			}
			if (pickups_)
			{
				pickups_->getEventDispatcher().removeEventHandler(this);
			}
			if (pawn_)
			{
				pawn_->getEventDispatcher().removeEventHandler(this);
				pawn_ = nullptr;
			}
			if (core_)
			{
				core_->getPlayers().getPlayerSpawnDispatcher().removeEventHandler(this);
				core_->getPlayers().getPlayerConnectDispatcher().removeEventHandler(this);
				core_->getEventDispatcher().removeEventHandler(this);
				core_ = nullptr;
			}
			vehicles_ = nullptr;
			objects_ = nullptr;
			pickups_ = nullptr;
			actors_ = nullptr;
			checkpoints_ = nullptr;
			classes_ = nullptr;
			core.reset();
			pAMXFunctions = nullptr;
		}

		void reset() override { }

		// --- CoreEventHandler ---------------------------------------------------------------
		void onTick(Microseconds /*elapsed*/, TimePoint /*now*/) override
		{
			if (core && core->getStreamer())
			{
				core->getStreamer()->startAutomaticUpdate();
			}
		}

		// --- PawnEventHandler ---------------------------------------------------------------
		void onAmxLoad(IPawnScript &script) override
		{
			AMX *amx = script.GetAMX();
			if (!amx)
			{
				return;
			}
			core->getData()->interfaces.insert(amx);
			core->getData()->amxUnloadDestroyItems.insert(amx);
			Utility::checkInterfaceAndRegisterNatives(amx, kNatives);
		}

		void onAmxUnload(IPawnScript &script) override
		{
			AMX *amx = script.GetAMX();
			if (!amx || !core)
			{
				return;
			}
			core->getData()->interfaces.erase(amx);
			if (core->getData()->amxUnloadDestroyItems.find(amx) != core->getData()->amxUnloadDestroyItems.end())
			{
				Utility::destroyAllItemsInInterface(amx);
				core->getData()->amxUnloadDestroyItems.erase(amx);
			}
		}

		// --- PlayerConnectEventHandler ------------------------------------------------------
		void onPlayerConnect(IPlayer &player) override
		{
			Streamer_OnPlayerConnect(player.getID());
		}

		void onPlayerDisconnect(IPlayer &player, PeerDisconnectReason reason) override
		{
			Streamer_OnPlayerDisconnect(player.getID(), static_cast<int>(reason));
		}

		// --- PlayerSpawnEventHandler --------------------------------------------------------
		bool onPlayerRequestSpawn(IPlayer & /*player*/) override
		{
			return true;
		}

		void onPlayerSpawn(IPlayer &player) override
		{
			Streamer_OnPlayerSpawn(player.getID());
		}

		// --- PickupEventHandler -------------------------------------------------------------
		void onPlayerPickUpPickup(IPlayer &player, IPickup &pickup) override
		{
			Streamer_OnPlayerPickUpPickup(player.getID(), pickup.getID());
		}

		// --- ActorEventHandler --------------------------------------------------------------
		void onActorStreamIn(IActor &actor, IPlayer &forPlayer) override
		{
			Streamer_OnActorStreamIn(actor.getID(), forPlayer.getID());
		}

		void onActorStreamOut(IActor &actor, IPlayer &forPlayer) override
		{
			Streamer_OnActorStreamOut(actor.getID(), forPlayer.getID());
		}

		void onPlayerGiveDamageActor(IPlayer &player, IActor &actor, float amount, unsigned weapon, BodyPart part) override
		{
			Streamer_OnPlayerGiveDamageActor(player.getID(), actor.getID(), amount, static_cast<int>(weapon), static_cast<int>(part));
		}

		// --- ClassEventHandler --------------------------------------------------------------
		bool onPlayerRequestClass(IPlayer &player, unsigned int classId) override
		{
			Streamer_OnPlayerRequestClass(player.getID(), static_cast<int>(classId));
			return true;
		}

		// --- ObjectEventHandler -------------------------------------------------------------
		void onPlayerObjectEdited(IPlayer &player, IPlayerObject &object, ObjectEditResponse response, Vector3 offset, Vector3 rotation) override
		{
			Streamer_OnPlayerEditObject(player.getID(), true, object.getID(), static_cast<int>(response),
				offset.x, offset.y, offset.z, rotation.x, rotation.y, rotation.z);
		}

		void onPlayerObjectSelected(IPlayer &player, IPlayerObject &object, int model, Vector3 position) override
		{
			Streamer_OnPlayerSelectObject(player.getID(), SELECT_OBJECT_PLAYER_OBJECT, object.getID(), model, position.x, position.y, position.z);
		}

		// --- PlayerShotEventHandler ---------------------------------------------------------
		bool onPlayerShotPlayerObject(IPlayer &player, IPlayerObject &target, const PlayerBulletData &bulletData) override
		{
			return Streamer_OnPlayerWeaponShot(player.getID(), static_cast<int>(bulletData.weapon), BULLET_HIT_TYPE_PLAYER_OBJECT, target.getID(), bulletData.hitPos.x, bulletData.hitPos.y, bulletData.hitPos.z);
		}

		// --- PlayerCheckpointEventHandler ---------------------------------------------------
		void onPlayerEnterCheckpoint(IPlayer &player) override
		{
			Streamer_OnPlayerEnterCheckpoint(player.getID());
		}

		void onPlayerLeaveCheckpoint(IPlayer &player) override
		{
			Streamer_OnPlayerLeaveCheckpoint(player.getID());
		}

		void onPlayerEnterRaceCheckpoint(IPlayer &player) override
		{
			Streamer_OnPlayerEnterRaceCheckpoint(player.getID());
		}

		void onPlayerLeaveRaceCheckpoint(IPlayer &player) override
		{
			Streamer_OnPlayerLeaveRaceCheckpoint(player.getID());
		}

	public:
		ICore *core_ = nullptr;
		IPawnComponent *pawn_ = nullptr;
		IVehiclesComponent *vehicles_ = nullptr;
		IObjectsComponent *objects_ = nullptr;
		IPickupsComponent *pickups_ = nullptr;
		IActorsComponent *actors_ = nullptr;
		ICheckpointsComponent *checkpoints_ = nullptr;
		IClassesComponent *classes_ = nullptr;
	};

	StreamerComponent g_streamerComponent;
}

namespace StreamerRuntime
{
	ICore *core() { return g_streamerComponent.core_; }
	IPawnComponent *pawn() { return g_streamerComponent.pawn_; }
	IVehiclesComponent *vehicles() { return g_streamerComponent.vehicles_; }
	IObjectsComponent *objects() { return g_streamerComponent.objects_; }
	IPickupsComponent *pickups() { return g_streamerComponent.pickups_; }
	IActorsComponent *actors() { return g_streamerComponent.actors_; }
	ICheckpointsComponent *checkpoints() { return g_streamerComponent.checkpoints_; }
	IClassesComponent *classes() { return g_streamerComponent.classes_; }
}

COMPONENT_ENTRY_POINT()
{
	return &g_streamerComponent;
}
