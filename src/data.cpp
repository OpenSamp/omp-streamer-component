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

#include "data.h"

Data::Data()
{
	errorCallbackEnabled = false;
	globalChunkTickRate[STREAMER_TYPE_OBJECT] = 1;
	globalChunkTickRate[STREAMER_TYPE_MAP_ICON] = 1;
	globalChunkTickRate[STREAMER_TYPE_3D_TEXT_LABEL] = 1;
	globalMaxItems[STREAMER_TYPE_OBJECT] = std::numeric_limits<std::size_t>::max();
	globalMaxItems[STREAMER_TYPE_PICKUP] = std::numeric_limits<std::size_t>::max();
	globalMaxItems[STREAMER_TYPE_CP] = std::numeric_limits<std::size_t>::max();
	globalMaxItems[STREAMER_TYPE_RACE_CP] = std::numeric_limits<std::size_t>::max();
	globalMaxItems[STREAMER_TYPE_MAP_ICON] = std::numeric_limits<std::size_t>::max();
	globalMaxItems[STREAMER_TYPE_3D_TEXT_LABEL] = std::numeric_limits<std::size_t>::max();
	globalMaxItems[STREAMER_TYPE_AREA] = std::numeric_limits<std::size_t>::max();
	globalMaxItems[STREAMER_TYPE_ACTOR] = std::numeric_limits<std::size_t>::max();
	// Per-player visible cap — bumped from the legacy SA-MP default of 500 to match the full
	// 0.3.7/open.mp per-player object pool. Below this, the streamer silently drops the farthest
	// items so the effective stream radius looks smaller than what Streamer_SetCellDistance /
	// per-item streamDistance suggested.
	// Must stay BELOW the client's per-player object pool (OBJECT_POOL_SIZE_037 = 1000 for a
	// 0.3.7 client, 2000 for an open.mp client), with headroom for a player's other objects
	// (attached accessories etc.). Setting it == the 0.3.7 pool (1000) made the streamer fill
	// the pool in dense areas, CreatePlayerObject failed at the boundary and it thrashed
	// create/destroy (~1000 RPCs/s -> tripped acks_limit). BUT too low (500) under-streams dense
	// RP zones: >500 objects in range means only the 500 closest show, so landmarks pop in only
	// up close. 850 leaves ~150 headroom below the 1000 pool while covering ~70% more objects.
	// (Proper fix for large landmarks is a bigger per-object streamDistance/priority in map data.)
	globalMaxVisibleItems[STREAMER_TYPE_OBJECT] = 850;
	globalMaxVisibleItems[STREAMER_TYPE_PICKUP] = 4096;
	globalMaxVisibleItems[STREAMER_TYPE_MAP_ICON] = 100;
	globalMaxVisibleItems[STREAMER_TYPE_3D_TEXT_LABEL] = 1024;
	globalMaxVisibleItems[STREAMER_TYPE_ACTOR] = 1000;
	globalRadiusMultipliers[STREAMER_TYPE_OBJECT] = 1.0f;
	globalRadiusMultipliers[STREAMER_TYPE_PICKUP] = 1.0f;
	globalRadiusMultipliers[STREAMER_TYPE_CP] = 1.0f;
	globalRadiusMultipliers[STREAMER_TYPE_RACE_CP] = 1.0f;
	globalRadiusMultipliers[STREAMER_TYPE_MAP_ICON] = 1.0f;
	globalRadiusMultipliers[STREAMER_TYPE_3D_TEXT_LABEL] = 1.0f;
	globalRadiusMultipliers[STREAMER_TYPE_AREA] = 1.0f;
	globalRadiusMultipliers[STREAMER_TYPE_ACTOR] = 1.0f;
	static const int defaultTypePriority[] =
	{
		STREAMER_TYPE_AREA,
		STREAMER_TYPE_OBJECT,
		STREAMER_TYPE_CP,
		STREAMER_TYPE_RACE_CP,
		STREAMER_TYPE_MAP_ICON,
		STREAMER_TYPE_3D_TEXT_LABEL,
		STREAMER_TYPE_PICKUP,
		STREAMER_TYPE_ACTOR
	};
	for (std::size_t i = 0; i < sizeof(defaultTypePriority) / sizeof(const int); ++i)
	{
		typePriority.push_back(defaultTypePriority[i]);
	}
}

std::size_t Data::getGlobalChunkTickRate(int type)
{
	if (type >= 0 && type < STREAMER_MAX_TYPES)
	{
		return globalChunkTickRate[type];
	}
	return 0;
}

bool Data::setGlobalChunkTickRate(int type, std::size_t value)
{
	if (type >= 0 && type < STREAMER_MAX_TYPES)
	{
		globalChunkTickRate[type] = value;
		return true;
	}
	return false;
}

std::size_t Data::getGlobalMaxItems(int type)
{
	if (type >= 0 && type < STREAMER_MAX_TYPES)
	{
		return globalMaxItems[type];
	}
	return 0;
}

bool Data::setGlobalMaxItems(int type, std::size_t value)
{
	if (type >= 0 && type < STREAMER_MAX_TYPES)
	{
		globalMaxItems[type] = value;
		return true;
	}
	return false;
}

std::size_t Data::getGlobalMaxVisibleItems(int type)
{
	switch (type)
	{
		case STREAMER_TYPE_OBJECT:
		{
			return globalMaxVisibleItems[STREAMER_TYPE_OBJECT];
		}
		case STREAMER_TYPE_PICKUP:
		{
			return globalMaxVisibleItems[STREAMER_TYPE_PICKUP];
		}
		case STREAMER_TYPE_MAP_ICON:
		{
			return globalMaxVisibleItems[STREAMER_TYPE_MAP_ICON];
		}
		case STREAMER_TYPE_3D_TEXT_LABEL:
		{
			return globalMaxVisibleItems[STREAMER_TYPE_3D_TEXT_LABEL];
		}
		case STREAMER_TYPE_ACTOR:
		{
			return globalMaxVisibleItems[STREAMER_TYPE_ACTOR];
		}
	}
	return 0;
}

bool Data::setGlobalMaxVisibleItems(int type, std::size_t value)
{
	switch (type)
	{
		case STREAMER_TYPE_OBJECT:
		{
			globalMaxVisibleItems[STREAMER_TYPE_OBJECT] = value;
			return true;
		}
		case STREAMER_TYPE_PICKUP:
		{
			globalMaxVisibleItems[STREAMER_TYPE_PICKUP] = value;
			return true;
		}
		case STREAMER_TYPE_MAP_ICON:
		{
			globalMaxVisibleItems[STREAMER_TYPE_MAP_ICON] = value;
			return true;
		}
		case STREAMER_TYPE_3D_TEXT_LABEL:
		{
			globalMaxVisibleItems[STREAMER_TYPE_3D_TEXT_LABEL] = value;
			return true;
		}
		case STREAMER_TYPE_ACTOR:
		{
			globalMaxVisibleItems[STREAMER_TYPE_ACTOR] = value;
			return true;
		}
	}
	return false;
}

float Data::getGlobalRadiusMultiplier(int type)
{
	if (type >= 0 && type < STREAMER_MAX_TYPES)
	{
		return globalRadiusMultipliers[type];
	}
	return 0;
}

bool Data::setGlobalRadiusMultiplier(int type, float value)
{
	if (type >= 0 && type < STREAMER_MAX_TYPES)
	{
		globalRadiusMultipliers[type] = value;
		return true;
	}
	return false;
}
