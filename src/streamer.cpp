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

#include "main.h"

#include "streamer.h"
#include "core.h"
#include "streamer_component_api.h"
#include "openmp_component.h" // FLOOD DIAGNOSTIC: StreamerRuntime::diagLog

namespace
{
	using Utility::squaredDistance3;

	// RAII scope timer that accumulates elapsed ns into Streamer::phaseTimeNs[type].
	// Designed for the process*/discover*/stream* phase calls — overhead is a single
	// steady_clock read on entry and exit (~100 ns on Windows), negligible compared
	// to the work inside each phase.
	struct PhaseTimer
	{
		std::array<uint64_t, STREAMER_MAX_TYPES> &store;
		int type;
		std::chrono::steady_clock::time_point start;
		PhaseTimer(std::array<uint64_t, STREAMER_MAX_TYPES> &s, int t) noexcept
			: store(s), type(t), start(std::chrono::steady_clock::now()) {}
		~PhaseTimer()
		{
			const auto delta = std::chrono::steady_clock::now() - start;
			if (type >= 0 && type < STREAMER_MAX_TYPES)
			{
				store[type] += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count());
			}
		}
	};

	// Comparator for candidate entries: pair<pair<priority, distance>, shared_item>.
	// Produces the same order as Item::PairCompare with std::multimap: higher priority
	// first, then closer first. After std::sort: front = best, back = worst.
	struct CandidateCmp
	{
		template<typename T>
		bool operator()(const std::pair<std::pair<int, float>, T> &a,
		                const std::pair<std::pair<int, float>, T> &b) const noexcept
		{
			if (a.first.first != b.first.first)
			{
				return a.first.first > b.first.first;
			}
			return a.first.second < b.first.second;
		}
	};
}

Streamer::Streamer()
{
	averageElapsedTime = 0.0f;
	lastUpdateTime = 0.0f;
	hysteresisFactor.fill(1.0f);
	tickCount = 0;
	tickRate = 50;
	velocityBoundaries = std::make_tuple(0.25f, 7.5f);
}

void Streamer::calculateAverageElapsedTime()
{
	std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
	static std::chrono::steady_clock::time_point lastRecordedTime;
	static Eigen::Array<float, 5, 1> recordedTimes = Eigen::Array<float, 5, 1>::Zero();
	if (lastRecordedTime.time_since_epoch().count())
	{
		if (!(recordedTimes > 0).all())
		{
			std::chrono::duration<float> elapsedTime = currentTime - lastRecordedTime;
			recordedTimes[(recordedTimes > 0).count()] = elapsedTime.count();
		}
		else
		{
			averageElapsedTime = recordedTimes.mean() * 50.0f;
			recordedTimes.setZero();
		}
	}
	lastRecordedTime = currentTime;
}

void Streamer::startAutomaticUpdate()
{
	// Run whenever there is streaming to do: any player online OR any Pawn script loaded.
	// The old gate keyed solely on a loaded AMX (interfaces), so a pure-C# server (SampSharp
	// drives this component through the C-exports, with no gameplay Pawn) only streamed by
	// accident — because open.mp still loads a stub gamemode AMX. Drop that stub and streaming
	// silently dies. This only broadens when the loop runs; the inner branches are unchanged.
	if (!core->getData()->players.empty() || !core->getData()->interfaces.empty())
	{
		std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
		if (!core->getData()->players.empty())
		{
			bool updatedActiveItems = false;
			for (std::unordered_map<int, Player>::iterator p = core->getData()->players.begin(); p != core->getData()->players.end(); ++p)
			{
				if (core->getChunkStreamer()->getChunkStreamingEnabled() && p->second.processingChunks.any())
				{
					core->getChunkStreamer()->performPlayerChunkUpdate(p->second, true);
				}
				else
				{
					if (++p->second.tickCount >= p->second.tickRate)
					{
						if (!updatedActiveItems)
						{
							processActiveItems();
							updatedActiveItems = true;
						}
						if (!p->second.delayedUpdate)
						{
							performPlayerUpdate(p->second, true);
						}
						else
						{
							startManualUpdate(p->second, p->second.delayedUpdateType);
						}
						p->second.tickCount = 0;
					}
				}
			}
		}
		else
		{
			processActiveItems();
		}
		if (++tickCount >= tickRate)
		{
			for (std::unordered_map<int, Player>::iterator p = core->getData()->players.begin(); p != core->getData()->players.end(); ++p)
			{
				std::vector<SharedCell> cells;
				core->getGrid()->findMinimalCellsForPlayer(p->second, cells);

				for (std::vector<int>::const_iterator t = core->getData()->typePriority.begin(); t != core->getData()->typePriority.end(); ++t)
				{
					switch (*t)
					{
						case STREAMER_TYPE_PICKUP:
						{
							if (!core->getData()->pickups.empty() && p->second.enabledItems[STREAMER_TYPE_PICKUP])
							{
								discoverPickups(p->second, cells);
							}
							break;
						}
						case STREAMER_TYPE_ACTOR:
						{
							if (!core->getData()->actors.empty() && p->second.enabledItems[STREAMER_TYPE_ACTOR])
							{
								discoverActors(p->second, cells);
							}
							break;
						}
					}
				}
			}

			for (std::vector<int>::const_iterator t = core->getData()->typePriority.begin(); t != core->getData()->typePriority.end(); ++t)
			{
				switch (*t)
				{
					case STREAMER_TYPE_PICKUP:
					{
						streamPickups();
						break;
					}
					case STREAMER_TYPE_ACTOR:
					{
						Utility::processPendingDestroyedActors();
						streamActors();
						break;
					}
				}
			}
			executeCallbacks();
			tickCount = 0;
		}
		// --- FLOOD DIAGNOSTIC (temporary; remove after root-cause) ---
		// Once per second, log any player whose per-second object stream-in rate is
		// abnormally high, together with the state that distinguishes the cause:
		//   creates>>destroys & distinctIds<<creates  -> same objects re-created (tracking/thrash)
		//   distinctIds ~= creates                    -> genuine churn (position/world instability or leak)
		//   internal==curVis<maxVis                   -> currentVisibleObjects ratcheted down (cap thrash)
		{
			static std::chrono::steady_clock::time_point diagLast = currentTime;
			static const std::size_t diagThreshold = 150; // object creates/sec above which we log
			if (std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - diagLast).count() >= 1000)
			{
				for (std::unordered_map<int, Player>::iterator dp = core->getData()->players.begin(); dp != core->getData()->players.end(); ++dp)
				{
					Player &pl = dp->second;
					if (pl.diagObjCreates >= diagThreshold)
					{
						int topId = -1, topCount = 0;
						for (std::unordered_map<int, int>::iterator it = pl.diagObjCreateIds.begin(); it != pl.diagObjCreateIds.end(); ++it)
						{
							if (it->second > topCount)
							{
								topCount = it->second;
								topId = it->first;
							}
						}
						char diagBuf[320];
						std::snprintf(diagBuf, sizeof(diagBuf),
							"[StreamerDiag] player %d creates=%d destroys=%d distinctIds=%d topObjId=%d x%d | internal=%d curVis=%d maxVis=%d pos=(%.1f,%.1f,%.1f) world=%d int=%d",
							pl.playerId, (int)pl.diagObjCreates, (int)pl.diagObjDestroys, (int)pl.diagObjCreateIds.size(),
							topId, topCount, (int)pl.internalObjects.size(), (int)pl.currentVisibleObjects, (int)pl.maxVisibleObjects,
							(double)pl.position[0], (double)pl.position[1], (double)pl.position[2], pl.worldId, pl.interiorId);
						StreamerRuntime::diagLog(diagBuf);
					}
					pl.diagObjCreates = 0;
					pl.diagObjDestroys = 0;
					pl.diagObjCreateIds.clear();
				}
				diagLast = currentTime;
			}
		}
		calculateAverageElapsedTime();
		lastUpdateTime = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - currentTime).count();
	}
}

void Streamer::startManualUpdate(Player &player, int type)
{
	std::bitset<STREAMER_MAX_TYPES> enabledItems = player.enabledItems;
	if (player.delayedUpdate)
	{
		if (player.delayedUpdateTime.time_since_epoch() <= std::chrono::steady_clock::now().time_since_epoch())
		{
			if (player.delayedUpdateFreeze)
			{
				StreamerApi::TogglePlayerControllable(player.playerId, true);
			}
			player.delayedUpdate = false;
		}
	}
	if (type >= 0 && type < STREAMER_MAX_TYPES)
	{
		if (core->getChunkStreamer()->getChunkStreamingEnabled())
		{
			switch (type)
			{
				case STREAMER_TYPE_OBJECT:
				{
					player.discoveredObjects.clear();
					player.existingObjects.clear();
					player.processingChunks.reset(STREAMER_TYPE_OBJECT);
					break;
				}
				case STREAMER_TYPE_MAP_ICON:
				{
					player.discoveredMapIcons.clear();
					player.existingMapIcons.clear();
					player.processingChunks.reset(STREAMER_TYPE_MAP_ICON);
					break;
				}
				case STREAMER_TYPE_3D_TEXT_LABEL:
				{
					player.discoveredTextLabels.clear();
					player.existingTextLabels.clear();
					player.processingChunks.reset(STREAMER_TYPE_3D_TEXT_LABEL);
					break;
				}
			}
		}
		player.enabledItems.reset();
		player.enabledItems.set(type);
	}
	else if (core->getChunkStreamer()->getChunkStreamingEnabled())
	{
		player.discoveredMapIcons.clear();
		player.discoveredObjects.clear();
		player.discoveredTextLabels.clear();
		player.existingMapIcons.clear();
		player.existingObjects.clear();
		player.existingTextLabels.clear();
		player.processingChunks.reset();
	}
	processActiveItems();
	performPlayerUpdate(player, false);
	if (core->getChunkStreamer()->getChunkStreamingEnabled())
	{
		core->getChunkStreamer()->performPlayerChunkUpdate(player, false);
	}
	player.enabledItems = enabledItems;
}

