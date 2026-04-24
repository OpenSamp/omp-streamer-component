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


cell AMX_NATIVE_CALL Natives::CreateDynamicObjectEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(18);
	const int modelId = static_cast<int>(params[1]);
	const float px = amx_ctof(params[2]), py = amx_ctof(params[3]), pz = amx_ctof(params[4]);
	const float rx = amx_ctof(params[5]), ry = amx_ctof(params[6]), rz = amx_ctof(params[7]);
	const float streamDist = amx_ctof(params[8]);
	const float drawDist = amx_ctof(params[9]);
	CHECK_MODEL_ID(modelId);
	CHECK_POS_VEC3(px, py, pz);
	CHECK_ROT_VEC3(rx, ry, rz);
	CHECK_STREAM_DISTANCE(streamDist);
	CHECK_DRAW_DISTANCE(drawDist);
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_OBJECT) == core->getData()->objects.size())
	{
		return INVALID_STREAMER_ID;
	}
	int objectId = Item::Object::identifier.get();
	Item::SharedObject object = std::make_shared<Item::Object>();
	object->amx = amx;
	object->objectId = objectId;
	object->inverseAreaChecking = false;
	object->noCameraCollision = false;
	object->originalComparableStreamDistance = -1.0f;
	object->positionOffset = Eigen::Vector3f::Zero();
	object->streamCallbacks = false;
	object->modelId = modelId;
	object->position = Eigen::Vector3f(px, py, pz);
	object->rotation = Eigen::Vector3f(rx, ry, rz);
	object->comparableStreamDistance = streamDist < STREAMER_STATIC_DISTANCE_CUTOFF ? streamDist : streamDist * streamDist;
	object->streamDistance = streamDist;
	object->drawDistance = drawDist;
	Utility::convertArrayToContainer(amx, params[10], params[15], object->worlds);
	Utility::convertArrayToContainer(amx, params[11], params[16], object->interiors);
	Utility::convertArrayToContainer(amx, params[12], params[17], object->players);
	Utility::convertArrayToContainer(amx, params[13], params[18], object->areas);
	object->priority = static_cast<int>(params[14]);
	core->getGrid()->addObject(object);
	core->getData()->objects.insert(std::make_pair(objectId, object));
	return static_cast<cell>(objectId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamicPickupEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(15);
	const int modelId = static_cast<int>(params[1]);
	const int pickupType = static_cast<int>(params[2]);
	const float px = amx_ctof(params[3]), py = amx_ctof(params[4]), pz = amx_ctof(params[5]);
	const float streamDist = amx_ctof(params[6]);
	CHECK_MODEL_ID(modelId);
	CHECK_POS_VEC3(px, py, pz);
	CHECK_STREAM_DISTANCE(streamDist);
	if (pickupType < 0 || pickupType > 22)
	{
		Utility::logError("CreateDynamicPickupEx: pickup type %d out of range [0, 22].", pickupType);
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
	pickup->comparableStreamDistance = streamDist < STREAMER_STATIC_DISTANCE_CUTOFF ? streamDist : streamDist * streamDist;
	pickup->streamDistance = streamDist;
	Utility::convertArrayToContainer(amx, params[7], params[12], pickup->worlds);
	Utility::convertArrayToContainer(amx, params[8], params[13], pickup->interiors);
	Utility::convertArrayToContainer(amx, params[9], params[14], pickup->players);
	Utility::convertArrayToContainer(amx, params[10], params[15], pickup->areas);
	pickup->priority = static_cast<int>(params[11]);
	core->getGrid()->addPickup(pickup);
	core->getData()->pickups.insert(std::make_pair(pickupId, pickup));
	return static_cast<cell>(pickupId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamicCPEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(14);
	const float px = amx_ctof(params[1]), py = amx_ctof(params[2]), pz = amx_ctof(params[3]);
	const float size = amx_ctof(params[4]);
	const float streamDist = amx_ctof(params[5]);
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
	checkpoint->comparableStreamDistance = streamDist < STREAMER_STATIC_DISTANCE_CUTOFF ? streamDist : streamDist * streamDist;
	checkpoint->streamDistance = streamDist;
	Utility::convertArrayToContainer(amx, params[6], params[11], checkpoint->worlds);
	Utility::convertArrayToContainer(amx, params[7], params[12], checkpoint->interiors);
	Utility::convertArrayToContainer(amx, params[8], params[13], checkpoint->players);
	Utility::convertArrayToContainer(amx, params[9], params[14], checkpoint->areas);
	checkpoint->priority = static_cast<int>(params[10]);
	core->getGrid()->addCheckpoint(checkpoint);
	core->getData()->checkpoints.insert(std::make_pair(checkpointId, checkpoint));
	return static_cast<cell>(checkpointId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamicRaceCPEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(18);
	const int raceType = static_cast<int>(params[1]);
	const float px = amx_ctof(params[2]), py = amx_ctof(params[3]), pz = amx_ctof(params[4]);
	const float nx = amx_ctof(params[5]), ny = amx_ctof(params[6]), nz = amx_ctof(params[7]);
	const float size = amx_ctof(params[8]);
	const float streamDist = amx_ctof(params[9]);
	CHECK_POS_VEC3(px, py, pz);
	CHECK_POS_VEC3(nx, ny, nz);
	CHECK_RADIUS(size);
	CHECK_STREAM_DISTANCE(streamDist);
	if (raceType < 0 || raceType > 8)
	{
		Utility::logError("CreateDynamicRaceCPEx: race checkpoint type %d out of range [0, 8].", raceType);
		return 0;
	}
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_RACE_CP) == core->getData()->raceCheckpoints.size())
	{
		return INVALID_STREAMER_ID;
	}
	int raceCheckpointId = Item::RaceCheckpoint::identifier.get();
	Item::SharedRaceCheckpoint raceCheckpoint = std::make_shared<Item::RaceCheckpoint>();
	raceCheckpoint->amx = amx;
	raceCheckpoint->raceCheckpointId = raceCheckpointId;
	raceCheckpoint->inverseAreaChecking = false;
	raceCheckpoint->originalComparableStreamDistance = -1.0f;
	raceCheckpoint->positionOffset = Eigen::Vector3f::Zero();
	raceCheckpoint->streamCallbacks = false;
	raceCheckpoint->type = raceType;
	raceCheckpoint->position = Eigen::Vector3f(px, py, pz);
	raceCheckpoint->next = Eigen::Vector3f(nx, ny, nz);
	raceCheckpoint->size = size;
	raceCheckpoint->comparableStreamDistance = streamDist < STREAMER_STATIC_DISTANCE_CUTOFF ? streamDist : streamDist * streamDist;
	raceCheckpoint->streamDistance = streamDist;
	Utility::convertArrayToContainer(amx, params[10], params[15], raceCheckpoint->worlds);
	Utility::convertArrayToContainer(amx, params[11], params[16], raceCheckpoint->interiors);
	Utility::convertArrayToContainer(amx, params[12], params[17], raceCheckpoint->players);
	Utility::convertArrayToContainer(amx, params[13], params[18], raceCheckpoint->areas);
	raceCheckpoint->priority = static_cast<int>(params[14]);
	core->getGrid()->addRaceCheckpoint(raceCheckpoint);
	core->getData()->raceCheckpoints.insert(std::make_pair(raceCheckpointId, raceCheckpoint));
	return static_cast<cell>(raceCheckpointId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamicMapIconEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(16);
	const float px = amx_ctof(params[1]), py = amx_ctof(params[2]), pz = amx_ctof(params[3]);
	const int iconType = static_cast<int>(params[4]);
	const float streamDist = amx_ctof(params[7]);
	CHECK_POS_VEC3(px, py, pz);
	CHECK_STREAM_DISTANCE(streamDist);
	if (iconType < 0 || iconType > 255)
	{
		Utility::logError("CreateDynamicMapIconEx: icon type %d out of range [0, 255].", iconType);
		return 0;
	}
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_MAP_ICON) == core->getData()->mapIcons.size())
	{
		return INVALID_STREAMER_ID;
	}
	int mapIconId = Item::MapIcon::identifier.get();
	Item::SharedMapIcon mapIcon = std::make_shared<Item::MapIcon>();
	mapIcon->amx = amx;
	mapIcon->mapIconId = mapIconId;
	mapIcon->inverseAreaChecking = false;
	mapIcon->originalComparableStreamDistance = -1.0f;
	mapIcon->positionOffset = Eigen::Vector3f::Zero();
	mapIcon->streamCallbacks = false;
	mapIcon->position = Eigen::Vector3f(px, py, pz);
	mapIcon->type = iconType;
	mapIcon->color = static_cast<int>(params[5]);
	mapIcon->style = static_cast<int>(params[6]);
	mapIcon->comparableStreamDistance = streamDist < STREAMER_STATIC_DISTANCE_CUTOFF ? streamDist : streamDist * streamDist;
	mapIcon->streamDistance = streamDist;
	Utility::convertArrayToContainer(amx, params[8], params[13], mapIcon->worlds);
	Utility::convertArrayToContainer(amx, params[9], params[14], mapIcon->interiors);
	Utility::convertArrayToContainer(amx, params[10], params[15], mapIcon->players);
	Utility::convertArrayToContainer(amx, params[11], params[16], mapIcon->areas);
	mapIcon->priority = static_cast<int>(params[12]);
	core->getGrid()->addMapIcon(mapIcon);
	core->getData()->mapIcons.insert(std::make_pair(mapIconId, mapIcon));
	return static_cast<cell>(mapIconId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamic3DTextLabelEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(19);
	const float px = amx_ctof(params[3]), py = amx_ctof(params[4]), pz = amx_ctof(params[5]);
	const float drawDist = amx_ctof(params[6]);
	const float streamDist = amx_ctof(params[10]);
	const bool attached = static_cast<int>(params[7]) != INVALID_PLAYER_ID || static_cast<int>(params[8]) != INVALID_VEHICLE_ID;
	if (!attached)
	{
		CHECK_POS_VEC3(px, py, pz);
	}
	else
	{
		CHECK_FINITE(px, "position x");
		CHECK_FINITE(py, "position y");
		CHECK_FINITE(pz, "position z");
	}
	CHECK_DRAW_DISTANCE(drawDist);
	CHECK_STREAM_DISTANCE(streamDist);
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_3D_TEXT_LABEL) == core->getData()->textLabels.size())
	{
		return INVALID_STREAMER_ID;
	}
	int textLabelId = Item::TextLabel::identifier.get();
	Item::SharedTextLabel textLabel = std::make_shared<Item::TextLabel>();
	textLabel->amx = amx;
	textLabel->textLabelId = textLabelId;
	textLabel->inverseAreaChecking = false;
	textLabel->originalComparableStreamDistance = -1.0f;
	textLabel->positionOffset = Eigen::Vector3f::Zero();
	textLabel->streamCallbacks = false;
	textLabel->text = Utility::convertNativeStringToString(amx, params[1]);
	textLabel->color = static_cast<int>(params[2]);
	textLabel->position = Eigen::Vector3f(px, py, pz);
	textLabel->drawDistance = drawDist;
	if (static_cast<int>(params[7]) != INVALID_PLAYER_ID || static_cast<int>(params[8]) != INVALID_VEHICLE_ID)
	{
		textLabel->attach = std::make_shared<Item::TextLabel::Attach>();
		textLabel->attach->player = static_cast<int>(params[7]);
		textLabel->attach->vehicle = static_cast<int>(params[8]);
		if (textLabel->position.cwiseAbs().maxCoeff() > 50.0f)
		{
			textLabel->position.setZero();
		}
		core->getStreamer()->attachedTextLabels.insert(textLabel);
	}
	textLabel->testLOS = static_cast<int>(params[9]) != 0;
	textLabel->comparableStreamDistance = streamDist < STREAMER_STATIC_DISTANCE_CUTOFF ? streamDist : streamDist * streamDist;
	textLabel->streamDistance = streamDist;
	Utility::convertArrayToContainer(amx, params[11], params[16], textLabel->worlds);
	Utility::convertArrayToContainer(amx, params[12], params[17], textLabel->interiors);
	Utility::convertArrayToContainer(amx, params[13], params[18], textLabel->players);
	Utility::convertArrayToContainer(amx, params[14], params[19], textLabel->areas);
	textLabel->priority = static_cast<int>(params[15]);
	core->getGrid()->addTextLabel(textLabel);
	core->getData()->textLabels.insert(std::make_pair(textLabelId, textLabel));
	return static_cast<cell>(textLabelId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamicCircleEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(10);
	const float x = amx_ctof(params[1]), y = amx_ctof(params[2]);
	const float size = amx_ctof(params[3]);
	if (!Validation::isCoordInRange(x) || !Validation::isCoordInRange(y))
	{
		Utility::logError("CreateDynamicCircleEx: invalid position.");
		return 0;
	}
	CHECK_RADIUS(size);
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_AREA) == core->getData()->areas.size())
	{
		return INVALID_STREAMER_ID;
	}
	int areaId = Item::Area::identifier.get();
	Item::SharedArea area = std::make_shared<Item::Area>();
	area->amx = amx;
	area->areaId = areaId;
	area->spectateMode = true;
	area->type = STREAMER_AREA_TYPE_CIRCLE;
	area->position = Eigen::Vector2f(x, y);
	area->comparableSize = size * size;
	area->size = size;
	Utility::convertArrayToContainer(amx, params[4], params[8], area->worlds);
	Utility::convertArrayToContainer(amx, params[5], params[9], area->interiors);
	Utility::convertArrayToContainer(amx, params[6], params[10], area->players);
	area->priority = static_cast<int>(params[7]);
  	core->getGrid()->addArea(area);
	core->getData()->areas.insert(std::make_pair(areaId, area));
	return static_cast<cell>(areaId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamicCylinderEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(12);
	const float x = amx_ctof(params[1]), y = amx_ctof(params[2]);
	const float minZ = amx_ctof(params[3]), maxZ = amx_ctof(params[4]);
	const float size = amx_ctof(params[5]);
	if (!Validation::isCoordInRange(x) || !Validation::isCoordInRange(y) || !Validation::isFinitef(minZ) || !Validation::isFinitef(maxZ))
	{
		Utility::logError("CreateDynamicCylinderEx: invalid position or height.");
		return 0;
	}
	CHECK_RADIUS(size);
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_AREA) == core->getData()->areas.size())
	{
		return INVALID_STREAMER_ID;
	}
	int areaId = Item::Area::identifier.get();
	Item::SharedArea area = std::make_shared<Item::Area>();
	area->amx = amx;
	area->areaId = areaId;
	area->spectateMode = true;
	area->type = STREAMER_AREA_TYPE_CYLINDER;
	area->position = Eigen::Vector2f(x, y);
	area->height = Eigen::Vector2f(minZ, maxZ);
	area->comparableSize = size * size;
	area->size = size;
	Utility::convertArrayToContainer(amx, params[6], params[10], area->worlds);
	Utility::convertArrayToContainer(amx, params[7], params[11], area->interiors);
	Utility::convertArrayToContainer(amx, params[8], params[12], area->players);
	area->priority = static_cast<int>(params[9]);
	core->getGrid()->addArea(area);
	core->getData()->areas.insert(std::make_pair(areaId, area));
	return static_cast<cell>(areaId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamicSphereEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(11);
	const float px = amx_ctof(params[1]), py = amx_ctof(params[2]), pz = amx_ctof(params[3]);
	const float size = amx_ctof(params[4]);
	CHECK_POS_VEC3(px, py, pz);
	CHECK_RADIUS(size);
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_AREA) == core->getData()->areas.size())
	{
		return INVALID_STREAMER_ID;
	}
	int areaId = Item::Area::identifier.get();
	Item::SharedArea area = std::make_shared<Item::Area>();
	area->amx = amx;
	area->areaId = areaId;
	area->spectateMode = true;
	area->type = STREAMER_AREA_TYPE_SPHERE;
	area->position = Eigen::Vector3f(px, py, pz);
	area->comparableSize = size * size;
	area->size = size;
	Utility::convertArrayToContainer(amx, params[5], params[9], area->worlds);
	Utility::convertArrayToContainer(amx, params[6], params[10], area->interiors);
	Utility::convertArrayToContainer(amx, params[7], params[11], area->players);
	area->priority = static_cast<int>(params[8]);
	core->getGrid()->addArea(area);
	core->getData()->areas.insert(std::make_pair(areaId, area));
	return static_cast<cell>(areaId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamicRectangleEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(11);
	const float x1 = amx_ctof(params[1]), y1 = amx_ctof(params[2]);
	const float x2 = amx_ctof(params[3]), y2 = amx_ctof(params[4]);
	if (!Validation::isCoordInRange(x1) || !Validation::isCoordInRange(y1) || !Validation::isCoordInRange(x2) || !Validation::isCoordInRange(y2))
	{
		Utility::logError("CreateDynamicRectangleEx: invalid rectangle coordinates.");
		return 0;
	}
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_AREA) == core->getData()->areas.size())
	{
		return INVALID_STREAMER_ID;
	}
	int areaId = Item::Area::identifier.get();
	Item::SharedArea area = std::make_shared<Item::Area>();
	area->amx = amx;
	area->areaId = areaId;
	area->spectateMode = true;
	area->type = STREAMER_AREA_TYPE_RECTANGLE;
	area->position = Box2d(Eigen::Vector2f(x1, y1), Eigen::Vector2f(x2, y2));
	boost::geometry::correct(std::get<Box2d>(area->position));
	area->comparableSize = static_cast<float>(boost::geometry::comparable_distance(std::get<Box2d>(area->position).min_corner(), std::get<Box2d>(area->position).max_corner()));
	area->size = static_cast<float>(boost::geometry::distance(std::get<Box2d>(area->position).min_corner(), std::get<Box2d>(area->position).max_corner()));
	Utility::convertArrayToContainer(amx, params[5], params[9], area->worlds);
	Utility::convertArrayToContainer(amx, params[6], params[10], area->interiors);
	Utility::convertArrayToContainer(amx, params[7], params[11], area->players);
	area->priority = static_cast<int>(params[8]);
	core->getGrid()->addArea(area);
	core->getData()->areas.insert(std::make_pair(areaId, area));
	return static_cast<cell>(areaId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamicCuboidEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(13);
	const float x1 = amx_ctof(params[1]), y1 = amx_ctof(params[2]), z1 = amx_ctof(params[3]);
	const float x2 = amx_ctof(params[4]), y2 = amx_ctof(params[5]), z2 = amx_ctof(params[6]);
	CHECK_POS_VEC3(x1, y1, z1);
	CHECK_POS_VEC3(x2, y2, z2);
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_AREA) == core->getData()->areas.size())
	{
		return INVALID_STREAMER_ID;
	}
	int areaId = Item::Area::identifier.get();
	Item::SharedArea area = std::make_shared<Item::Area>();
	area->amx = amx;
	area->areaId = areaId;
	area->spectateMode = true;
	area->type = STREAMER_AREA_TYPE_CUBOID;
	area->position = Box3d(Eigen::Vector3f(x1, y1, z1), Eigen::Vector3f(x2, y2, z2));
	boost::geometry::correct(std::get<Box3d>(area->position));
	area->comparableSize = static_cast<float>(boost::geometry::comparable_distance(Eigen::Vector2f(std::get<Box3d>(area->position).min_corner()[0], std::get<Box3d>(area->position).min_corner()[1]), Eigen::Vector2f(std::get<Box3d>(area->position).max_corner()[0], std::get<Box3d>(area->position).max_corner()[1])));
	area->size = static_cast<float>(boost::geometry::distance(Eigen::Vector2f(std::get<Box3d>(area->position).min_corner()[0], std::get<Box3d>(area->position).min_corner()[1]), Eigen::Vector2f(std::get<Box3d>(area->position).max_corner()[0], std::get<Box3d>(area->position).max_corner()[1])));
	Utility::convertArrayToContainer(amx, params[7], params[11], area->worlds);
	Utility::convertArrayToContainer(amx, params[8], params[12], area->interiors);
	Utility::convertArrayToContainer(amx, params[9], params[13], area->players);
	area->priority = static_cast<int>(params[10]);
	core->getGrid()->addArea(area);
	core->getData()->areas.insert(std::make_pair(areaId, area));
	return static_cast<cell>(areaId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamicPolygonEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(11);
	const int pointCount = static_cast<int>(params[4]);
	const float minZ = amx_ctof(params[2]), maxZ = amx_ctof(params[3]);
	if (!Validation::isFinitef(minZ) || !Validation::isFinitef(maxZ))
	{
		Utility::logError("CreateDynamicPolygonEx: invalid height bounds.");
		return 0;
	}
	if (!Validation::isPolygonArraySize(pointCount))
	{
		Utility::logError("CreateDynamicPolygonEx: Number of points %d invalid (must be even and >= 6).", pointCount);
		return INVALID_STREAMER_ID;
	}
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_AREA) == core->getData()->areas.size())
	{
		return INVALID_STREAMER_ID;
	}
	int areaId = Item::Area::identifier.get();
	Item::SharedArea area = std::make_shared<Item::Area>();
	area->amx = amx;
	area->areaId = areaId;
	area->spectateMode = true;
	area->type = STREAMER_AREA_TYPE_POLYGON;
	Utility::convertArrayToPolygon(amx, params[1], params[4], std::get<Polygon2d>(area->position));
	area->height = Eigen::Vector2f(amx_ctof(params[2]), amx_ctof(params[3]));
	Box2d box = boost::geometry::return_envelope<Box2d>(std::get<Polygon2d>(area->position));
	area->comparableSize = static_cast<float>(boost::geometry::comparable_distance(box.min_corner(), box.max_corner()));
	area->size = static_cast<float>(boost::geometry::distance(box.min_corner(), box.max_corner()));
	Utility::convertArrayToContainer(amx, params[5], params[9], area->worlds);
	Utility::convertArrayToContainer(amx, params[6], params[10], area->interiors);
	Utility::convertArrayToContainer(amx, params[7], params[11], area->players);
	area->priority = static_cast<int>(params[8]);
	core->getGrid()->addArea(area);
	core->getData()->areas.insert(std::make_pair(areaId, area));
	return static_cast<cell>(areaId);
}

