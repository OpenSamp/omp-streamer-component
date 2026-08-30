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

#include "chunk-streamer.h"
#include "core.h"

ChunkStreamer::ChunkStreamer()
{
	chunkSize[STREAMER_TYPE_OBJECT] = 100;
	chunkSize[STREAMER_TYPE_MAP_ICON] = 100;
	chunkSize[STREAMER_TYPE_3D_TEXT_LABEL] = 100;
	// Enabled: amortise a large stream-in burst (dense RP interiors reach ~850 visible objects at
	// once) across several ticks instead of firing every CreateObject/SetObjectMaterial in one tick.
	// The single-tick burst peaks reliable-RPC/s high enough to trip acks_limit for higher-ping
	// players. chunkSize / chunkTickRate here are starting defaults; tune from live [StreamerDiag]
	// telemetry (the per-second create/destroy rate tells us the real server tick frequency).
	chunkStreamingEnabled = true;
}

std::size_t ChunkStreamer::getChunkSize(int type)
{
	switch (type)
	{
		case STREAMER_TYPE_OBJECT:
		{
			return chunkSize[STREAMER_TYPE_OBJECT];
		}
		case STREAMER_TYPE_MAP_ICON:
		{
			return chunkSize[STREAMER_TYPE_MAP_ICON];
		}
		case STREAMER_TYPE_3D_TEXT_LABEL:
		{
			return chunkSize[STREAMER_TYPE_3D_TEXT_LABEL];
		}
	}
	return 0;
}

bool ChunkStreamer::setChunkSize(int type, std::size_t value)
{
	if (value > 0)
	{
		switch (type)
		{
			case STREAMER_TYPE_OBJECT:
			{
				chunkSize[STREAMER_TYPE_OBJECT] = value;
				return true;
			}
			case STREAMER_TYPE_MAP_ICON:
			{
				chunkSize[STREAMER_TYPE_MAP_ICON] = value;
				return true;
			}
			case STREAMER_TYPE_3D_TEXT_LABEL:
			{
				chunkSize[STREAMER_TYPE_3D_TEXT_LABEL] = value;
				return true;
			}
		}
	}
	return false;
}

void ChunkStreamer::performPlayerChunkUpdate(Player &player, bool automatic)
{
	for (std::vector<int>::const_iterator t = core->getData()->typePriority.begin(); t != core->getData()->typePriority.end(); ++t)
	{
		switch (*t)
		{
			case STREAMER_TYPE_OBJECT:
			{
				if (player.processingChunks[STREAMER_TYPE_OBJECT])
				{
					streamObjects(player, automatic);
				}
				break;
			}
			case STREAMER_TYPE_MAP_ICON:
			{
				if (player.processingChunks[STREAMER_TYPE_MAP_ICON])
				{
					streamMapIcons(player, automatic);
				}
				break;
			}
			case STREAMER_TYPE_3D_TEXT_LABEL:
			{
				if (player.processingChunks[STREAMER_TYPE_3D_TEXT_LABEL])
				{
					streamTextLabels(player, automatic);
				}
				break;
			}
		}
	}
}

void ChunkStreamer::discoverMapIcons(Player &player, const std::vector<SharedCell> &cells)
{
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
					distance = Utility::squaredDistance3(player.position, Eigen::Vector3f(m->second->position + m->second->positionOffset));
				}
			}
			std::unordered_map<int, int>::iterator i = player.internalMapIcons.find(m->first);
			if (distance < (m->second->comparableStreamDistance * player.radiusMultipliers[STREAMER_TYPE_MAP_ICON]))
			{
				if (i == player.internalMapIcons.end())
				{
					player.discoveredMapIcons.insertOrAssign(m->second->priority, distance, m->first, m->second);
				}
				else
				{
					if (m->second->cell)
					{
						player.visibleCell->mapIcons.insert(*m);
					}
					player.existingMapIcons.insertOrAssign(m->second->priority, distance, m->first, m->second);
				}
			}
			else
			{
				if (i != player.internalMapIcons.end())
				{
					player.removedMapIcons.insert(i->first);
				}
			}
		}
	}
	if (!player.discoveredMapIcons.empty() || !player.removedMapIcons.empty())
	{
		player.processingChunks.set(STREAMER_TYPE_MAP_ICON);
	}
}