void Streamer::performPlayerUpdate(Player &player, bool automatic)
{
	Eigen::Vector3f delta = Eigen::Vector3f::Zero(), position = player.position;
	bool update = true;
	if (automatic)
	{
		player.interiorId = StreamerApi::GetPlayerInterior(player.playerId);
		player.worldId = StreamerApi::GetPlayerVirtualWorld(player.playerId);
		if (!player.updateUsingCameraPosition)
		{
			int state = StreamerApi::GetPlayerState(player.playerId);
			if ((state != PLAYER_STATE_NONE && state != PLAYER_STATE_WASTED) || (state == PLAYER_STATE_SPECTATING && !player.requestingClass))
			{
				if (!StreamerApi::IsPlayerInAnyVehicle(player.playerId))
				{
					StreamerApi::GetPlayerPos(player.playerId, &player.position[0], &player.position[1], &player.position[2]);
				}
				else
				{
					StreamerApi::GetVehiclePos(StreamerApi::GetPlayerVehicleID(player.playerId), &player.position[0], &player.position[1], &player.position[2]);
				}
				if (player.position != position)
				{
					position = player.position;
					Eigen::Vector3f velocity = Eigen::Vector3f::Zero();
					if (state == PLAYER_STATE_ONFOOT)
					{
						StreamerApi::GetPlayerVelocity(player.playerId, &velocity[0], &velocity[1], &velocity[2]);
					}
					else if (state == PLAYER_STATE_DRIVER || state == PLAYER_STATE_PASSENGER)
					{
						StreamerApi::GetVehicleVelocity(StreamerApi::GetPlayerVehicleID(player.playerId), &velocity[0], &velocity[1], &velocity[2]);
					}
					float velocityNorm = velocity.squaredNorm();
					if (velocityNorm > std::get<0>(velocityBoundaries) && velocityNorm < std::get<1>(velocityBoundaries))
					{
						delta = velocity * averageElapsedTime;
					}
				}
				else
				{
					update = player.updateWhenIdle;
				}
			}
			else
			{
				update = false;
			}
		}
		else
		{
			StreamerApi::GetPlayerCameraPos(player.playerId, &player.position[0], &player.position[1], &player.position[2]);
		}
		if (player.delayedCheckpoint)
		{
			std::unordered_map<int, Item::SharedCheckpoint>::iterator c = core->getData()->checkpoints.find(player.delayedCheckpoint);
			if (c != core->getData()->checkpoints.end())
			{
				StreamerApi::SetPlayerCheckpoint(player.playerId, c->second->position[0], c->second->position[1], c->second->position[2], c->second->size);
				if (c->second->streamCallbacks)
				{
					streamInCallbacks.push_back(std::make_tuple(STREAMER_TYPE_CP, c->first, player.playerId));
				}
				player.visibleCheckpoint = c->first;
			}
			player.delayedCheckpoint = 0;
		}
		else if (player.delayedRaceCheckpoint)
		{
			std::unordered_map<int, Item::SharedRaceCheckpoint>::iterator r = core->getData()->raceCheckpoints.find(player.delayedRaceCheckpoint);
			if (r != core->getData()->raceCheckpoints.end())
			{
				StreamerApi::SetPlayerRaceCheckpoint(player.playerId, r->second->type, r->second->position[0], r->second->position[1], r->second->position[2], r->second->next[0], r->second->next[1], r->second->next[2], r->second->size);
				if (r->second->streamCallbacks)
				{
					streamInCallbacks.push_back(std::make_tuple(STREAMER_TYPE_RACE_CP, r->first, player.playerId));
				}
				player.visibleRaceCheckpoint = r->first;
			}
			player.delayedRaceCheckpoint = 0;
		}
	}
	std::vector<SharedCell> cells;
	if (update)
	{
		core->getGrid()->findAllCellsForPlayer(player, cells);
	}
	else
	{
		core->getGrid()->findMinimalCellsForPlayer(player, cells);
	}
	if (!cells.empty())
	{
		if (!delta.isZero())
		{
			player.position += delta;
		}
		for (std::vector<int>::const_iterator t = core->getData()->typePriority.begin(); t != core->getData()->typePriority.end(); ++t)
		{
			if (update)
			{
				switch (*t)
				{
					case STREAMER_TYPE_OBJECT:
					{
						if (!core->getData()->objects.empty() && player.enabledItems[STREAMER_TYPE_OBJECT])
						{
							PhaseTimer _pt(phaseTimeNs, STREAMER_TYPE_OBJECT);
							if (core->getChunkStreamer()->getChunkStreamingEnabled())
							{
								core->getChunkStreamer()->discoverObjects(player, cells);
							}
							else
							{
								processObjects(player, cells);
							}
						}
						break;
					}
					case STREAMER_TYPE_CP:
					{
						if (!core->getData()->checkpoints.empty() && player.enabledItems[STREAMER_TYPE_CP])
						{
							PhaseTimer _pt(phaseTimeNs, STREAMER_TYPE_CP);
							processCheckpoints(player, cells);
						}
						break;
					}
					case STREAMER_TYPE_RACE_CP:
					{
						if (!core->getData()->raceCheckpoints.empty() && player.enabledItems[STREAMER_TYPE_RACE_CP])
						{
							PhaseTimer _pt(phaseTimeNs, STREAMER_TYPE_RACE_CP);
							processRaceCheckpoints(player, cells);
						}
						break;
					}
					case STREAMER_TYPE_MAP_ICON:
					{
						if (!core->getData()->mapIcons.empty() && player.enabledItems[STREAMER_TYPE_MAP_ICON])
						{
							PhaseTimer _pt(phaseTimeNs, STREAMER_TYPE_MAP_ICON);
							if (core->getChunkStreamer()->getChunkStreamingEnabled())
							{
								core->getChunkStreamer()->discoverMapIcons(player, cells);
							}
							else
							{
								processMapIcons(player, cells);
							}
						}
						break;
					}
					case STREAMER_TYPE_3D_TEXT_LABEL:
					{
						if (!core->getData()->textLabels.empty() && player.enabledItems[STREAMER_TYPE_3D_TEXT_LABEL])
						{
							PhaseTimer _pt(phaseTimeNs, STREAMER_TYPE_3D_TEXT_LABEL);
							if (core->getChunkStreamer()->getChunkStreamingEnabled())
							{
								core->getChunkStreamer()->discoverTextLabels(player, cells);
							}
							else
							{
								processTextLabels(player, cells);
							}
						}
						break;
					}
					case STREAMER_TYPE_AREA:
					{
						if (!core->getData()->areas.empty() && player.enabledItems[STREAMER_TYPE_AREA])
						{
							PhaseTimer _pt(phaseTimeNs, STREAMER_TYPE_AREA);
							if (!delta.isZero())
							{
								player.position = position;
							}
							processAreas(player, cells);
							if (!delta.isZero())
							{
								player.position += delta;
							}
						}
						break;
					}
				}
			}
		}
		if (!delta.isZero())
		{
			player.position = position;
		}
	}
	++phaseTickCount;
}

