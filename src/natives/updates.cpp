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

#include "../main.h"

#include "../natives.h"
#include "../core.h"

cell AMX_NATIVE_CALL Natives::Streamer_ProcessActiveItems(AMX *amx, cell *params)
{
	core->getStreamer()->processActiveItems();
	return 1;
}

cell AMX_NATIVE_CALL Natives::Streamer_ToggleIdleUpdate(AMX *amx, cell *params)
{
	CHECK_PARAMS(2);
	std::unordered_map<int, Player>::iterator p = core->getData()->players.find(static_cast<int>(params[1]));
	if (p != core->getData()->players.end())
	{
		p->second.updateWhenIdle = static_cast<int>(params[2]) != 0;
		return 1;
	}
	return 0;
}

cell AMX_NATIVE_CALL Natives::Streamer_IsToggleIdleUpdate(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	std::unordered_map<int, Player>::iterator p = core->getData()->players.find(static_cast<int>(params[1]));
	if (p != core->getData()->players.end())
	{
		return static_cast<cell>(p->second.updateWhenIdle != 0);
	}
	return 0;
}

cell AMX_NATIVE_CALL Natives::Streamer_ToggleCameraUpdate(AMX *amx, cell *params)
{
	CHECK_PARAMS(2);
	std::unordered_map<int, Player>::iterator p = core->getData()->players.find(static_cast<int>(params[1]));
	if (p != core->getData()->players.end())
	{
		p->second.updateUsingCameraPosition = static_cast<int>(params[2]) != 0;
		return 1;
	}
	return 0;
}

cell AMX_NATIVE_CALL Natives::Streamer_IsToggleCameraUpdate(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	std::unordered_map<int, Player>::iterator p = core->getData()->players.find(static_cast<int>(params[1]));
	if (p != core->getData()->players.end())
	{
		return static_cast<cell>(p->second.updateUsingCameraPosition != 0);
	}
	return 0;
}

cell AMX_NATIVE_CALL Natives::Streamer_ToggleItemUpdate(AMX *amx, cell *params)
{
	CHECK_PARAMS(3);
	std::unordered_map<int, Player>::iterator p = core->getData()->players.find(static_cast<int>(params[1]));
	if (p != core->getData()->players.end())
	{
		if (static_cast<int>(params[2]) >= 0 && static_cast<int>(params[2]) < STREAMER_MAX_TYPES)
		{
			p->second.enabledItems.set(static_cast<size_t>(params[2]), params[3] != 0);
			return 1;
		}
	}
	return 0;
}

cell AMX_NATIVE_CALL Natives::Streamer_IsToggleItemUpdate(AMX *amx, cell *params)
{
	CHECK_PARAMS(2);
	std::unordered_map<int, Player>::iterator p = core->getData()->players.find(static_cast<int>(params[1]));
	if (p != core->getData()->players.end())
	{
		if (static_cast<int>(params[2]) >= 0 && static_cast<int>(params[2]) < STREAMER_MAX_TYPES)
		{
			return static_cast<cell>(p->second.enabledItems.test(params[2]) != 0);
		}
	}
	return 0;
}

cell AMX_NATIVE_CALL Natives::Streamer_GetLastUpdateTime(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	Utility::storeFloatInNative(amx, params[1], core->getStreamer()->getLastUpdateTime());
	return 1;
}

cell AMX_NATIVE_CALL Natives::Streamer_Update(AMX *amx, cell *params)
{
	CHECK_PARAMS(2);
	std::unordered_map<int, Player>::iterator p = core->getData()->players.find(static_cast<int>(params[1]));
	if (p != core->getData()->players.end())
	{
		p->second.interiorId = StreamerApi::GetPlayerInterior(p->first);
		p->second.worldId = StreamerApi::GetPlayerVirtualWorld(p->first);
		StreamerApi::GetPlayerPos(p->first, &p->second.position[0], &p->second.position[1], &p->second.position[2]);
		core->getStreamer()->startManualUpdate(p->second, static_cast<int>(params[2]));
		return 1;
	}
	return 0;
}

cell AMX_NATIVE_CALL Natives::Streamer_UpdateEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(9);
	std::unordered_map<int, Player>::iterator p = core->getData()->players.find(static_cast<int>(params[1]));
	if (p != core->getData()->players.end())
	{
		p->second.position = Eigen::Vector3f(amx_ctof(params[2]), amx_ctof(params[3]), amx_ctof(params[4]));
		if (static_cast<int>(params[5]) >= 0)
		{
			p->second.worldId = static_cast<int>(params[5]);
		}
		else
		{
			p->second.worldId = StreamerApi::GetPlayerVirtualWorld(p->first);
		}
		if (static_cast<int>(params[6]) >= 0)
		{
			p->second.interiorId = static_cast<int>(params[6]);
		}
		else
		{
			p->second.interiorId = StreamerApi::GetPlayerInterior(p->first);
		}
		if (static_cast<int>(params[8]) >= 0)
		{
			StreamerApi::SetPlayerPos(p->first, p->second.position[0], p->second.position[1], p->second.position[2]);
			if (static_cast<int>(params[9]))
			{
				StreamerApi::TogglePlayerControllable(p->first, false);
			}
			p->second.delayedUpdate = true;
			p->second.delayedUpdateType = static_cast<int>(params[7]);
			p->second.delayedUpdateTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(params[8]));
			p->second.delayedUpdateFreeze = static_cast<int>(params[9]) != 0;
		}
		core->getStreamer()->startManualUpdate(p->second, static_cast<int>(params[7]));
		return 1;
	}
	return 0;
}

