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

#include "core.h"

namespace
{
	// Argument coercion to AMX cell. Floats go through amx_ftoc so they round-trip as Float:
	// in Pawn. Anything else castable to cell uses static_cast (ints, bools, enum values).
	inline cell toCell(float v) { return amx_ftoc(v); }
	inline cell toCell(double v) { float f = static_cast<float>(v); return amx_ftoc(f); }
	template <typename T>
	inline cell toCell(T v) { return static_cast<cell>(v); }

	// Push Pawn public arguments in reverse (rightmost first), matching the AMX calling
	// convention. Recursive so the left-to-right argument pack is consumed right-to-left.
	inline void pushArgsRev(AMX *) { }
	template <typename T, typename... Rest>
	inline void pushArgsRev(AMX *amx, T first, Rest... rest)
	{
		pushArgsRev(amx, rest...);
		amx_Push(amx, toCell(first));
	}

	// Fire-and-forget: dispatch the public to every registered AMX, ignore return values.
	template <typename... Args>
	void dispatchPublic(const char *name, Args... args)
	{
		for (AMX *amx : core->getData()->interfaces)
		{
			int idx = 0;
			if (core->getData()->findCachedPublic(amx, name, idx))
			{
				pushArgsRev(amx, args...);
				amx_Exec(amx, nullptr, idx);
			}
		}
	}

	// Break on first interface whose handler returns non-zero. Used where the streamer lets the
	// first script that claims the event stop the chain (e.g. edit/select dynamic object).
	template <typename... Args>
	void dispatchPublicBreakOnTruthy(const char *name, Args... args)
	{
		for (AMX *amx : core->getData()->interfaces)
		{
			int idx = 0;
			if (core->getData()->findCachedPublic(amx, name, idx))
			{
				pushArgsRev(amx, args...);
				cell ret = 0;
				amx_Exec(amx, &ret, idx);
				if (ret)
				{
					return;
				}
			}
		}
	}

	// Return false if any interface's handler returns zero; used for veto-style events like
	// OnPlayerShootDynamicObject.
	template <typename... Args>
	bool dispatchPublicRequireAll(const char *name, Args... args)
	{
		bool allow = true;
		for (AMX *amx : core->getData()->interfaces)
		{
			int idx = 0;
			if (core->getData()->findCachedPublic(amx, name, idx))
			{
				pushArgsRev(amx, args...);
				cell ret = 0;
				amx_Exec(amx, &ret, idx);
				if (!ret)
				{
					allow = false;
				}
			}
		}
		return allow;
	}
}

bool Streamer_OnPlayerConnect(int playerid)
{
	if (playerid >= 0 && playerid < MAX_PLAYERS)
	{
		auto &players = core->getData()->players;
		if (players.find(playerid) == players.end())
		{
			players.insert(std::make_pair(playerid, Player(playerid)));
		}
	}
	return true;
}

bool Streamer_OnPlayerDisconnect(int playerid, int /*reason*/)
{
	core->getData()->players.erase(playerid);
	return true;
}

bool Streamer_OnPlayerSpawn(int playerid)
{
	auto it = core->getData()->players.find(playerid);
	if (it != core->getData()->players.end())
	{
		it->second.requestingClass = false;
	}
	return true;
}

bool Streamer_OnPlayerRequestClass(int playerid, int /*classid*/)
{
	auto it = core->getData()->players.find(playerid);
	if (it != core->getData()->players.end())
	{
		it->second.requestingClass = true;
	}
	return true;
}

bool Streamer_OnPlayerEnterCheckpoint(int playerid)
{
	auto it = core->getData()->players.find(playerid);
	if (it == core->getData()->players.end()) return true;
	Player &player = it->second;
	if (player.activeCheckpoint == player.visibleCheckpoint) return true;

	int checkpointid = player.visibleCheckpoint;
	player.activeCheckpoint = checkpointid;
	dispatchPublic("OnPlayerEnterDynamicCP", playerid, checkpointid);
	return true;
}

bool Streamer_OnPlayerLeaveCheckpoint(int playerid)
{
	auto it = core->getData()->players.find(playerid);
	if (it == core->getData()->players.end()) return true;
	Player &player = it->second;
	if (player.activeCheckpoint != player.visibleCheckpoint) return true;

	int checkpointid = player.activeCheckpoint;
	player.activeCheckpoint = 0;
	dispatchPublic("OnPlayerLeaveDynamicCP", playerid, checkpointid);
	return true;
}

bool Streamer_OnPlayerEnterRaceCheckpoint(int playerid)
{
	auto it = core->getData()->players.find(playerid);
	if (it == core->getData()->players.end()) return true;
	Player &player = it->second;
	if (player.activeRaceCheckpoint == player.visibleRaceCheckpoint) return true;

	int checkpointid = player.visibleRaceCheckpoint;
	player.activeRaceCheckpoint = checkpointid;
	dispatchPublic("OnPlayerEnterDynamicRaceCP", playerid, checkpointid);
	return true;
}