void Streamer::executeCallbacks()
{
	if (!areaLeaveCallbacks.empty())
	{
		std::multimap<int, std::tuple<int, int> > callbacks;
		std::swap(areaLeaveCallbacks, callbacks);
		for (std::multimap<int, std::tuple<int, int> >::reverse_iterator c = callbacks.rbegin(); c != callbacks.rend(); ++c)
		{
			std::unordered_map<int, Item::SharedArea>::iterator a = core->getData()->areas.find(std::get<0>(c->second));
			if (a != core->getData()->areas.end())
			{
				for (std::set<AMX*>::iterator i = core->getData()->interfaces.begin(); i != core->getData()->interfaces.end(); ++i)
				{
					int amxIndex = 0;
					if (!amx_FindPublic(*i, "OnPlayerLeaveDynamicArea", &amxIndex))
					{
						amx_Push(*i, static_cast<cell>(std::get<0>(c->second)));
						amx_Push(*i, static_cast<cell>(std::get<1>(c->second)));
						amx_Exec(*i, NULL, amxIndex);
					}
				}
			}
		}
	}
	if (!areaEnterCallbacks.empty())
	{
		std::multimap<int, std::tuple<int, int> > callbacks;
		std::swap(areaEnterCallbacks, callbacks);
		for (std::multimap<int, std::tuple<int, int> >::reverse_iterator c = callbacks.rbegin(); c != callbacks.rend(); ++c)
		{
			std::unordered_map<int, Item::SharedArea>::iterator a = core->getData()->areas.find(std::get<0>(c->second));
			if (a != core->getData()->areas.end())
			{
				for (std::set<AMX*>::iterator i = core->getData()->interfaces.begin(); i != core->getData()->interfaces.end(); ++i)
				{
					int amxIndex = 0;
					if (!amx_FindPublic(*i, "OnPlayerEnterDynamicArea", &amxIndex))
					{
						amx_Push(*i, static_cast<cell>(std::get<0>(c->second)));
						amx_Push(*i, static_cast<cell>(std::get<1>(c->second)));
						amx_Exec(*i, NULL, amxIndex);
					}
				}
			}
		}
	}
	if (!objectMoveCallbacks.empty())
	{
		std::vector<int> callbacks;
		std::swap(objectMoveCallbacks, callbacks);
		for (std::vector<int>::const_iterator c = callbacks.begin(); c != callbacks.end(); ++c)
		{
			std::unordered_map<int, Item::SharedObject>::iterator o = core->getData()->objects.find(*c);
			if (o != core->getData()->objects.end())
			{
				for (std::set<AMX*>::iterator i = core->getData()->interfaces.begin(); i != core->getData()->interfaces.end(); ++i)
				{
					int amxIndex = 0;
					if (!amx_FindPublic(*i, "OnDynamicObjectMoved", &amxIndex))
					{
						amx_Push(*i, static_cast<cell>(*c));
						amx_Exec(*i, NULL, amxIndex);
					}
				}
				for (auto* h : GetStreamerEventHandlers()) h->onDynamicObjectMoved(*c);
			}
		}
	}
	if (!streamInCallbacks.empty())
	{
		std::vector<std::tuple<int, int, int> > callbacks;
		std::swap(streamInCallbacks, callbacks);
		for (std::vector<std::tuple<int, int, int> >::const_iterator c = callbacks.begin(); c != callbacks.end(); ++c)
		{
			switch (std::get<0>(*c))
			{
				case STREAMER_TYPE_OBJECT:
				{
					if (core->getData()->objects.find(std::get<1>(*c)) == core->getData()->objects.end())
					{
						continue;
					}
					break;
				}
				case STREAMER_TYPE_PICKUP:
				{
					if (core->getData()->pickups.find(std::get<1>(*c)) == core->getData()->pickups.end())
					{
						continue;
					}
					break;
				}
				case STREAMER_TYPE_CP:
				{
					if (core->getData()->checkpoints.find(std::get<1>(*c)) == core->getData()->checkpoints.end())
					{
						continue;
					}
					break;
				}
				case STREAMER_TYPE_RACE_CP:
				{
					if (core->getData()->raceCheckpoints.find(std::get<1>(*c)) == core->getData()->raceCheckpoints.end())
					{
						continue;
					}
					break;
				}
				case STREAMER_TYPE_MAP_ICON:
				{
					if (core->getData()->mapIcons.find(std::get<1>(*c)) == core->getData()->mapIcons.end())
					{
						continue;
					}
					break;
				}
				case STREAMER_TYPE_3D_TEXT_LABEL:
				{
					if (core->getData()->textLabels.find(std::get<1>(*c)) == core->getData()->textLabels.end())
					{
						continue;
					}
					break;
				}
			}
			for (std::set<AMX*>::iterator i = core->getData()->interfaces.begin(); i != core->getData()->interfaces.end(); ++i)
			{
				int amxIndex = 0;
				if (!amx_FindPublic(*i, "Streamer_OnItemStreamIn", &amxIndex))
				{
					amx_Push(*i, static_cast<cell>(std::get<2>(*c)));
					amx_Push(*i, static_cast<cell>(std::get<1>(*c)));
					amx_Push(*i, static_cast<cell>(std::get<0>(*c)));
					amx_Exec(*i, NULL, amxIndex);
				}
			}
			for (auto* h : GetStreamerEventHandlers())
			{
				int type = std::get<0>(*c), id = std::get<1>(*c), forPlayer = std::get<2>(*c);
				switch (type)
				{
					case STREAMER_TYPE_OBJECT:         h->onDynamicObjectStreamIn(id, forPlayer); break;
					case STREAMER_TYPE_PICKUP:         h->onDynamicPickupStreamIn(id, forPlayer); break;
					case STREAMER_TYPE_CP:             h->onDynamicCheckpointStreamIn(id, forPlayer); break;
					case STREAMER_TYPE_MAP_ICON:       h->onDynamicMapIconStreamIn(id, forPlayer); break;
					case STREAMER_TYPE_3D_TEXT_LABEL:  h->onDynamicTextLabelStreamIn(id, forPlayer); break;
				}
			}
		}
	}
	if (!streamOutCallbacks.empty())
	{
		std::vector<std::tuple<int, int, int> > callbacks;
		std::swap(streamOutCallbacks, callbacks);
		for (std::vector<std::tuple<int, int, int> >::const_iterator c = callbacks.begin(); c != callbacks.end(); ++c)
		{
			switch (std::get<0>(*c))
			{
				case STREAMER_TYPE_OBJECT:
				{
					if (core->getData()->objects.find(std::get<1>(*c)) == core->getData()->objects.end())
					{
						continue;
					}
					break;
				}
				case STREAMER_TYPE_PICKUP:
				{
					if (core->getData()->pickups.find(std::get<1>(*c)) == core->getData()->pickups.end())
					{
						continue;
					}
					break;
				}
				case STREAMER_TYPE_CP:
				{
					if (core->getData()->checkpoints.find(std::get<1>(*c)) == core->getData()->checkpoints.end())
					{
						continue;
					}
					break;
				}
				case STREAMER_TYPE_RACE_CP:
				{
					if (core->getData()->raceCheckpoints.find(std::get<1>(*c)) == core->getData()->raceCheckpoints.end())
					{
						continue;
					}
					break;
				}
				case STREAMER_TYPE_MAP_ICON:
				{
					if (core->getData()->mapIcons.find(std::get<1>(*c)) == core->getData()->mapIcons.end())
					{
						continue;
					}
					break;
				}
				case STREAMER_TYPE_3D_TEXT_LABEL:
				{
					if (core->getData()->textLabels.find(std::get<1>(*c)) == core->getData()->textLabels.end())
					{
						continue;
					}
					break;
				}
			}
			for (std::set<AMX*>::iterator i = core->getData()->interfaces.begin(); i != core->getData()->interfaces.end(); ++i)
			{
				int amxIndex = 0;
				if (!amx_FindPublic(*i, "Streamer_OnItemStreamOut", &amxIndex))
				{
					amx_Push(*i, static_cast<cell>(std::get<2>(*c)));
					amx_Push(*i, static_cast<cell>(std::get<1>(*c)));
					amx_Push(*i, static_cast<cell>(std::get<0>(*c)));
					amx_Exec(*i, NULL, amxIndex);
				}
			}
			for (auto* h : GetStreamerEventHandlers())
			{
				int type = std::get<0>(*c), id = std::get<1>(*c), forPlayer = std::get<2>(*c);
				switch (type)
				{
					case STREAMER_TYPE_OBJECT:         h->onDynamicObjectStreamOut(id, forPlayer); break;
					case STREAMER_TYPE_PICKUP:         h->onDynamicPickupStreamOut(id, forPlayer); break;
					case STREAMER_TYPE_CP:             h->onDynamicCheckpointStreamOut(id, forPlayer); break;
					case STREAMER_TYPE_MAP_ICON:       h->onDynamicMapIconStreamOut(id, forPlayer); break;
					case STREAMER_TYPE_3D_TEXT_LABEL:  h->onDynamicTextLabelStreamOut(id, forPlayer); break;
				}
			}
		}
	}
}