void ChunkStreamer::streamMapIcons(Player &player, bool automatic)
{
	if (!automatic || ++player.chunkTickCount[STREAMER_TYPE_MAP_ICON] >= player.chunkTickRate[STREAMER_TYPE_MAP_ICON])
	{
		std::size_t chunkCount = 0;
		if (!player.removedMapIcons.empty())
		{
			std::unordered_set<int>::iterator r = player.removedMapIcons.begin();
			while (r != player.removedMapIcons.end())
			{
				if (automatic && ++chunkCount > chunkSize[STREAMER_TYPE_MAP_ICON])
				{
					break;
				}
				std::unordered_map<int, int>::iterator i = player.internalMapIcons.find(*r);
				if (i != player.internalMapIcons.end())
				{
					StreamerApi::RemovePlayerMapIcon(player.playerId, i->second);
					std::unordered_map<int, Item::SharedMapIcon>::iterator m = core->getData()->mapIcons.find(*r);
					if (m != core->getData()->mapIcons.end())
					{
						if (m->second->streamCallbacks)
						{
							streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_MAP_ICON, *r, player.playerId));
						}
					}
					player.mapIconIdentifier.remove(i->second, player.internalMapIcons.size());
					player.internalMapIcons.erase(i);
				}
				r = player.removedMapIcons.erase(r);
			}
		}
		else
		{
			auto d = player.discoveredMapIcons.begin();
			while (d != player.discoveredMapIcons.end())
			{
				if (automatic && ++chunkCount > chunkSize[STREAMER_TYPE_MAP_ICON])
				{
					break;
				}
				const auto &icon = d->second;
				std::unordered_map<int, int>::iterator i = player.internalMapIcons.find(icon->mapIconId);
				if (i != player.internalMapIcons.end())
				{
					d = player.discoveredMapIcons.erase(d);
					continue;
				}
				if (player.internalMapIcons.size() == player.maxVisibleMapIcons)
				{
					auto e = player.existingMapIcons.rbegin();
					if (e != player.existingMapIcons.rend())
					{
						if (e->first.priority < d->first.priority || (e->first.distance > STREAMER_STATIC_DISTANCE_CUTOFF && d->first.distance < e->first.distance))
						{
							const int worstId = e->first.id;
							const auto &worst = e->second;
							std::unordered_map<int, int>::iterator j = player.internalMapIcons.find(worstId);
							if (j != player.internalMapIcons.end())
							{
								StreamerApi::RemovePlayerMapIcon(player.playerId, j->second);
								if (worst->streamCallbacks)
								{
									streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_MAP_ICON, worstId, player.playerId));
								}
								player.mapIconIdentifier.remove(j->second, player.internalMapIcons.size());
								player.internalMapIcons.erase(j);
							}
							if (worst->cell)
							{
								player.visibleCell->mapIcons.erase(worstId);
							}
							player.existingMapIcons.popWorst();
						}
					}
					if (player.internalMapIcons.size() == player.maxVisibleMapIcons)
					{
						player.discoveredMapIcons.clear();
						break;
					}
				}
				int internalId = player.mapIconIdentifier.get();
				StreamerApi::SetPlayerMapIcon(player.playerId, internalId, icon->position[0], icon->position[1], icon->position[2], icon->type, icon->color, icon->style);
				if (icon->streamCallbacks)
				{
					streamInCallbacks.push_back(std::make_tuple(STREAMER_TYPE_MAP_ICON, icon->mapIconId, player.playerId));
				}
				player.internalMapIcons.insert(std::make_pair(icon->mapIconId, internalId));
				if (icon->cell)
				{
					player.visibleCell->mapIcons.insert(std::make_pair(icon->mapIconId, icon));
				}
				d = player.discoveredMapIcons.erase(d);
			}
		}
		player.chunkTickCount[STREAMER_TYPE_MAP_ICON] = 0;
	}
	if (player.discoveredMapIcons.empty() && player.removedMapIcons.empty())
	{
		player.existingMapIcons.clear();
		player.processingChunks.reset(STREAMER_TYPE_MAP_ICON);
	}
}

void ChunkStreamer::discoverObjects(Player &player, const std::vector<SharedCell> &cells)
{
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
						distance = Utility::squaredDistance3(player.position, o->second->attach->position) + std::numeric_limits<float>::epsilon();
					}
					else
					{
						distance = Utility::squaredDistance3(player.position, Eigen::Vector3f(o->second->position + o->second->positionOffset));
					}
				}
			}
			std::unordered_map<int, int>::iterator i = player.internalObjects.find(o->first);
			if (distance < (o->second->comparableStreamDistance * player.radiusMultipliers[STREAMER_TYPE_OBJECT]))
			{
				if (i == player.internalObjects.end())
				{
					player.discoveredObjects.insertOrAssign(o->second->priority, distance, o->first, o->second);
				}
				else
				{
					if (o->second->cell)
					{
						player.visibleCell->objects.insert(*o);
					}
					player.existingObjects.insertOrAssign(o->second->priority, distance, o->first, o->second);
				}
			}
			else
			{
				if (i != player.internalObjects.end())
				{
					player.removedObjects.insert(i->first);
				}
			}
		}
	}
	if (!player.discoveredObjects.empty() || !player.removedObjects.empty())
	{
		player.processingChunks.set(STREAMER_TYPE_OBJECT);
	}
}