// --- Phase telemetry (VS:RP fork) ---------------------------------------------------

cell AMX_NATIVE_CALL Natives::Streamer_GetPhaseTimeNs(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	const int type = static_cast<int>(params[1]);
	if (type < 0 || type >= STREAMER_MAX_TYPES)
	{
		Utility::logError("Streamer_GetPhaseTimeNs: invalid type %d.", type);
		return 0;
	}
	// AMX cell is int32; clamp to avoid wrap. Callers should reset often.
	const uint64_t v = core->getStreamer()->phaseTimeNs[type];
	if (v > static_cast<uint64_t>(std::numeric_limits<cell>::max()))
	{
		return std::numeric_limits<cell>::max();
	}
	return static_cast<cell>(v);
}

cell AMX_NATIVE_CALL Natives::Streamer_GetPhaseAvgUs(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	const int type = static_cast<int>(params[1]);
	if (type < 0 || type >= STREAMER_MAX_TYPES)
	{
		Utility::logError("Streamer_GetPhaseAvgUs: invalid type %d.", type);
		return 0;
	}
	const auto ticks = core->getStreamer()->phaseTickCount;
	if (ticks == 0)
	{
		return 0;
	}
	const uint64_t totalNs = core->getStreamer()->phaseTimeNs[type];
	const uint64_t avgUs = (totalNs / ticks) / 1000ULL;
	if (avgUs > static_cast<uint64_t>(std::numeric_limits<cell>::max()))
	{
		return std::numeric_limits<cell>::max();
	}
	return static_cast<cell>(avgUs);
}

cell AMX_NATIVE_CALL Natives::Streamer_GetPhaseTickCount(AMX *amx, cell *params)
{
	(void)amx; (void)params;
	const auto ticks = core->getStreamer()->phaseTickCount;
	if (ticks > static_cast<std::size_t>(std::numeric_limits<cell>::max()))
	{
		return std::numeric_limits<cell>::max();
	}
	return static_cast<cell>(ticks);
}

cell AMX_NATIVE_CALL Natives::Streamer_ResetPhaseStats(AMX *amx, cell *params)
{
	(void)amx; (void)params;
	core->getStreamer()->resetStats();
	return 1;
}

cell AMX_NATIVE_CALL Natives::Streamer_GetPhaseStreamInCount(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	const int type = static_cast<int>(params[1]);
	if (type < 0 || type >= STREAMER_MAX_TYPES)
	{
		Utility::logError("Streamer_GetPhaseStreamInCount: invalid type %d.", type);
		return 0;
	}
	const uint64_t v = core->getStreamer()->phaseStreamInCount[type];
	if (v > static_cast<uint64_t>(std::numeric_limits<cell>::max()))
	{
		return std::numeric_limits<cell>::max();
	}
	return static_cast<cell>(v);
}

cell AMX_NATIVE_CALL Natives::Streamer_GetPhaseStreamOutCount(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	const int type = static_cast<int>(params[1]);
	if (type < 0 || type >= STREAMER_MAX_TYPES)
	{
		Utility::logError("Streamer_GetPhaseStreamOutCount: invalid type %d.", type);
		return 0;
	}
	const uint64_t v = core->getStreamer()->phaseStreamOutCount[type];
	if (v > static_cast<uint64_t>(std::numeric_limits<cell>::max()))
	{
		return std::numeric_limits<cell>::max();
	}
	return static_cast<cell>(v);
}

cell AMX_NATIVE_CALL Natives::Streamer_GetHysteresisFactor(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	const int type = static_cast<int>(params[1]);
	float v = core->getStreamer()->getHysteresisFactor(type);
	return amx_ftoc(v);
}

cell AMX_NATIVE_CALL Natives::Streamer_SetHysteresisFactor(AMX *amx, cell *params)
{
	CHECK_PARAMS(2);
	const int type = static_cast<int>(params[1]);
	const float value = amx_ctof(params[2]);
	if (!core->getStreamer()->setHysteresisFactor(type, value))
	{
		Utility::logError("Streamer_SetHysteresisFactor: invalid type %d or factor %f (must be in [1.0, 10.0]).", type, value);
		return 0;
	}
	return 1;
}