void Streamer::discoverActors(Player &player, const std::vector<SharedCell> &cells)
{
	if (!StreamerApi::IsPlayerNPC(player.playerId))
	{
		for (std::vector<SharedCell>::const_iterator c = cells.begin(); c != cells.end(); ++c)
		{
			for (std::unordered_map<int, Item::SharedActor>::const_iterator a = (*c)->actors.begin(); a != (*c)->actors.end(); ++a)
			{
				for (std::unordered_set<int>::const_iterator w = a->second->worlds.begin(); w != a->second->worlds.end(); ++w)
				{
					if (player.worldId != *w && *w != -1)
					{
						continue;
					}

					std::unordered_map<std::pair<int, int>, Item::SharedActor, pair_hash>::iterator d = core->getData()->discoveredActors.find(std::make_pair(a->first, *w));
					if (d == core->getData()->discoveredActors.end())
					{
						const int playerWorldId = *w == -1 ? -1 : player.worldId;
						if (doesPlayerSatisfyConditions(a->second->players, player.playerId, a->second->interiors, player.interiorId, a->second->worlds, playerWorldId, a->second->areas, player.internalAreas, a->second->inverseAreaChecking))
						{
							if (a->second->comparableStreamDistance < STREAMER_STATIC_DISTANCE_CUTOFF || squaredDistance3(player.position, Eigen::Vector3f(a->second->position + a->second->positionOffset)) < (a->second->comparableStreamDistance * player.radiusMultipliers[STREAMER_TYPE_ACTOR]))
							{
								core->getData()->discoveredActors.insert(std::make_pair(std::make_pair(a->first, *w), a->second));
							}
						}
					}
				}
			}
		}
	}
}

void Streamer::streamActors()
{
	std::unordered_map<std::pair<int, int>, int, pair_hash>::iterator i = core->getData()->internalActors.begin();
	while (i != core->getData()->internalActors.end())
	{
		std::unordered_map<std::pair<int, int>, Item::SharedActor, pair_hash>::iterator d = core->getData()->discoveredActors.find(i->first);
		if (d == core->getData()->discoveredActors.end())
		{
			StreamerApi::DestroyActor(i->second);
			i = core->getData()->internalActors.erase(i);
		}
		else
		{
			core->getData()->discoveredActors.erase(d);
			++i;
		}
	}
	// Priority-ordered creation list. Ascending by priority matches the previous
	// multimap iteration order (std::multimap<int> = std::less<int>).
	std::vector<std::tuple<int, int, Item::SharedActor>> sortedActors;
	sortedActors.reserve(core->getData()->discoveredActors.size());
	for (std::unordered_map<std::pair<int, int>, Item::SharedActor, pair_hash>::iterator d = core->getData()->discoveredActors.begin(); d != core->getData()->discoveredActors.end(); ++d)
	{
		sortedActors.emplace_back(d->second->priority, d->first.second, d->second);
	}
	core->getData()->discoveredActors.clear();
	std::sort(sortedActors.begin(), sortedActors.end(), [](auto const &a, auto const &b) {
		return std::get<0>(a) < std::get<0>(b);
	});
	for (auto &s : sortedActors)
	{
		if (core->getData()->internalActors.size() == core->getData()->getGlobalMaxVisibleItems(STREAMER_TYPE_ACTOR))
		{
			break;
		}
		const auto &actor = std::get<2>(s);
		const int worldId = std::get<1>(s);
		int internalId = StreamerApi::CreateActor(actor->modelId, actor->position[0], actor->position[1], actor->position[2], actor->rotation);
		if (internalId == INVALID_ACTOR_ID)
		{
			break;
		}
		StreamerApi::SetActorInvulnerable(internalId, actor->invulnerable);
		StreamerApi::SetActorHealth(internalId, actor->health);
		StreamerApi::SetActorVirtualWorld(internalId, worldId);
		if (actor->anim)
		{
			StreamerApi::ApplyActorAnimation(internalId, actor->anim->lib.c_str(), actor->anim->name.c_str(), actor->anim->delta, actor->anim->loop, actor->anim->lockx, actor->anim->locky, actor->anim->freeze, actor->anim->time);
		}
		core->getData()->internalActors.insert(std::make_pair(std::make_pair(actor->actorId, worldId), internalId));
	}
}

void Streamer::processAreas(Player &player, const std::vector<SharedCell> &cells)
{
	int state = StreamerApi::GetPlayerState(player.playerId);
	for (std::vector<SharedCell>::const_iterator c = cells.begin(); c != cells.end(); ++c)
	{
		for (std::unordered_map<int, Item::SharedArea>::const_iterator a = (*c)->areas.begin(); a != (*c)->areas.end(); ++a)
		{
			Streamer::processPlayerArea(player, a->second, state);
		}
	}
}

bool Streamer::processPlayerArea(Player &player, const Item::SharedArea &a, const int state)
{
	bool inArea = false;
	if (doesPlayerSatisfyConditions(a->players, player.playerId, a->interiors, player.interiorId, a->worlds, player.worldId) && ((!a->spectateMode && state != PLAYER_STATE_SPECTATING) || a->spectateMode))
	{
		inArea = Utility::isPointInArea(player.position, a);
	}
	std::unordered_set<int>::iterator foundArea = player.internalAreas.find(a->areaId);
	if (inArea)
	{
		if (foundArea == player.internalAreas.end())
		{
			player.internalAreas.insert(a->areaId);
			areaEnterCallbacks.insert(std::make_pair(a->priority, std::make_tuple(a->areaId, player.playerId)));
		}
		if (a->cell)
		{
			player.visibleCell->areas.insert(std::make_pair(a->areaId, a));
		}
	}
	else
	{
		if (foundArea != player.internalAreas.end())
		{
			player.internalAreas.erase(foundArea);
			areaLeaveCallbacks.insert(std::make_pair(a->priority, std::make_tuple(a->areaId, player.playerId)));
		}
	}
	return inArea;
}

void Streamer::processCheckpoints(Player &player, const std::vector<SharedCell> &cells)
{
	// Only the best candidate is consumed below, so keep the running minimum in a local
	// entry instead of inserting every candidate into a multimap and sorting.
	std::pair<std::pair<int, float>, Item::SharedCheckpoint> bestEntry{{std::numeric_limits<int>::min(), std::numeric_limits<float>::infinity()}, Item::SharedCheckpoint()};
	bool haveBest = false;
	CandidateCmp cmp;
	for (std::vector<SharedCell>::const_iterator c = cells.begin(); c != cells.end(); ++c)
	{
		for (std::unordered_map<int, Item::SharedCheckpoint>::const_iterator d = (*c)->checkpoints.begin(); d != (*c)->checkpoints.end(); ++d)
		{
			float distance = std::numeric_limits<float>::infinity();
			if (doesPlayerSatisfyConditions(d->second->players, player.playerId, d->second->interiors, player.interiorId, d->second->worlds, player.worldId, d->second->areas, player.internalAreas, d->second->inverseAreaChecking))
			{
				if (d->second->comparableStreamDistance < STREAMER_STATIC_DISTANCE_CUTOFF)
				{
					distance = std::numeric_limits<float>::infinity() * -1.0f;
				}
				else
				{
					distance = squaredDistance3(player.position, Eigen::Vector3f(d->second->position + d->second->positionOffset));
				}
			}
			if (distance < (d->second->comparableStreamDistance * player.radiusMultipliers[STREAMER_TYPE_CP]))
			{
				std::pair<std::pair<int, float>, Item::SharedCheckpoint> candidate{{d->second->priority, distance}, d->second};
				if (!haveBest || cmp(candidate, bestEntry))
				{
					bestEntry = candidate;
					haveBest = true;
				}
			}
			else
			{
				if (d->first == player.visibleCheckpoint)
				{
					StreamerApi::DisablePlayerCheckpoint(player.playerId);
					if (d->second->streamCallbacks)
					{
						streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_CP, d->second->checkpointId, player.playerId));
					}
					player.activeCheckpoint = 0;
					player.visibleCheckpoint = 0;

				}
			}
		}
	}
	if (haveBest)
	{
		auto &d = bestEntry;
		if (d.second->checkpointId != player.visibleCheckpoint)
		{
			if (player.visibleCheckpoint)
			{
				StreamerApi::DisablePlayerCheckpoint(player.playerId);
				if (d.second->streamCallbacks)
				{
					streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_CP, d.second->checkpointId, player.playerId));
				}
				player.activeCheckpoint = 0;
			}
			player.delayedCheckpoint = d.second->checkpointId;
		}
		if (d.second->cell)
		{
			player.visibleCell->checkpoints.insert(std::make_pair(d.second->checkpointId, d.second));
		}
	}
}