void ChunkStreamer::streamObjects(Player &player, bool automatic)
{
	if (!automatic || ++player.chunkTickCount[STREAMER_TYPE_OBJECT] >= player.chunkTickRate[STREAMER_TYPE_OBJECT])
	{
		std::size_t chunkCount = 0;
#if defined(STREAMER_FLOOD_DIAG)
		std::size_t createsThisCall = 0; // per-tick burst size (see diagObjMaxPerTick)
#endif
		if (!player.removedObjects.empty())
		{
			std::unordered_set<int>::iterator r = player.removedObjects.begin();
			while (r != player.removedObjects.end())
			{
				if (automatic && ++chunkCount > chunkSize[STREAMER_TYPE_OBJECT])
				{
					break;
				}
				std::unordered_map<int, int>::iterator i = player.internalObjects.find(*r);
				if (i != player.internalObjects.end())
				{
					StreamerApi::DestroyPlayerObject(player.playerId, i->second);
#if defined(STREAMER_FLOOD_DIAG)
					++player.diagObjDestroys;
#endif
					std::unordered_map<int, Item::SharedObject>::iterator o = core->getData()->objects.find(*r);
					if (o != core->getData()->objects.end())
					{
						if (o->second->streamCallbacks)
						{
							streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_OBJECT, *r, player.playerId));
						}
					}
					player.internalObjects.erase(i);
				}
				r = player.removedObjects.erase(r);
			}
		}
		else
		{
			bool streamingCanceled = false;
			auto d = player.discoveredObjects.begin();
			while (d != player.discoveredObjects.end())
			{
				if (automatic && ++chunkCount > chunkSize[STREAMER_TYPE_OBJECT])
				{
					break;
				}
				const int objId = d->first.id;
				const auto &obj = d->second;
				std::unordered_map<int, int>::iterator i = player.internalObjects.find(obj->objectId);
				if (i != player.internalObjects.end())
				{
					d = player.discoveredObjects.erase(d);
					continue;
				}
				int internalBaseId = INVALID_STREAMER_ID;
				if (obj->attach)
				{
					if (obj->attach->object != INVALID_STREAMER_ID)
					{
						std::unordered_map<int, int>::iterator j = player.internalObjects.find(obj->attach->object);
						if (j == player.internalObjects.end())
						{
							d = player.discoveredObjects.erase(d);
							continue;
						}
						internalBaseId = j->second;
					}
				}
				if (player.internalObjects.size() == player.currentVisibleObjects)
				{
					auto e = player.existingObjects.rbegin();
					if (e != player.existingObjects.rend())
					{
						if (e->first.priority < d->first.priority || (e->first.distance > STREAMER_STATIC_DISTANCE_CUTOFF && d->first.distance < e->first.distance))
						{
							const int worstId = e->first.id;
							const auto &worst = e->second;
							std::unordered_map<int, int>::iterator j = player.internalObjects.find(worstId);
							if (j != player.internalObjects.end())
							{
								StreamerApi::DestroyPlayerObject(player.playerId, j->second);
#if defined(STREAMER_FLOOD_DIAG)
								++player.diagObjDestroys;
#endif
								if (worst->streamCallbacks)
								{
									streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_OBJECT, worstId, player.playerId));
								}
								player.internalObjects.erase(j);
							}
							if (worst->cell)
							{
								player.visibleCell->objects.erase(worstId);
							}
							player.existingObjects.popWorst();
						}
					}
				}
				if (player.internalObjects.size() == player.maxVisibleObjects)
				{
					streamingCanceled = true;
					break;
				}
				int internalId = StreamerApi::CreatePlayerObject(player.playerId, obj->modelId, obj->position[0], obj->position[1], obj->position[2], obj->rotation[0], obj->rotation[1], obj->rotation[2], obj->drawDistance);
				if (internalId == INVALID_OBJECT_ID)
				{
					streamingCanceled = true;
					break;
				}