cell AMX_NATIVE_CALL Natives::CreateDynamicActorEx(AMX *amx, cell *params)
{
	CHECK_PARAMS(17);
	const int modelId = static_cast<int>(params[1]);
	const float px = amx_ctof(params[2]), py = amx_ctof(params[3]), pz = amx_ctof(params[4]);
	const float rot = amx_ctof(params[5]);
	const float health = amx_ctof(params[7]);
	const float streamDist = amx_ctof(params[8]);
	CHECK_MODEL_ID(modelId);
	CHECK_POS_VEC3(px, py, pz);
	CHECK_FINITE(rot, "rotation");
	CHECK_FINITE(health, "health");
	CHECK_STREAM_DISTANCE(streamDist);
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_ACTOR) == core->getData()->actors.size())
	{
		return INVALID_STREAMER_ID;
	}
	int actorId = Item::Actor::identifier.get();
	Item::SharedActor actor = std::make_shared<Item::Actor>();
	actor->amx = amx;
	actor->actorId = actorId;
	actor->inverseAreaChecking = false;
	actor->originalComparableStreamDistance = -1.0f;
	actor->modelId = modelId;
	actor->position = Eigen::Vector3f(px, py, pz);
	actor->rotation = rot;
	actor->invulnerable = static_cast<int>(params[6]) != 0;
	actor->health = health;
	actor->comparableStreamDistance = streamDist < STREAMER_STATIC_DISTANCE_CUTOFF ? streamDist : streamDist * streamDist;
	actor->streamDistance = streamDist;
	Utility::convertArrayToContainer(amx, params[9], params[14], actor->worlds);
	Utility::convertArrayToContainer(amx, params[10], params[15], actor->interiors);
	Utility::convertArrayToContainer(amx, params[11], params[16], actor->players);
	Utility::convertArrayToContainer(amx, params[12], params[17], actor->areas);
	actor->priority = static_cast<int>(params[13]);
	core->getGrid()->addActor(actor);
	core->getData()->actors.insert(std::make_pair(actorId, actor));
	return static_cast<cell>(actorId);
}