void Streamer::processMapIcons(Player &player, const std::vector<SharedCell> &cells)
{
	std::vector<std::pair<std::pair<int, float>, Item::SharedMapIcon>> discoveredMapIcons, existingMapIcons;
	for (std::vector<SharedCell>::const_iterator c = cells.begin(); c != cells.end(); ++c)
	{
		for (std::unordered_map<int, Item::SharedMapIcon>::const_iterator m = (*c)->mapIcons.begin(); m != (*c)->mapIcons.end(); ++m)
		{
			float distance = std::numeric_limits<float>::infinity();
			if (doesPlayerSatisfyConditions(m->second->players, player.playerId, m->second->interiors, player.interiorId, m->second->worlds, player.worldId, m->second->areas, player.internalAreas, m->second->inverseAreaChecking))
			{
				if (m->second->comparableStreamDistance < STREAMER_STATIC_DISTANCE_CUTOFF)
				{
					distance = std::numeric_limits<float>::infinity() * -1.0f;
				}
				else
				{
					distance = squaredDistance3(player.position, Eigen::Vector3f(m->second->position + m->second->positionOffset));
				}
			}
			std::unordered_map<int, int>::iterator i = player.internalMapIcons.find(m->first);
			const float thresholdIn = m->second->comparableStreamDistance * player.radiusMultipliers[STREAMER_TYPE_MAP_ICON];
			const float hyst = hysteresisFactor[STREAMER_TYPE_MAP_ICON];
			const float thresholdOut = thresholdIn * hyst * hyst;
			if (distance < thresholdIn)
			{
				if (i == player.internalMapIcons.end())
				{
					discoveredMapIcons.emplace_back(std::make_pair(m->second->priority, distance), m->second);
				}
				else
				{
					if (m->second->cell)
					{
						player.visibleCell->mapIcons.insert(*m);
					}
					existingMapIcons.emplace_back(std::make_pair(m->second->priority, distance), m->second);
				}
			}
			else if (distance < thresholdOut && i != player.internalMapIcons.end())
			{
				if (m->second->cell)
				{
					player.visibleCell->mapIcons.insert(*m);
				}
				existingMapIcons.emplace_back(std::make_pair(m->second->priority, distance), m->second);
			}
			else
			{
				if (i != player.internalMapIcons.end())
				{
					StreamerApi::RemovePlayerMapIcon(player.playerId, i->second);
					++phaseStreamOutCount[STREAMER_TYPE_MAP_ICON];
					if (m->second->streamCallbacks)
					{
						streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_MAP_ICON, m->first, player.playerId));
					}
					player.mapIconIdentifier.remove(i->second, player.internalMapIcons.size());
					player.internalMapIcons.erase(i);
				}
			}
		}
	}
	std::sort(discoveredMapIcons.begin(), discoveredMapIcons.end(), CandidateCmp{});
	std::sort(existingMapIcons.begin(), existingMapIcons.end(), CandidateCmp{});
	for (auto &d : discoveredMapIcons)
	{
		std::unordered_map<int, int>::iterator i = player.internalMapIcons.find(d.second->mapIconId);
		if (i != player.internalMapIcons.end())
		{
			continue;
		}
		if (player.internalMapIcons.size() == player.maxVisibleMapIcons)
		{
			if (!existingMapIcons.empty())
			{
				auto &e = existingMapIcons.back();
				if (e.first.first < d.first.first || (e.first.second > STREAMER_STATIC_DISTANCE_CUTOFF && d.first.second < e.first.second))
				{
					std::unordered_map<int, int>::iterator j = player.internalMapIcons.find(e.second->mapIconId);
					if (j != player.internalMapIcons.end())
					{
						StreamerApi::RemovePlayerMapIcon(player.playerId, j->second);
						if (e.second->streamCallbacks)
						{
							streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_MAP_ICON, e.second->mapIconId, player.playerId));
						}
						player.mapIconIdentifier.remove(j->second, player.internalMapIcons.size());
						player.internalMapIcons.erase(j);
					}
					if (e.second->cell)
					{
						player.visibleCell->mapIcons.erase(e.second->mapIconId);
					}
					existingMapIcons.pop_back();
				}
			}
			if (player.internalMapIcons.size() == player.maxVisibleMapIcons)
			{
				break;
			}
		}
		int internalId = player.mapIconIdentifier.get();
		StreamerApi::SetPlayerMapIcon(player.playerId, internalId, d.second->position[0], d.second->position[1], d.second->position[2], d.second->type, d.second->color, d.second->style);
		++phaseStreamInCount[STREAMER_TYPE_MAP_ICON];
		if (d.second->streamCallbacks)
		{
			streamInCallbacks.push_back(std::make_tuple(STREAMER_TYPE_MAP_ICON, d.second->mapIconId, player.playerId));
		}
		player.internalMapIcons.insert(std::make_pair(d.second->mapIconId, internalId));
		if (d.second->cell)
		{
			player.visibleCell->mapIcons.insert(std::make_pair(d.second->mapIconId, d.second));
		}
	}
}

void Streamer::processObjects(Player &player, const std::vector<SharedCell> &cells)
{
	// vector+sort replaces per-item multimap<pair<priority, distance>> inserts:
	// each allocation skipped, single O(N log N) sort instead of N log N tree inserts.
	std::vector<std::pair<std::pair<int, float>, Item::SharedObject>> discoveredObjects, existingObjects;
	for (std::vector<SharedCell>::const_iterator c = cells.begin(); c != cells.end(); ++c)
	{
		for (std::unordered_map<int, Item::SharedObject>::const_iterator o = (*c)->objects.begin(); o != (*c)->objects.end(); ++o)
		{
			float distance = std::numeric_limits<float>::infinity();
			if (doesPlayerSatisfyConditions(o->second->players, player.playerId, o->second->interiors, player.interiorId, o->second->attach ? o->second->attach->worlds : o->second->worlds, player.worldId, o->second->areas, player.internalAreas, o->second->inverseAreaChecking))
			{
				if (o->second->comparableStreamDistance < STREAMER_STATIC_DISTANCE_CUTOFF)
				{
					distance = std::numeric_limits<float>::infinity() * -1.0f;
				}
				else
				{
					if (o->second->attach)
					{
						distance = squaredDistance3(player.position, o->second->attach->position) + std::numeric_limits<float>::epsilon();
					}
					else
					{
						distance = squaredDistance3(player.position, Eigen::Vector3f(o->second->position + o->second->positionOffset));
					}
				}
			}
			std::unordered_map<int, int>::iterator i = player.internalObjects.find(o->first);
			const float thresholdIn = o->second->comparableStreamDistance * player.radiusMultipliers[STREAMER_TYPE_OBJECT];
			const float hyst = hysteresisFactor[STREAMER_TYPE_OBJECT];
			const float thresholdOut = thresholdIn * hyst * hyst;
			if (distance < thresholdIn)
			{
				if (i == player.internalObjects.end())
				{
					discoveredObjects.emplace_back(std::make_pair(o->second->priority, distance), o->second);
				}
				else
				{
					if (o->second->cell)
					{
						player.visibleCell->objects.insert(*o);
					}
					existingObjects.emplace_back(std::make_pair(o->second->priority, distance), o->second);
				}
			}
			else if (distance < thresholdOut && i != player.internalObjects.end())
			{
				// Hysteresis band: already streamed, keep it (don't add new, don't destroy).
				if (o->second->cell)
				{
					player.visibleCell->objects.insert(*o);
				}
				existingObjects.emplace_back(std::make_pair(o->second->priority, distance), o->second);
			}
			else
			{
				if (i != player.internalObjects.end())
				{
					StreamerApi::DestroyPlayerObject(player.playerId, i->second);
					++player.diagObjDestroys; // FLOOD DIAGNOSTIC
					++phaseStreamOutCount[STREAMER_TYPE_OBJECT];
					if (o->second->streamCallbacks)
					{
						streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_OBJECT, o->first, player.playerId));
					}
					player.internalObjects.erase(i);
				}
			}
		}
	}
	std::sort(discoveredObjects.begin(), discoveredObjects.end(), CandidateCmp{});
	std::sort(existingObjects.begin(), existingObjects.end(), CandidateCmp{});
	for (auto &d : discoveredObjects)
	{
		std::unordered_map<int, int>::iterator i = player.internalObjects.find(d.second->objectId);
		if (i != player.internalObjects.end())
		{
			continue;
		}
		int internalBaseId = INVALID_STREAMER_ID;
		if (d.second->attach)
		{
			if (d.second->attach->object != INVALID_STREAMER_ID)
			{
				std::unordered_map<int, int>::iterator j = player.internalObjects.find(d.second->attach->object);
				if (j == player.internalObjects.end())
				{
					continue;
				}
				internalBaseId = j->second;
			}
		}
		if (player.internalObjects.size() == player.currentVisibleObjects)
		{
			if (!existingObjects.empty())
			{
				auto &e = existingObjects.back();
				if (e.first.first < d.first.first || (e.first.second > STREAMER_STATIC_DISTANCE_CUTOFF && d.first.second < e.first.second))
				{
					std::unordered_map<int, int>::iterator j = player.internalObjects.find(e.second->objectId);
					if (j != player.internalObjects.end())
					{
						StreamerApi::DestroyPlayerObject(player.playerId, j->second);
						++player.diagObjDestroys; // FLOOD DIAGNOSTIC
						if (e.second->streamCallbacks)
						{
							streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_OBJECT, e.second->objectId, player.playerId));
						}
						player.internalObjects.erase(j);
					}
					if (e.second->cell)
					{
						player.visibleCell->objects.erase(e.second->objectId);
					}
					existingObjects.pop_back();
				}
			}
		}
		if (player.internalObjects.size() == player.maxVisibleObjects)
		{
			player.currentVisibleObjects = player.internalObjects.size();
			break;
		}
		int internalId = StreamerApi::CreatePlayerObject(player.playerId, d.second->modelId, d.second->position[0], d.second->position[1], d.second->position[2], d.second->rotation[0], d.second->rotation[1], d.second->rotation[2], d.second->drawDistance);
		if (internalId == INVALID_OBJECT_ID)
		{
			player.currentVisibleObjects = player.internalObjects.size();
			break;
		}
		++player.diagObjCreates; // FLOOD DIAGNOSTIC
		++player.diagObjCreateIds[d.second->objectId]; // FLOOD DIAGNOSTIC
		++phaseStreamInCount[STREAMER_TYPE_OBJECT];
		if (d.second->streamCallbacks)
		{
			streamInCallbacks.push_back(std::make_tuple(STREAMER_TYPE_OBJECT, d.second->objectId, player.playerId));
		}
		if (d.second->attach)
		{
			if (internalBaseId != INVALID_STREAMER_ID)
			{
				StreamerApi::AttachPlayerObjectToObject(player.playerId, internalId, internalBaseId, d.second->attach->positionOffset[0], d.second->attach->positionOffset[1], d.second->attach->positionOffset[2], d.second->attach->rotation[0], d.second->attach->rotation[1], d.second->attach->rotation[2], d.second->attach->syncRotation);
			}
			else if (d.second->attach->player != INVALID_PLAYER_ID)
			{
				StreamerApi::AttachPlayerObjectToPlayer(player.playerId, internalId, d.second->attach->player, d.second->attach->positionOffset[0], d.second->attach->positionOffset[1], d.second->attach->positionOffset[2], d.second->attach->rotation[0], d.second->attach->rotation[1], d.second->attach->rotation[2]);
			}
			else if (d.second->attach->vehicle != INVALID_VEHICLE_ID)
			{
				StreamerApi::AttachPlayerObjectToVehicle(player.playerId, internalId, d.second->attach->vehicle, d.second->attach->positionOffset[0], d.second->attach->positionOffset[1], d.second->attach->positionOffset[2], d.second->attach->rotation[0], d.second->attach->rotation[1], d.second->attach->rotation[2]);
			}
		}
		else if (d.second->move)
		{
			StreamerApi::MovePlayerObject(player.playerId, internalId, std::get<0>(d.second->move->position)[0], std::get<0>(d.second->move->position)[1], std::get<0>(d.second->move->position)[2], d.second->move->speed, std::get<0>(d.second->move->rotation)[0], std::get<0>(d.second->move->rotation)[1], std::get<0>(d.second->move->rotation)[2]);
		}
		for (std::unordered_map<int, Item::Object::Material>::iterator m = d.second->materials.begin(); m != d.second->materials.end(); ++m)
		{
			if (m->second.main)
			{
				StreamerApi::SetPlayerObjectMaterial(player.playerId, internalId, m->first, m->second.main->modelId, m->second.main->txdFileName.c_str(), m->second.main->textureName.c_str(), m->second.main->materialColor);
			}
			else if (m->second.text)
			{
				StreamerApi::SetPlayerObjectMaterialText(player.playerId, internalId, m->second.text->materialText.c_str(), m->first, m->second.text->materialSize, m->second.text->fontFace.c_str(), m->second.text->fontSize, m->second.text->bold, m->second.text->fontColor, m->second.text->backColor, m->second.text->textAlignment);
			}
		}
		if (d.second->noCameraCollision)
		{
			StreamerApi::SetPlayerObjectNoCameraCol(player.playerId, internalId);
		}
		player.internalObjects.insert(std::make_pair(d.second->objectId, internalId));
		if (d.second->cell)
		{
			player.visibleCell->objects.insert(std::make_pair(d.second->objectId, d.second));
		}
	}
}