#if defined(STREAMER_FLOOD_DIAG)
				++player.diagObjCreates;
				++player.diagObjCreateIds[objId];
				++createsThisCall;
#endif
				if (obj->streamCallbacks)
				{
					streamInCallbacks.push_back(std::make_tuple(STREAMER_TYPE_OBJECT, objId, player.playerId));
				}
				if (obj->attach)
				{
					if (internalBaseId != INVALID_STREAMER_ID)
					{
						StreamerApi::AttachPlayerObjectToObject(player.playerId, internalId, internalBaseId, obj->attach->positionOffset[0], obj->attach->positionOffset[1], obj->attach->positionOffset[2], obj->attach->rotation[0], obj->attach->rotation[1], obj->attach->rotation[2], obj->attach->syncRotation);
					}
					else if (obj->attach->player != INVALID_PLAYER_ID)
					{
						StreamerApi::AttachPlayerObjectToPlayer(player.playerId, internalId, obj->attach->player, obj->attach->positionOffset[0], obj->attach->positionOffset[1], obj->attach->positionOffset[2], obj->attach->rotation[0], obj->attach->rotation[1], obj->attach->rotation[2]);
					}
					else if (obj->attach->vehicle != INVALID_VEHICLE_ID)
					{
						StreamerApi::AttachPlayerObjectToVehicle(player.playerId, internalId, obj->attach->vehicle, obj->attach->positionOffset[0], obj->attach->positionOffset[1], obj->attach->positionOffset[2], obj->attach->rotation[0], obj->attach->rotation[1], obj->attach->rotation[2]);
					}
				}
				else if (obj->move)
				{
					StreamerApi::MovePlayerObject(player.playerId, internalId, std::get<0>(obj->move->position)[0], std::get<0>(obj->move->position)[1], std::get<0>(obj->move->position)[2], obj->move->speed, std::get<0>(obj->move->rotation)[0], std::get<0>(obj->move->rotation)[1], std::get<0>(obj->move->rotation)[2]);
				}
				for (std::unordered_map<int, Item::Object::Material>::iterator m = obj->materials.begin(); m != obj->materials.end(); ++m)
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
				if (obj->noCameraCollision)
				{
					StreamerApi::SetPlayerObjectNoCameraCol(player.playerId, internalId);
				}
				player.internalObjects.insert(std::make_pair(objId, internalId));
				if (obj->cell)
				{
					player.visibleCell->objects.insert(std::make_pair(objId, obj));
				}
				d = player.discoveredObjects.erase(d);
			}
			if (streamingCanceled)
			{
				player.currentVisibleObjects = player.internalObjects.size();
				player.discoveredObjects.clear();
			}
		}
#if defined(STREAMER_FLOOD_DIAG)
		if (createsThisCall > player.diagObjMaxPerTick)
		{
			player.diagObjMaxPerTick = createsThisCall;
		}
#endif
		player.chunkTickCount[STREAMER_TYPE_OBJECT] = 0;
	}
	if (player.discoveredObjects.empty() && player.removedObjects.empty())
	{
		player.existingObjects.clear();
		player.processingChunks.reset(STREAMER_TYPE_OBJECT);
	}
}

void ChunkStreamer::discoverTextLabels(Player &player, const std::vector<SharedCell> &cells)
{
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
						distance = Utility::squaredDistance3(player.position, t->second->attach->position);
					}
					else
					{
						distance = Utility::squaredDistance3(player.position, Eigen::Vector3f(t->second->position + t->second->positionOffset));
					}
				}
			}
			std::unordered_map<int, int>::iterator i = player.internalTextLabels.find(t->first);
			if (distance < (t->second->comparableStreamDistance * player.radiusMultipliers[STREAMER_TYPE_3D_TEXT_LABEL]))
			{
				if (i == player.internalTextLabels.end())
				{
					player.discoveredTextLabels.insertOrAssign(t->second->priority, distance, t->first, t->second);
				}
				else
				{
					if (t->second->cell)
					{
						player.visibleCell->textLabels.insert(*t);
					}
					player.existingTextLabels.insertOrAssign(t->second->priority, distance, t->first, t->second);
				}
			}
			else
			{
				if (i != player.internalTextLabels.end())
				{
					player.removedTextLabels.insert(i->first);
				}
			}
		}
	}
	if (!player.discoveredTextLabels.empty() || !player.removedTextLabels.empty())
	{
		player.processingChunks.set(STREAMER_TYPE_3D_TEXT_LABEL);
	}
}

