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
#include <array>
#include <cstdint>
#include <vector>

#include <sdk.hpp>
#include <Server/Components/Actors/actors.hpp>
#include <Server/Components/Checkpoints/checkpoints.hpp>
#include <Server/Components/Classes/classes.hpp>
#include <Server/Components/Objects/objects.hpp>
#include <Server/Components/Pawn/pawn.hpp>
#include <Server/Components/Pickups/pickups.hpp>

#include "main.h"

#include "core.h"
#include "natives_table.h"
#include "openmp_component.h"
#include "streamer_callbacks.h"
#include "streamer_component_api.h"

extern void *pAMXFunctions;

namespace
{
	// UID picked fresh (different from the unrelated 0x73747265616d6572 placeholder used in the
	// earlier non-functional bridge).
	constexpr UID kStreamerComponentUID = UID(0x53744d72506c674eULL); // "StMrPlgN"

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
		, public PlayerChangeEventHandler
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
				core_->getPlayers().getPlayerChangeDispatcher().addEventHandler(this);
			}
		}

		void onReady() override { }

		void onFree(IComponent * /*component*/) override { }

		void free() override
		{
			if (core_)
			{
				core_->getPlayers().getPlayerChangeDispatcher().removeEventHandler(this);
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

		IExtension *getExtension(UID id) override
		{
			if (id == IStreamerComponent::ExtensionIID)
			{
				return static_cast<IExtension *>(GetStreamerExtension());
			}
			return IComponent::getExtension(id);
		}

		// --- CoreEventHandler ---------------------------------------------------------------
		void onTick(Microseconds /*elapsed*/, TimePoint now) override
		{
			if (core && core->getStreamer())
			{
				core->getStreamer()->startAutomaticUpdate();
			}
			processActorResyncs(now);
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
			Utility::checkInterfaceAndRegisterNatives(amx, gStreamerNatives);
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
			// Do NOT call setPosition synchronously here — doing so inside the event handler
			// appears to confuse open.mp's streaming state and the actor never materialises for
			// the client. The periodic tick sweep below handles the fall-through correction
			// instead. We just track the pair so the sweep only touches actors actually visible.
			actorsStreamedIn_.insert(actor.getID());
		}

		void onActorStreamOut(IActor &actor, IPlayer &forPlayer) override
		{
			Streamer_OnActorStreamOut(actor.getID(), forPlayer.getID());
			// If no player still has this actor streamed in, drop it from the resync set.
			if (actors_)
			{
				if (IActor *a = actors_->get(actor.getID()))
				{
					bool stillVisible = false;
					auto &playerPool = core_->getPlayers();
					for (IPlayer *p : playerPool.players())
					{
						if (p && p != &forPlayer && a->isStreamedInForPlayer(*p))
						{
							stillVisible = true;
							break;
						}
					}
					if (!stillVisible)
					{
						actorsStreamedIn_.erase(actor.getID());
					}
				}
				else
				{
					actorsStreamedIn_.erase(actor.getID());
				}
			}
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

		// --- PlayerChangeEventHandler -------------------------------------------------------
		void onPlayerInteriorChange(IPlayer & /*player*/, unsigned /*newInterior*/, unsigned /*oldInterior*/) override
		{
			// Left empty on purpose — forcing a nudge here while the client is mid-transition
			// also seems to lose the actor on the client side. The periodic sweep (runs every
			// few seconds) picks up the new interior state safely once things settle.
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

		// Actor position re-sync. open.mp's stream-in fires before the client has map collision
		// around, so a ped placed above terrain falls through. We can't position an actor for a
		// specific player (setPosition broadcasts to everyone who has it streamed), so we force
		// a position sync packet by nudging the Z coordinate by 1mm up and putting it back in
		// the same tick. The nudge guarantees a packet even if the pool skips setPosition when
		// the coordinates don't change.
		//
		// Three trigger paths, all feeding the same nudge:
		//   1. onActorStreamIn — fast-path for the just-streamed player
		//   2. onPlayerInteriorChange — same treatment when the map swaps around the player
		//   3. periodic tick sweep — catches anything the first two missed

		static constexpr int kActorPeriodicResyncMs = 3000;
		static constexpr float kActorResyncNudgeZ = 0.001f;

		void nudgeActor(IActor &a)
		{
			Vector3 p = a.getPosition();
			a.setPosition(Vector3(p.x, p.y, p.z + kActorResyncNudgeZ));
			a.setPosition(p);
		}

		void processActorResyncs(TimePoint now)
		{
			if (!actors_ || !core_) return;
			if (lastActorPeriodicResync_ == TimePoint())
			{
				lastActorPeriodicResync_ = now;
				return;
			}
			if (now - lastActorPeriodicResync_ < Milliseconds(kActorPeriodicResyncMs))
			{
				return;
			}
			lastActorPeriodicResync_ = now;

			// Iterate only actors we know are streamed-in for at least one player, sourced from
			// the stream-in/out bookkeeping above. Avoids the old O(actors * players) sweep.
			for (auto it = actorsStreamedIn_.begin(); it != actorsStreamedIn_.end();)
			{
				IActor *a = actors_->get(*it);
				if (!a)
				{
					it = actorsStreamedIn_.erase(it);
					continue;
				}
				nudgeActor(*a);
				++it;
			}
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

	private:
		TimePoint lastActorPeriodicResync_ {};
		std::unordered_set<int> actorsStreamedIn_;
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