void Streamer::discoverPickups(Player &player, const std::vector<SharedCell> &cells)
{
	for (std::vector<SharedCell>::const_iterator c = cells.begin(); c != cells.end(); ++c)
	{
		for (std::unordered_map<int, Item::SharedPickup>::const_iterator p = (*c)->pickups.begin(); p != (*c)->pickups.end(); ++p)
		{
			for (std::unordered_set<int>::const_iterator w = p->second->worlds.begin(); w != p->second->worlds.end(); ++w)
			{
				if (player.worldId != *w && *w != -1)
				{
					continue;
				}

				std::unordered_map<std::pair<int, int>, Item::SharedPickup, pair_hash>::iterator d = core->getData()->discoveredPickups.find(std::make_pair(p->first, *w));
				if (d == core->getData()->discoveredPickups.end())
				{
					const int playerWorldId = *w == -1 ? -1 : player.worldId;
					if (doesPlayerSatisfyConditions(p->second->players, player.playerId, p->second->interiors, player.interiorId, p->second->worlds, playerWorldId, p->second->areas, player.internalAreas, p->second->inverseAreaChecking))
					{
						if (p->second->comparableStreamDistance < STREAMER_STATIC_DISTANCE_CUTOFF || squaredDistance3(player.position, Eigen::Vector3f(p->second->position + p->second->positionOffset)) < (p->second->comparableStreamDistance * player.radiusMultipliers[STREAMER_TYPE_PICKUP]))
						{
							core->getData()->discoveredPickups.insert(std::make_pair(std::make_pair(p->first, *w), p->second));
						}
					}
				}
			}
		}
	}
}

void Streamer::streamPickups()
{
	std::unordered_map<std::pair<int, int>, int, pair_hash>::iterator i = core->getData()->internalPickups.begin();
	while (i != core->getData()->internalPickups.end())
	{
		std::unordered_map<std::pair<int, int>, Item::SharedPickup, pair_hash>::iterator d = core->getData()->discoveredPickups.find(i->first);
		if (d == core->getData()->discoveredPickups.end())
		{
			StreamerApi::DestroyPickup(i->second);
			std::unordered_map<int, Item::SharedPickup>::iterator p = core->getData()->pickups.find(i->first.first);
			if (p != core->getData()->pickups.end())
			{
				if (p->second->streamCallbacks)
				{
					streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_PICKUP, i->first.first, INVALID_PLAYER_ID));
				}
			}
			i = core->getData()->internalPickups.erase(i);
		}
		else
		{
			core->getData()->discoveredPickups.erase(d);
			++i;
		}
	}
	std::vector<std::tuple<int, int, Item::SharedPickup>> sortedPickups;
	sortedPickups.reserve(core->getData()->discoveredPickups.size());
	for (std::unordered_map<std::pair<int, int>, Item::SharedPickup, pair_hash>::iterator d = core->getData()->discoveredPickups.begin(); d != core->getData()->discoveredPickups.end(); ++d)
	{
		sortedPickups.emplace_back(d->second->priority, d->first.second, d->second);
	}
	core->getData()->discoveredPickups.clear();
	std::sort(sortedPickups.begin(), sortedPickups.end(), [](auto const &a, auto const &b) {
		return std::get<0>(a) < std::get<0>(b);
	});
	for (auto &s : sortedPickups)
	{
		if (core->getData()->internalPickups.size() == core->getData()->getGlobalMaxVisibleItems(STREAMER_TYPE_PICKUP))
		{
			break;
		}
		const auto &pickup = std::get<2>(s);
		const int worldId = std::get<1>(s);
		int internalId = StreamerApi::CreatePickup(pickup->modelId, pickup->type, pickup->position[0], pickup->position[1], pickup->position[2], worldId);
		if (internalId == INVALID_PICKUP_ID)
		{
			break;
		}
		if (pickup->streamCallbacks)
		{
			streamInCallbacks.push_back(std::make_tuple(STREAMER_TYPE_PICKUP, pickup->pickupId, INVALID_PLAYER_ID));
		}
		core->getData()->internalPickups.insert(std::make_pair(std::make_pair(pickup->pickupId, worldId), internalId));
	}
}