void ChunkStreamer::streamTextLabels(Player &player, bool automatic)
{
	if (!automatic || ++player.chunkTickCount[STREAMER_TYPE_3D_TEXT_LABEL] >= player.chunkTickRate[STREAMER_TYPE_3D_TEXT_LABEL])
	{
		std::size_t chunkCount = 0;
		if (!player.removedTextLabels.empty())
		{
			std::unordered_set<int>::iterator r = player.removedTextLabels.begin();
			while (r != player.removedTextLabels.end())
			{
				if (automatic && ++chunkCount > chunkSize[STREAMER_TYPE_3D_TEXT_LABEL])
				{
					break;
				}
				std::unordered_map<int, int>::iterator i = player.internalTextLabels.find(*r);
				if (i != player.internalTextLabels.end())
				{
					StreamerApi::DeletePlayer3DTextLabel(player.playerId, i->second);
					std::unordered_map<int, Item::SharedTextLabel>::iterator t = core->getData()->textLabels.find(*r);
					if (t != core->getData()->textLabels.end())
					{
						if (t->second->streamCallbacks)
						{
							streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_3D_TEXT_LABEL, *r, player.playerId));
						}
					}
					player.internalTextLabels.erase(i);
				}
				r = player.removedTextLabels.erase(r);
			}
		}
		else
		{
			bool streamingCanceled = false;
			auto d = player.discoveredTextLabels.begin();
			while (d != player.discoveredTextLabels.end())
			{
				if (automatic && ++chunkCount > chunkSize[STREAMER_TYPE_3D_TEXT_LABEL])
				{
					break;
				}
				const int labelId = d->first.id;
				const auto &label = d->second;
				std::unordered_map<int, int>::iterator i = player.internalTextLabels.find(label->textLabelId);
				if (i != player.internalTextLabels.end())
				{
					d = player.discoveredTextLabels.erase(d);
					continue;
				}
				if (player.internalTextLabels.size() == player.currentVisibleTextLabels)
				{
					auto e = player.existingTextLabels.rbegin();
					if (e != player.existingTextLabels.rend())
					{
						if (e->first.priority < d->first.priority || (e->first.distance > STREAMER_STATIC_DISTANCE_CUTOFF && d->first.distance < e->first.distance))
						{
							const int worstId = e->first.id;
							const auto &worst = e->second;
							std::unordered_map<int, int>::iterator j = player.internalTextLabels.find(worstId);
							if (j != player.internalTextLabels.end())
							{
								StreamerApi::DeletePlayer3DTextLabel(player.playerId, j->second);
								if (worst->streamCallbacks)
								{
									streamOutCallbacks.push_back(std::make_tuple(STREAMER_TYPE_3D_TEXT_LABEL, worstId, player.playerId));
								}
								player.internalTextLabels.erase(j);
							}
							if (worst->cell)
							{
								player.visibleCell->textLabels.erase(worstId);
							}
							player.existingTextLabels.popWorst();
						}
					}
				}
				if (player.internalTextLabels.size() == player.maxVisibleTextLabels)
				{
					streamingCanceled = true;
					break;
				}
				int internalId = StreamerApi::CreatePlayer3DTextLabel(player.playerId, label->text.c_str(), label->color, label->position[0], label->position[1], label->position[2], label->drawDistance, label->attach ? label->attach->player : INVALID_PLAYER_ID, label->attach ? label->attach->vehicle : INVALID_VEHICLE_ID, label->testLOS);
				if (internalId == INVALID_3DTEXT_ID)
				{
					streamingCanceled = true;
					break;
				}
				if (label->streamCallbacks)
				{
					streamInCallbacks.push_back(std::make_tuple(STREAMER_TYPE_3D_TEXT_LABEL, labelId, player.playerId));
				}
				player.internalTextLabels.insert(std::make_pair(labelId, internalId));
				if (label->cell)
				{
					player.visibleCell->textLabels.insert(std::make_pair(labelId, label));
				}
				d = player.discoveredTextLabels.erase(d);
			}
			if (streamingCanceled)
			{
				player.currentVisibleTextLabels = player.internalTextLabels.size();
				player.discoveredTextLabels.clear();
			}
		}
		player.chunkTickCount[STREAMER_TYPE_3D_TEXT_LABEL] = 0;
	}
	if (player.discoveredTextLabels.empty() && player.removedTextLabels.empty())
	{
		player.existingTextLabels.clear();
		player.processingChunks.reset(STREAMER_TYPE_3D_TEXT_LABEL);
	}
}