bool Streamer_OnPlayerLeaveRaceCheckpoint(int playerid)
{
	auto it = core->getData()->players.find(playerid);
	if (it == core->getData()->players.end()) return true;
	Player &player = it->second;
	if (player.activeRaceCheckpoint != player.visibleRaceCheckpoint) return true;

	int checkpointid = player.activeRaceCheckpoint;
	player.activeRaceCheckpoint = 0;
	dispatchPublic("OnPlayerLeaveDynamicRaceCP", playerid, checkpointid);
	return true;
}

bool Streamer_OnPlayerPickUpPickup(int playerid, int pickupid)
{
	for (const auto &entry : core->getData()->internalPickups)
	{
		if (entry.second == pickupid)
		{
			dispatchPublic("OnPlayerPickUpDynamicPickup", playerid, entry.first.first);
			break;
		}
	}
	return true;
}

bool Streamer_OnPlayerEditObject(int playerid, bool playerobject, int objectid, int response, float fX, float fY, float fZ, float fRotX, float fRotY, float fRotZ)
{
	if (!playerobject) return false;

	auto playerIt = core->getData()->players.find(playerid);
	if (playerIt == core->getData()->players.end()) return false;

	for (const auto &internal : playerIt->second.internalObjects)
	{
		if (internal.second != objectid) continue;

		int dynObjectId = internal.first;
		if (response == EDIT_RESPONSE_CANCEL || response == EDIT_RESPONSE_FINAL)
		{
			auto objIt = core->getData()->objects.find(dynObjectId);
			if (objIt != core->getData()->objects.end())
			{
				auto &obj = objIt->second;
				if (obj->comparableStreamDistance < STREAMER_STATIC_DISTANCE_CUTOFF
					&& obj->originalComparableStreamDistance > STREAMER_STATIC_DISTANCE_CUTOFF)
				{
					obj->comparableStreamDistance = obj->originalComparableStreamDistance;
					obj->originalComparableStreamDistance = -1.0f;
				}
			}
		}
		dispatchPublicBreakOnTruthy("OnPlayerEditDynamicObject",
			playerid, dynObjectId, response, fX, fY, fZ, fRotX, fRotY, fRotZ);
		return true;
	}
	return false;
}

bool Streamer_OnPlayerSelectObject(int playerid, int type, int objectid, int modelid, float x, float y, float z)
{
	if (type != SELECT_OBJECT_PLAYER_OBJECT) return false;

	auto playerIt = core->getData()->players.find(playerid);
	if (playerIt == core->getData()->players.end()) return false;

	for (const auto &internal : playerIt->second.internalObjects)
	{
		if (internal.second != objectid) continue;
		dispatchPublicBreakOnTruthy("OnPlayerSelectDynamicObject",
			playerid, internal.first, modelid, x, y, z);
		return true;
	}
	return false;
}

bool Streamer_OnPlayerWeaponShot(int playerid, int weaponid, int hittype, int hitid, float x, float y, float z)
{
	if (hittype != BULLET_HIT_TYPE_PLAYER_OBJECT) return true;

	auto playerIt = core->getData()->players.find(playerid);
	if (playerIt == core->getData()->players.end()) return true;

	for (const auto &internal : playerIt->second.internalObjects)
	{
		if (internal.second != hitid) continue;
		return dispatchPublicRequireAll("OnPlayerShootDynamicObject",
			playerid, weaponid, internal.first, x, y, z);
	}
	return true;
}

bool Streamer_OnPlayerGiveDamageActor(int playerid, int actorid, float amount, int weaponid, int bodypart)
{
	for (const auto &entry : core->getData()->internalActors)
	{
		if (entry.second != actorid) continue;
		dispatchPublicBreakOnTruthy("OnPlayerGiveDamageDynamicActor",
			playerid, entry.first.first, amount, weaponid, bodypart);
		return true;
	}
	return false;
}

bool Streamer_OnActorStreamIn(int actorid, int forplayerid)
{
	for (const auto &entry : core->getData()->internalActors)
	{
		if (entry.second == actorid)
		{
			dispatchPublic("OnDynamicActorStreamIn", entry.first.first, forplayerid);
			break;
		}
	}
	return true;
}

bool Streamer_OnActorStreamOut(int actorid, int forplayerid)
{
	for (const auto &entry : core->getData()->internalActors)
	{
		if (entry.second == actorid)
		{
			dispatchPublic("OnDynamicActorStreamOut", entry.first.first, forplayerid);
			break;
		}
	}
	return true;
}