void Streamer::processRaceCheckpoints(Player &player, const std::vector<SharedCell> &cells)
{
	std::pair<std::pair<int, float>, Item::SharedRaceCheckpoint> bestEntry{{std::numeric_limits<int>::min(), std::numeric_limits<float>::infinity()}, Item::SharedRaceCheckpoint()};
	bool haveBest = false;
	CandidateCmp cmp;
	for (std::vector<SharedCell>::const_iterator c = cells.begin(); c != cells.end(); ++c)
	{
		for (std::unordered_map<int, Item::SharedRaceCheckpoint>::const_iterator r = (*c)->raceCheckpoints.begin(); r != (*c)->raceCheckpoints.end(); ++r)
		{
			float distance = std::numeric_limits<float>::infinity();
			if (doesPlayerSatisfyConditions(r->second->players, player.playerId, r->second->interiors, player.interiorId, r->second->worlds, player.worldId, r->second->areas, player.internalAreas, r->second->inverseAreaChecking))
			{
				if (r->second->comparableStreamDistance < STREAMER_STATIC_DISTANCE_CUTOFF)
				{
					distance = std::numeric_limits<float>::infinity() * -1.0f;
				}
				else
				{
					distance = squaredDistance3(player.position, Eigen::Vector3f(r->second->position + r->second->positionOffset));
				}
			}
			if (distance < (r->second->comparableStreamDistance * player.radiusMultipliers[STREAMER_TYPE_RACE_CP]))
			{
				std::pair<std::pair<int, float>, Item::SharedRaceCheckpoint> candidate{{r->second->priority, distance}, r->second};
				if (!haveBest || cmp(candidate, bestEntry))
				{
					bestEntry = candidate;
					haveBest = true;
				}
			}
			else
			{
				if (r->first == player.visibleRaceCheckpoint)
				{
					StreamerApi::DisablePlayerRaceCheckpoint(player.playerId);
					if (r->second->streamCallbacks)
					{
						streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_RACE_CP, r->second->raceCheckpointId, player.playerId));
					}
					player.activeRaceCheckpoint = 0;
					player.visibleRaceCheckpoint = 0;
				}
			}
		}
	}
	if (haveBest)
	{
		auto &d = bestEntry;
		if (d.second->raceCheckpointId != player.visibleRaceCheckpoint)
		{
			if (player.visibleRaceCheckpoint)
			{
				StreamerApi::DisablePlayerRaceCheckpoint(player.playerId);
				if (d.second->streamCallbacks)
				{
					streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_RACE_CP, d.second->raceCheckpointId, player.playerId));
				}
				player.activeRaceCheckpoint = 0;
			}
			player.delayedRaceCheckpoint = d.second->raceCheckpointId;
		}
		if (d.second->cell)
		{
			player.visibleCell->raceCheckpoints.insert(std::make_pair(d.second->raceCheckpointId, d.second));
		}
	}
}

void Streamer::processTextLabels(Player &player, const std::vector<SharedCell> &cells)
{
	std::vector<std::pair<std::pair<int, float>, Item::SharedTextLabel>> discoveredTextLabels, existingTextLabels;
	for (std::vector<SharedCell>::const_iterator c = cells.begin(); c != cells.end(); ++c)
	{
		for (std::unordered_map<int, Item::SharedTextLabel>::const_iterator t = (*c)->textLabels.begin(); t != (*c)->textLabels.end(); ++t)
		{
			float distance = std::numeric_limits<float>::infinity();
			if (doesPlayerSatisfyConditions(t->second->players, player.playerId, t->second->interiors, player.interiorId, t->second->attach ? t->second->attach->worlds : t->second->worlds, player.worldId, t->second->areas, player.internalAreas, t->second->inverseAreaChecking))
			{
				if (t->second->comparableStreamDistance < STREAMER_STATIC_DISTANCE_CUTOFF)
				{
					distance = std::numeric_limits<float>::infinity() * -1.0f;
				}
				else
				{
					if (t->second->attach)
					{
						distance = squaredDistance3(player.position, t->second->attach->position);
					}
					else
					{
						distance = squaredDistance3(player.position, Eigen::Vector3f(t->second->position + t->second->positionOffset));
					}
				}
			}
			std::unordered_map<int, int>::iterator i = player.internalTextLabels.find(t->first);
			const float thresholdIn = t->second->comparableStreamDistance * player.radiusMultipliers[STREAMER_TYPE_3D_TEXT_LABEL];
			const float hyst = hysteresisFactor[STREAMER_TYPE_3D_TEXT_LABEL];
			const float thresholdOut = thresholdIn * hyst * hyst;
			if (distance < thresholdIn)
			{
				if (i == player.internalTextLabels.end())
				{
					discoveredTextLabels.emplace_back(std::make_pair(t->second->priority, distance), t->second);
				}
				else
				{
					if (t->second->cell)
					{
						player.visibleCell->textLabels.insert(*t);
					}
					existingTextLabels.emplace_back(std::make_pair(t->second->priority, distance), t->second);
				}
			}
			else if (distance < thresholdOut && i != player.internalTextLabels.end())
			{
				if (t->second->cell)
				{
					player.visibleCell->textLabels.insert(*t);
				}
				existingTextLabels.emplace_back(std::make_pair(t->second->priority, distance), t->second);
			}
			else
			{
				if (i != player.internalTextLabels.end())
				{
					StreamerApi::DeletePlayer3DTextLabel(player.playerId, i->second);
					++phaseStreamOutCount[STREAMER_TYPE_3D_TEXT_LABEL];
					if (t->second->streamCallbacks)
					{
						streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_3D_TEXT_LABEL, t->first, player.playerId));
					}
					player.internalTextLabels.erase(i);
				}
			}
		}
	}
	std::sort(discoveredTextLabels.begin(), discoveredTextLabels.end(), CandidateCmp{});
	std::sort(existingTextLabels.begin(), existingTextLabels.end(), CandidateCmp{});
	for (auto &d : discoveredTextLabels)
	{
		std::unordered_map<int, int>::iterator i = player.internalTextLabels.find(d.second->textLabelId);
		if (i != player.internalTextLabels.end())
		{
			continue;
		}
		if (player.internalTextLabels.size() == player.currentVisibleTextLabels)
		{
			if (!existingTextLabels.empty())
			{
				auto &e = existingTextLabels.back();
				if (e.first.first < d.first.first || (e.first.second > STREAMER_STATIC_DISTANCE_CUTOFF && d.first.second < e.first.second))
				{
					std::unordered_map<int, int>::iterator j = player.internalTextLabels.find(e.second->textLabelId);
					if (j != player.internalTextLabels.end())
					{
						StreamerApi::DeletePlayer3DTextLabel(player.playerId, j->second);
						if (e.second->streamCallbacks)
						{
							streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_3D_TEXT_LABEL, e.second->textLabelId, player.playerId));
						}
						player.internalTextLabels.erase(j);
					}
					if (e.second->cell)
					{
						player.visibleCell->textLabels.erase(e.second->textLabelId);
					}
					existingTextLabels.pop_back();
				}
			}
		}
		if (player.internalTextLabels.size() == player.maxVisibleTextLabels)
		{
			player.currentVisibleTextLabels = player.internalTextLabels.size();
			break;
		}
		int internalId = StreamerApi::CreatePlayer3DTextLabel(player.playerId, d.second->text.c_str(), d.second->color, d.second->position[0], d.second->position[1], d.second->position[2], d.second->drawDistance, d.second->attach ? d.second->attach->player : INVALID_PLAYER_ID, d.second->attach ? d.second->attach->vehicle : INVALID_VEHICLE_ID, d.second->testLOS);
		if (internalId == INVALID_3DTEXT_ID)
		{
			player.currentVisibleTextLabels = player.internalTextLabels.size();
			break;
		}
		++phaseStreamInCount[STREAMER_TYPE_3D_TEXT_LABEL];
		if (d.second->streamCallbacks)
		{
			streamInCallbacks.push_back(std::make_tuple(STREAMER_TYPE_3D_TEXT_LABEL, d.second->textLabelId, player.playerId));
		}
		player.internalTextLabels.insert(std::make_pair(d.second->textLabelId, internalId));
		if (d.second->cell)
		{
			player.visibleCell->textLabels.insert(std::make_pair(d.second->textLabelId, d.second));
		}
	}
}

void Streamer::processActiveItems()
{
	if (!movingObjects.empty())
	{
		processMovingObjects();
	}
	if (!attachedAreas.empty())
	{
		processAttachedAreas();
	}
	if (!attachedObjects.empty())
	{
		processAttachedObjects();
	}
	if (!attachedTextLabels.empty())
	{
		processAttachedTextLabels();
	}
}

void Streamer::processMovingObjects()
{
	std::unordered_set<Item::SharedObject>::iterator o = movingObjects.begin();
	while (o != movingObjects.end())
	{
		bool objectFinishedMoving = false;
		if ((*o)->move)
		{
			std::chrono::duration<float, std::milli> elapsedTime = std::chrono::steady_clock::now() - (*o)->move->time;
			if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsedTime).count() < (*o)->move->duration)
			{
				(*o)->position = std::get<1>((*o)->move->position) + (std::get<2>((*o)->move->position) * elapsedTime.count());
				if (!Utility::almostEquals(std::get<0>((*o)->move->rotation).maxCoeff(), -1000.0f))
				{
					(*o)->rotation = std::get<1>((*o)->move->rotation) + (std::get<2>((*o)->move->rotation) * elapsedTime.count());
				}
			}
			else
			{
				(*o)->position = std::get<0>((*o)->move->position);
				if (!Utility::almostEquals(std::get<0>((*o)->move->rotation).maxCoeff(), -1000.0f))
				{
					(*o)->rotation = std::get<0>((*o)->move->rotation);
				}
				(*o)->move.reset();
				objectMoveCallbacks.push_back((*o)->objectId);
				objectFinishedMoving = true;
			}
			if ((*o)->cell)
			{
				core->getGrid()->removeObject(*o, true);
			}
		}
		if (objectFinishedMoving)
		{
			o = movingObjects.erase(o);
		}
		else
		{
			++o;
		}
	}
}

