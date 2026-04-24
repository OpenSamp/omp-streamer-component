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

cell AMX_NATIVE_CALL Natives::CreateDynamicPickup(AMX *amx, cell *params)
{
	CHECK_PARAMS(11);
	const int modelId = static_cast<int>(params[1]);
	const int pickupType = static_cast<int>(params[2]);
	const float px = amx_ctof(params[3]), py = amx_ctof(params[4]), pz = amx_ctof(params[5]);
	const float streamDist = amx_ctof(params[9]);
	CHECK_MODEL_ID(modelId);
	CHECK_POS_VEC3(px, py, pz);
	CHECK_STREAM_DISTANCE(streamDist);
	if (pickupType < 0 || pickupType > 22)
	{
		Utility::logError("CreateDynamicPickup: pickup type %d out of range [0, 22].", pickupType);
		return 0;
	}
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_PICKUP) == core->getData()->pickups.size())
	{
		return INVALID_STREAMER_ID;
	}
	int pickupId = Item::Pickup::identifier.get();
	Item::SharedPickup pickup = std::make_shared<Item::Pickup>();
	pickup->amx = amx;
	pickup->pickupId = pickupId;
	pickup->inverseAreaChecking = false;
	pickup->originalComparableStreamDistance = -1.0f;
	pickup->positionOffset = Eigen::Vector3f::Zero();
	pickup->streamCallbacks = false;
	pickup->modelId = modelId;
	pickup->type = pickupType;
	pickup->position = Eigen::Vector3f(px, py, pz);
	Utility::addToContainer(pickup->worlds, static_cast<int>(params[6]));
	if (pickup->worlds.empty())
	{
		pickup->worlds.insert(-1);
	}
	Utility::addToContainer(pickup->interiors, static_cast<int>(params[7]));
	Utility::addToContainer(pickup->players, static_cast<int>(params[8]));
	pickup->comparableStreamDistance = amx_ctof(params[9]) < STREAMER_STATIC_DISTANCE_CUTOFF ? amx_ctof(params[9]) : amx_ctof(params[9]) * amx_ctof(params[9]);
	pickup->streamDistance = amx_ctof(params[9]);
	Utility::addToContainer(pickup->areas, static_cast<int>(params[10]));
	pickup->priority = static_cast<int>(params[11]);
	core->getGrid()->addPickup(pickup);
	core->getData()->pickups.insert(std::make_pair(pickupId, pickup));
	return static_cast<cell>(pickupId);
}

cell AMX_NATIVE_CALL Natives::DestroyDynamicPickup(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	std::unordered_map<int, Item::SharedPickup>::iterator p = core->getData()->pickups.find(static_cast<int>(params[1]));
	if (p != core->getData()->pickups.end())
	{
		Utility::destroyPickup(p);
		return 1;
	}
	return 0;
}

cell AMX_NATIVE_CALL Natives::IsValidDynamicPickup(AMX *amx, cell *params)
{
	CHECK_PARAMS(1);
	std::unordered_map<int, Item::SharedPickup>::iterator p = core->getData()->pickups.find(static_cast<int>(params[1]));
	if (p != core->getData()->pickups.end())
	{
		return 1;
	}
	return 0;
}
