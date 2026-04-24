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
#include "../utility.h"

cell AMX_NATIVE_CALL Natives::CreateDynamicCP(AMX *amx, cell *params)
{
	CHECK_PARAMS(10);
	const float px = amx_ctof(params[1]), py = amx_ctof(params[2]), pz = amx_ctof(params[3]);
	const float size = amx_ctof(params[4]);
	const float streamDist = amx_ctof(params[8]);
	CHECK_POS_VEC3(px, py, pz);
	CHECK_RADIUS(size);
	CHECK_STREAM_DISTANCE(streamDist);
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_CP) == core->getData()->checkpoints.size())
	{
		return INVALID_STREAMER_ID;
	}
	int checkpointId = Item::Checkpoint::identifier.get();
	Item::SharedCheckpoint checkpoint = std::make_shared<Item::Checkpoint>();
	checkpoint->amx = amx;
	checkpoint->checkpointId = checkpointId;
	checkpoint->inverseAreaChecking = false;
	checkpoint->originalComparableStreamDistance = -1.0f;
	checkpoint->positionOffset = Eigen::Vector3f::Zero();
	checkpoint->streamCallbacks = false;
	checkpoint->position = Eigen::Vector3f(px, py, pz);
	checkpoint->size = size;
	Utility::addToContainer(checkpoint->worlds, static_cast<int>(params[5]));
	Utility::addToContainer(checkpoint->interiors, static_cast<int>(params[6]));
	Utility::addToContainer(checkpoint->players, static_cast<int>(params[7]));
	checkpoint->comparableStreamDistance = streamDist < STREAMER_STATIC_DISTANCE_CUTOFF ? streamDist : streamDist * streamDist;
	checkpoint->streamDistance = streamDist;
	Utility::addToContainer(checkpoint->areas, static_cast<int>(params[9]));
	checkpoint->priority = static_cast<int>(params[10]);
	core->getGrid()->addCheckpoint(checkpoint);
	core->getData()->checkpoints.insert(std::make_pair(checkpointId, checkpoint));
	return static_cast<cell>(checkpointId);
}

cell AMX_NATIVE_CALL Natives::DestroyDynamicCP(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	std::unordered_map<int, Item::SharedCheckpoint>::iterator c = core->getData()->checkpoints.find(static_cast<int>(params[1]));
	if (c != core->getData()->checkpoints.end())
	{
		Utility::destroyCheckpoint(c);
		return 1;
	}
	return 0;
}

cell AMX_NATIVE_CALL Natives::IsValidDynamicCP(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	std::unordered_map<int, Item::SharedCheckpoint>::iterator c = core->getData()->checkpoints.find(static_cast<int>(params[1]));
	if (c != core->getData()->checkpoints.end())
	{
		return 1;
	}
	return 0;
}

cell AMX_NATIVE_CALL Natives::IsPlayerInDynamicCP(AMX *amx, cell *params)
{
	CHECK_PARAMS(2);
	std::unordered_map<int, Player>::iterator p = core->getData()->players.find(static_cast<int>(params[1]));
	if (p != core->getData()->players.end())
	{
		if (p->second.activeCheckpoint == static_cast<int>(params[2]))
		{
			return 1;
		}
	}
	return 0;
}

cell AMX_NATIVE_CALL Natives::GetPlayerVisibleDynamicCP(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	std::unordered_map<int, Player>::iterator p = core->getData()->players.find(static_cast<int>(params[1]));
	if (p != core->getData()->players.end())
	{
		return p->second.visibleCheckpoint;
	}
	return 0;
}