void Streamer::processAttachedAreas()
{
	for (std::unordered_set<Item::SharedArea>::iterator a = attachedAreas.begin(); a != attachedAreas.end(); ++a)
	{
		if ((*a)->attach)
		{
			bool adjust = false;
			if ((std::get<0>((*a)->attach->object) != INVALID_OBJECT_ID && std::get<1>((*a)->attach->object) != STREAMER_OBJECT_TYPE_DYNAMIC) || (std::get<0>((*a)->attach->object) != INVALID_STREAMER_ID && std::get<1>((*a)->attach->object) == STREAMER_OBJECT_TYPE_DYNAMIC))
			{
				switch (std::get<1>((*a)->attach->object))
				{
					case STREAMER_OBJECT_TYPE_GLOBAL:
					{
						Eigen::Vector3f position = Eigen::Vector3f::Zero(), rotation = Eigen::Vector3f::Zero();
						adjust = StreamerApi::GetObjectPos(std::get<0>((*a)->attach->object), &position[0], &position[1], &position[2]);
						StreamerApi::GetObjectRot(std::get<0>((*a)->attach->object), &rotation[0], &rotation[1], &rotation[2]);
						Utility::constructAttachedArea(*a, std::variant<float, Eigen::Vector3f, Eigen::Vector4f>(rotation), position);
						break;
					}
					case STREAMER_OBJECT_TYPE_PLAYER:
					{
						Eigen::Vector3f position = Eigen::Vector3f::Zero(), rotation = Eigen::Vector3f::Zero();
						adjust = StreamerApi::GetPlayerObjectPos(std::get<2>((*a)->attach->object), std::get<0>((*a)->attach->object), &position[0], &position[1], &position[2]);
						StreamerApi::GetPlayerObjectRot(std::get<2>((*a)->attach->object), std::get<0>((*a)->attach->object), &rotation[0], &rotation[1], &rotation[2]);
						Utility::constructAttachedArea(*a, std::variant<float, Eigen::Vector3f, Eigen::Vector4f>(rotation), position);
						break;
					}
					case STREAMER_OBJECT_TYPE_DYNAMIC:
					{
						std::unordered_map<int, Item::SharedObject>::iterator o = core->getData()->objects.find(std::get<0>((*a)->attach->object));
						if (o != core->getData()->objects.end())
						{
							Utility::constructAttachedArea(*a, std::variant<float, Eigen::Vector3f, Eigen::Vector4f>(o->second->rotation), o->second->position);
							adjust = true;
						}
						break;
					}
				}
			}
			else if ((*a)->attach->player != INVALID_PLAYER_ID)
			{
				float heading = 0.0f;
				Eigen::Vector3f position = Eigen::Vector3f::Zero();
				adjust = StreamerApi::GetPlayerPos((*a)->attach->player, &position[0], &position[1], &position[2]);
				StreamerApi::GetPlayerFacingAngle((*a)->attach->player, &heading);
				Utility::constructAttachedArea(*a, std::variant<float, Eigen::Vector3f, Eigen::Vector4f>(heading), position);
			}
			else if ((*a)->attach->vehicle != INVALID_VEHICLE_ID)
			{
				bool occupied = false;
				for (std::unordered_map<int, Player>::iterator p = core->getData()->players.begin(); p != core->getData()->players.end(); ++p)
				{
					if (StreamerApi::GetPlayerState(p->first) == PLAYER_STATE_DRIVER)
					{
						if (StreamerApi::GetPlayerVehicleID(p->first) == (*a)->attach->vehicle)
						{
							occupied = true;
							break;
						}
					}
				}
				Eigen::Vector3f position = Eigen::Vector3f::Zero();
				adjust = StreamerApi::GetVehiclePos((*a)->attach->vehicle, &position[0], &position[1], &position[2]);
				if (!occupied)
				{
					float heading = 0.0f;
					StreamerApi::GetVehicleZAngle((*a)->attach->vehicle, &heading);
					Utility::constructAttachedArea(*a, std::variant<float, Eigen::Vector3f, Eigen::Vector4f>(heading), position);
				}
				else
				{
					Eigen::Vector4f quaternion = Eigen::Vector4f::Zero();
					StreamerApi::GetVehicleRotationQuat((*a)->attach->vehicle, &quaternion[0], &quaternion[1], &quaternion[2], &quaternion[3]);
					Utility::constructAttachedArea(*a, std::variant<float, Eigen::Vector3f, Eigen::Vector4f>(quaternion), position);
				}
			}
			if (adjust)
			{
				if ((*a)->cell)
				{
					core->getGrid()->removeArea(*a, true);
				}
			}
			else
			{
				switch ((*a)->type)
				{
					case STREAMER_AREA_TYPE_CIRCLE:
					case STREAMER_AREA_TYPE_CYLINDER:
					{
						std::get<Eigen::Vector2f>((*a)->attach->position).fill(std::numeric_limits<float>::infinity());
						break;
					}
					case STREAMER_AREA_TYPE_SPHERE:
					{
						std::get<Eigen::Vector3f>((*a)->attach->position).fill(std::numeric_limits<float>::infinity());
						break;
					}
					case STREAMER_AREA_TYPE_RECTANGLE:
					{
						std::get<Box2d>((*a)->attach->position).min_corner().fill(std::numeric_limits<float>::infinity());
						std::get<Box2d>((*a)->attach->position).max_corner().fill(std::numeric_limits<float>::infinity());
						break;
					}
					case STREAMER_AREA_TYPE_CUBOID:
					{
						std::get<Box3d>((*a)->attach->position).min_corner().fill(std::numeric_limits<float>::infinity());
						std::get<Box3d>((*a)->attach->position).max_corner().fill(std::numeric_limits<float>::infinity());
						break;
					}
					case STREAMER_AREA_TYPE_POLYGON:
					{
						std::get<Polygon2d>((*a)->attach->position).clear();
						break;
					}
				}
			}
		}
	}
}

void Streamer::processAttachedObjects()
{
	for (std::unordered_set<Item::SharedObject>::iterator o = attachedObjects.begin(); o != attachedObjects.end(); ++o)
	{
		if ((*o)->attach)
		{
			bool adjust = false;
			Eigen::Vector3f position = (*o)->attach->position;
			if ((*o)->attach->object != INVALID_STREAMER_ID)
			{
				std::unordered_map<int, Item::SharedObject>::iterator p = core->getData()->objects.find((*o)->attach->object);
				if (p != core->getData()->objects.end())
				{
					(*o)->attach->position = p->second->position;
					(*o)->attach->worlds = p->second->worlds;
					adjust = true;
				}
			}
			else if ((*o)->attach->player != INVALID_PLAYER_ID)
			{
				adjust = StreamerApi::GetPlayerPos((*o)->attach->player, &(*o)->attach->position[0], &(*o)->attach->position[1], &(*o)->attach->position[2]);
				Utility::setFirstValueInContainer((*o)->attach->worlds, StreamerApi::GetPlayerVirtualWorld((*o)->attach->player));
			}
			else if ((*o)->attach->vehicle != INVALID_VEHICLE_ID)
			{
				adjust = StreamerApi::GetVehiclePos((*o)->attach->vehicle, &(*o)->attach->position[0], &(*o)->attach->position[1], &(*o)->attach->position[2]);
				Utility::setFirstValueInContainer((*o)->attach->worlds, StreamerApi::GetVehicleVirtualWorld((*o)->attach->vehicle));
			}
			if (adjust)
			{
				if ((*o)->cell && !(*o)->attach->position.isApprox(position))
				{
					core->getGrid()->removeObject(*o, true);
				}
			}
			else
			{
				(*o)->attach->position.fill(std::numeric_limits<float>::infinity());
			}
		}
	}
}

void Streamer::processAttachedTextLabels()
{
	for (std::unordered_set<Item::SharedTextLabel>::iterator t = attachedTextLabels.begin(); t != attachedTextLabels.end(); ++t)
	{
		bool adjust = false;
		Eigen::Vector3f position = (*t)->attach->position;
		if ((*t)->attach)
		{
			if ((*t)->attach->player != INVALID_PLAYER_ID)
			{
				adjust = StreamerApi::GetPlayerPos((*t)->attach->player, &(*t)->attach->position[0], &(*t)->attach->position[1], &(*t)->attach->position[2]);
				Utility::setFirstValueInContainer((*t)->attach->worlds, StreamerApi::GetPlayerVirtualWorld((*t)->attach->player));
			}
			else if ((*t)->attach->vehicle != INVALID_VEHICLE_ID)
			{
				adjust = StreamerApi::GetVehiclePos((*t)->attach->vehicle, &(*t)->attach->position[0], &(*t)->attach->position[1], &(*t)->attach->position[2]);
				Utility::setFirstValueInContainer((*t)->attach->worlds, StreamerApi::GetVehicleVirtualWorld((*t)->attach->vehicle));
			}
			if (adjust)
			{
				if ((*t)->cell && !(*t)->attach->position.isApprox(position))
				{
					core->getGrid()->removeTextLabel(*t, true);
				}
			}
			else
			{
				(*t)->attach->position.fill(std::numeric_limits<float>::infinity());
			}
		}
	}
}
