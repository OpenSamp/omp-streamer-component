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

#include "grid.h"
#include "core.h"

Grid::Grid()
{
	cellDistance = 360000.0f; // stream-distance² threshold for fine tier (default 600m²)
	cellSize = 300.0f;
	globalCell = std::make_shared<Cell>();
	calculateTranslationMatrix();
	// Coarse tier defaults: items with stream distance in (600m, 2500m] go into 1500×1500
	// cells instead of the O(N) globalCell bucket. Big-radius items (airports, stunt
	// ramps, signboards) only match players inside their ~9-cell fan-out instead of
	// being scanned for every player every tick.
}

CellId Grid::getCoarseCellId(const Eigen::Vector2f &position, bool insert)
{
	const float minX = std::floor(position[0] / coarseCellSize) * coarseCellSize;
	const float minY = std::floor(position[1] / coarseCellSize) * coarseCellSize;
	const CellId cellId = std::make_pair(static_cast<int>(minX + coarseCellSize * 0.5f),
	                                      static_cast<int>(minY + coarseCellSize * 0.5f));
	if (insert)
	{
		auto c = coarseCells.find(cellId);
		if (c == coarseCells.end())
		{
			auto cell = std::make_shared<Cell>(cellId);
			cell->coarse = true;
			coarseCells.emplace(cellId, std::move(cell));
		}
	}
	return cellId;
}

// Resolve (and if needed create) the target cell in the tier indicated by `coarse`.
SharedCell Grid::acquireCell(const Eigen::Vector2f &position, bool coarse)
{
	if (coarse)
	{
		CellId id = getCoarseCellId(position);
		return coarseCells[id];
	}
	CellId id = getCellId(position);
	return cells[id];
}

void Grid::addActor(const Item::SharedActor &actor)
{
	const int tier = pickTier(actor->comparableStreamDistance);
	if (tier == 0)
	{
		globalCell->actors.insert(std::make_pair(actor->actorId, actor));
		actor->cell.reset();
		return;
	}
	SharedCell cell = acquireCell(Eigen::Vector2f(actor->position[0], actor->position[1]), tier == 1);
	cell->actors.insert(std::make_pair(actor->actorId, actor));
	actor->cell = cell;
}

void Grid::addArea(const Item::SharedArea &area)
{
	// Areas tier on their own "size" (squared radius), not streamDistance.
	int tier;
	if (area->comparableSize <= comparableCellDistance)
	{
		tier = 2;
	}
	else if (comparableCoarseCellDistance > 0.0f && area->comparableSize <= comparableCoarseCellDistance)
	{
		tier = 1;
	}
	else
	{
		tier = 0;
	}
	if (tier == 0)
	{
		globalCell->areas.insert(std::make_pair(area->areaId, area));
		area->cell.reset();
		return;
	}
	Eigen::Vector2f centroid = Eigen::Vector2f::Zero();
	std::variant<Polygon2d, Box2d, Box3d, Eigen::Vector2f, Eigen::Vector3f> position;
	if (area->attach)
	{
		position = area->attach->position;
	}
	else
	{
		position = area->position;
	}
	switch (area->type)
	{
		case STREAMER_AREA_TYPE_CIRCLE:
		case STREAMER_AREA_TYPE_CYLINDER:
		{
			centroid = Eigen::Vector2f(std::get<Eigen::Vector2f>(position));
			break;
		}
		case STREAMER_AREA_TYPE_SPHERE:
		{
			centroid = Eigen::Vector2f(std::get<Eigen::Vector3f>(position)[0], std::get<Eigen::Vector3f>(position)[1]);
			break;
		}
		case STREAMER_AREA_TYPE_RECTANGLE:
		{
			boost::geometry::centroid(std::get<Box2d>(position), centroid);
			break;
		}
		case STREAMER_AREA_TYPE_CUBOID:
		{
			Eigen::Vector3f point = boost::geometry::return_centroid<Eigen::Vector3f>(std::get<Box3d>(position));
			centroid = Eigen::Vector2f(point[0], point[1]);
			break;
		}
		case STREAMER_AREA_TYPE_POLYGON:
		{
			boost::geometry::centroid(std::get<Polygon2d>(position), centroid);
			break;
		}
	}
	SharedCell cell = acquireCell(centroid, tier == 1);
	cell->areas.insert(std::make_pair(area->areaId, area));
	area->cell = cell;
}

void Grid::addCheckpoint(const Item::SharedCheckpoint &checkpoint)
{
	const int tier = pickTier(checkpoint->comparableStreamDistance);
	if (tier == 0)
	{
		globalCell->checkpoints.insert(std::make_pair(checkpoint->checkpointId, checkpoint));
		checkpoint->cell.reset();
		return;
	}
	SharedCell cell = acquireCell(Eigen::Vector2f(checkpoint->position[0], checkpoint->position[1]), tier == 1);
	cell->checkpoints.insert(std::make_pair(checkpoint->checkpointId, checkpoint));
	checkpoint->cell = cell;
}

void Grid::addMapIcon(const Item::SharedMapIcon &mapIcon)
{
	const int tier = pickTier(mapIcon->comparableStreamDistance);
	if (tier == 0)
	{
		globalCell->mapIcons.insert(std::make_pair(mapIcon->mapIconId, mapIcon));
		mapIcon->cell.reset();
		return;
	}
	SharedCell cell = acquireCell(Eigen::Vector2f(mapIcon->position[0], mapIcon->position[1]), tier == 1);
	cell->mapIcons.insert(std::make_pair(mapIcon->mapIconId, mapIcon));
	mapIcon->cell = cell;
}

void Grid::addObject(const Item::SharedObject &object)
{
	const int tier = pickTier(object->comparableStreamDistance);
	if (tier == 0)
	{
		globalCell->objects.insert(std::make_pair(object->objectId, object));
		object->cell.reset();
		return;
	}
	Eigen::Vector2f position;
	if (object->attach)
	{
		position = Eigen::Vector2f(object->attach->position[0], object->attach->position[1]);
	}
	else
	{
		position = Eigen::Vector2f(object->position[0], object->position[1]);
	}
	SharedCell cell = acquireCell(position, tier == 1);
	cell->objects.insert(std::make_pair(object->objectId, object));
	object->cell = cell;
}

void Grid::addPickup(const Item::SharedPickup &pickup)
{
	const int tier = pickTier(pickup->comparableStreamDistance);
	if (tier == 0)
	{
		globalCell->pickups.insert(std::make_pair(pickup->pickupId, pickup));
		pickup->cell.reset();
		return;
	}
	SharedCell cell = acquireCell(Eigen::Vector2f(pickup->position[0], pickup->position[1]), tier == 1);
	cell->pickups.insert(std::make_pair(pickup->pickupId, pickup));
	pickup->cell = cell;
}

void Grid::addRaceCheckpoint(const Item::SharedRaceCheckpoint &raceCheckpoint)
{
	const int tier = pickTier(raceCheckpoint->comparableStreamDistance);
	if (tier == 0)
	{
		globalCell->raceCheckpoints.insert(std::make_pair(raceCheckpoint->raceCheckpointId, raceCheckpoint));
		raceCheckpoint->cell.reset();
		return;
	}
	SharedCell cell = acquireCell(Eigen::Vector2f(raceCheckpoint->position[0], raceCheckpoint->position[1]), tier == 1);
	cell->raceCheckpoints.insert(std::make_pair(raceCheckpoint->raceCheckpointId, raceCheckpoint));
	raceCheckpoint->cell = cell;
}

void Grid::addTextLabel(const Item::SharedTextLabel &textLabel)
{
	const int tier = pickTier(textLabel->comparableStreamDistance);
	if (tier == 0)
	{
		globalCell->textLabels.insert(std::make_pair(textLabel->textLabelId, textLabel));
		textLabel->cell.reset();
		return;
	}
	Eigen::Vector2f position;
	if (textLabel->attach)
	{
		position = Eigen::Vector2f(textLabel->attach->position[0], textLabel->attach->position[1]);
	}
	else
	{
		position = Eigen::Vector2f(textLabel->position[0], textLabel->position[1]);
	}
	SharedCell cell = acquireCell(position, tier == 1);
	cell->textLabels.insert(std::make_pair(textLabel->textLabelId, textLabel));
	textLabel->cell = cell;
}

void Grid::rebuildGrid()
{
	cells.clear();
	coarseCells.clear();
	globalCell = std::make_shared<Cell>();
	calculateTranslationMatrix();
	for (std::unordered_map<int, Item::SharedActor>::iterator a = core->getData()->actors.begin(); a != core->getData()->actors.end(); ++a)
	{
		addActor(a->second);
	}
	for (std::unordered_map<int, Item::SharedArea>::iterator a = core->getData()->areas.begin(); a != core->getData()->areas.end(); ++a)
	{
		addArea(a->second);
	}
	for (std::unordered_map<int, Item::SharedCheckpoint>::iterator c = core->getData()->checkpoints.begin(); c != core->getData()->checkpoints.end(); ++c)
	{
		addCheckpoint(c->second);
	}
	for (std::unordered_map<int, Item::SharedMapIcon>::iterator m = core->getData()->mapIcons.begin(); m != core->getData()->mapIcons.end(); ++m)
	{
		addMapIcon(m->second);
	}
	for (std::unordered_map<int, Item::SharedObject>::iterator o = core->getData()->objects.begin(); o != core->getData()->objects.end(); ++o)
	{
		addObject(o->second);
	}
	for (std::unordered_map<int, Item::SharedPickup>::iterator p = core->getData()->pickups.begin(); p != core->getData()->pickups.end(); ++p)
	{
		addPickup(p->second);
	}
	for (std::unordered_map<int, Item::SharedRaceCheckpoint>::iterator r = core->getData()->raceCheckpoints.begin(); r != core->getData()->raceCheckpoints.end(); ++r)
	{
		addRaceCheckpoint(r->second);
	}
	for (std::unordered_map<int, Item::SharedTextLabel>::iterator t = core->getData()->textLabels.begin(); t != core->getData()->textLabels.end(); ++t)
	{
		addTextLabel(t->second);
	}
}

void Grid::removeActor(const Item::SharedActor &actor, bool reassign)
{
	bool found = false;
	if (actor->cell)
	{
		auto &tier = tierOf(actor->cell);
		auto c = tier.find(actor->cell->cellId);
		if (c != tier.end())
		{
			std::unordered_map<int, Item::SharedActor>::iterator a = c->second->actors.find(actor->actorId);
			if (a != c->second->actors.end())
			{
				c->second->actors.erase(a);
				eraseCellIfEmpty(c->second);
				found = true;
			}
		}
	}
	else
	{
		std::unordered_map<int, Item::SharedActor>::iterator a = globalCell->actors.find(actor->actorId);
		if (a != globalCell->actors.end())
		{
			globalCell->actors.erase(a);
			found = true;
		}
	}
	if (found)
	{
		if (reassign)
		{
			addActor(actor);
		}
	}
}

void Grid::removeArea(const Item::SharedArea &area, bool reassign)
{
	bool found = false;
	if (area->cell)
	{
		auto &tier = tierOf(area->cell);
		auto c = tier.find(area->cell->cellId);
		if (c != tier.end())
		{
			std::unordered_map<int, Item::SharedArea>::iterator a = c->second->areas.find(area->areaId);
			if (a != c->second->areas.end())
			{
				c->second->areas.erase(a);
				eraseCellIfEmpty(c->second);
				found = true;
			}
		}
	}
	else
	{
		std::unordered_map<int, Item::SharedArea>::iterator a = globalCell->areas.find(area->areaId);
		if (a != globalCell->areas.end())
		{
			globalCell->areas.erase(a);
			found = true;
		}
	}
	if (found)
	{
		if (reassign)
		{
			addArea(area);
		}
		else
		{
			if (area->attach)
			{
				core->getStreamer()->attachedAreas.erase(area);
			}
		}
	}
}

void Grid::removeCheckpoint(const Item::SharedCheckpoint &checkpoint, bool reassign)
{
	bool found = false;
	if (checkpoint->cell)
	{
		auto &tier = tierOf(checkpoint->cell);
		auto c = tier.find(checkpoint->cell->cellId);
		if (c != tier.end())
		{
			std::unordered_map<int, Item::SharedCheckpoint>::iterator d = c->second->checkpoints.find(checkpoint->checkpointId);
			if (d != c->second->checkpoints.end())
			{
				c->second->checkpoints.erase(d);
				eraseCellIfEmpty(c->second);
				found = true;
			}
		}
	}
	else
	{
		std::unordered_map<int, Item::SharedCheckpoint>::iterator c = globalCell->checkpoints.find(checkpoint->checkpointId);
		if (c != globalCell->checkpoints.end())
		{
			globalCell->checkpoints.erase(c);
			found = true;
		}
	}
	if (found)
	{
		if (reassign)
		{
			addCheckpoint(checkpoint);
		}
	}
}

void Grid::removeMapIcon(const Item::SharedMapIcon &mapIcon, bool reassign)
{
	bool found = false;
	if (mapIcon->cell)
	{
		auto &tier = tierOf(mapIcon->cell);
		auto c = tier.find(mapIcon->cell->cellId);
		if (c != tier.end())
		{
			std::unordered_map<int, Item::SharedMapIcon>::iterator m = c->second->mapIcons.find(mapIcon->mapIconId);
			if (m != c->second->mapIcons.end())
			{
				c->second->mapIcons.erase(m);
				eraseCellIfEmpty(c->second);
				found = true;
			}
		}
	}
	else
	{
		std::unordered_map<int, Item::SharedMapIcon>::iterator m = globalCell->mapIcons.find(mapIcon->mapIconId);
		if (m != globalCell->mapIcons.end())
		{
			globalCell->mapIcons.erase(m);
			found = true;
		}
	}
	if (found)
	{
		if (reassign)
		{
			addMapIcon(mapIcon);
		}
	}
}

void Grid::removeObject(const Item::SharedObject &object, bool reassign)
{
	bool found = false;
	if (object->cell)
	{
		auto &tier = tierOf(object->cell);
		auto c = tier.find(object->cell->cellId);
		if (c != tier.end())
		{
			std::unordered_map<int, Item::SharedObject>::iterator o = c->second->objects.find(object->objectId);
			if (o != c->second->objects.end())
			{
				c->second->objects.erase(o);
				eraseCellIfEmpty(c->second);
				found = true;
			}
		}
	}
	else
	{
		std::unordered_map<int, Item::SharedObject>::iterator o = globalCell->objects.find(object->objectId);
		if (o != globalCell->objects.end())
		{
			globalCell->objects.erase(o);
			found = true;
		}
	}
	if (found)
	{
		if (reassign)
		{
			addObject(object);
		}
		else
		{
			if (object->attach)
			{
				core->getStreamer()->attachedObjects.erase(object);
			}
			if (object->move)
			{
				core->getStreamer()->movingObjects.erase(object);
			}
		}
	}
}

void Grid::removePickup(const Item::SharedPickup &pickup, bool reassign)
{
	bool found = false;
	if (pickup->cell)
	{
		auto &tier = tierOf(pickup->cell);
		auto c = tier.find(pickup->cell->cellId);
		if (c != tier.end())
		{
			std::unordered_map<int, Item::SharedPickup>::iterator p = c->second->pickups.find(pickup->pickupId);
			if (p != c->second->pickups.end())
			{
				c->second->pickups.erase(p);
				eraseCellIfEmpty(c->second);
				found = true;
			}
		}
	}
	else
	{
		std::unordered_map<int, Item::SharedPickup>::iterator p = globalCell->pickups.find(pickup->pickupId);
		if (p != globalCell->pickups.end())
		{
			globalCell->pickups.erase(p);
			found = true;
		}
	}
	if (found)
	{
		if (reassign)
		{
			addPickup(pickup);
		}
	}
}

void Grid::removeRaceCheckpoint(const Item::SharedRaceCheckpoint &raceCheckpoint, bool reassign)
{
	bool found = false;
	if (raceCheckpoint->cell)
	{
		auto &tier = tierOf(raceCheckpoint->cell);
		auto c = tier.find(raceCheckpoint->cell->cellId);
		if (c != tier.end())
		{
			std::unordered_map<int, Item::SharedRaceCheckpoint>::iterator r = c->second->raceCheckpoints.find(raceCheckpoint->raceCheckpointId);
			if (r != c->second->raceCheckpoints.end())
			{
				c->second->raceCheckpoints.erase(r);
				eraseCellIfEmpty(c->second);
				found = true;
			}
		}
	}
	else
	{
		std::unordered_map<int, Item::SharedRaceCheckpoint>::iterator r = globalCell->raceCheckpoints.find(raceCheckpoint->raceCheckpointId);
		if (r != globalCell->raceCheckpoints.end())
		{
			globalCell->raceCheckpoints.erase(r);
			found = true;
		}
	}
	if (found)
	{
		if (reassign)
		{
			addRaceCheckpoint(raceCheckpoint);
		}
	}
}

void Grid::removeTextLabel(const Item::SharedTextLabel &textLabel, bool reassign)
{
	bool found = false;
	if (textLabel->cell)
	{
		auto &tier = tierOf(textLabel->cell);
		auto c = tier.find(textLabel->cell->cellId);
		if (c != tier.end())
		{
			std::unordered_map<int, Item::SharedTextLabel>::iterator t = c->second->textLabels.find(textLabel->textLabelId);
			if (t != c->second->textLabels.end())
			{
				c->second->textLabels.erase(t);
				eraseCellIfEmpty(c->second);
				found = true;
			}
		}
	}
	else
	{
		std::unordered_map<int, Item::SharedTextLabel>::iterator t = globalCell->textLabels.find(textLabel->textLabelId);
		if (t != globalCell->textLabels.end())
		{
			globalCell->textLabels.erase(t);
			found = true;
		}
	}
	if (found)
	{
		if (reassign)
		{
			addTextLabel(textLabel);
		}
		else
		{
			if (textLabel->attach)
			{
				core->getStreamer()->attachedTextLabels.erase(textLabel);
			}
		}
	}
}

CellId Grid::getCellId(const Eigen::Vector2f &position, bool insert)
{
	static Box2d box;
	box.min_corner()[0] = std::floor((position[0] / cellSize)) * cellSize;
	box.min_corner()[1] = std::floor((position[1] / cellSize)) * cellSize;
	box.max_corner()[0] = box.min_corner()[0] + cellSize;
	box.max_corner()[1] = box.min_corner()[1] + cellSize;
	Eigen::Vector2f centroid = boost::geometry::return_centroid<Eigen::Vector2f>(box);
	CellId cellId = std::make_pair(static_cast<int>(centroid[0]), static_cast<int>(centroid[1]));
	if (insert)
	{
		std::unordered_map<CellId, SharedCell, pair_hash>::iterator c = cells.find(cellId);
		if (c == cells.end())
		{
			cells[cellId] = std::make_shared<Cell>(cellId);
		}
	}
	return cellId;
}


void Grid::processDiscoveredCellsForPlayer(Player &player, std::vector<SharedCell> &playerCells,
	const std::unordered_set<CellId, pair_hash> &discoveredFineCells,
	const std::unordered_set<CellId, pair_hash> &discoveredCoarseCells)
{
	// Pick the right discovery set based on the item's tier.
	auto inDiscovery = [&](const SharedCell &itemCell) -> bool
	{
		if (!itemCell) return false;
		const auto &set = itemCell->coarse ? discoveredCoarseCells : discoveredFineCells;
		return set.find(itemCell->cellId) != set.end();
	};
	playerCells.push_back(std::make_shared<Cell>());
	if (player.enabledItems[STREAMER_TYPE_OBJECT])
	{
		std::unordered_map<int, Item::SharedObject>::iterator o = player.visibleCell->objects.begin();
		while (o != player.visibleCell->objects.end())
		{
			if (o->second->cell)
			{
				if (inDiscovery(o->second->cell))
				{
					o = player.visibleCell->objects.erase(o);
				}
				else
				{
					++o;
				}
			}
			else
			{
				o = player.visibleCell->objects.erase(o);
			}
		}
		playerCells.back()->objects.swap(player.visibleCell->objects);
	}
	if (player.enabledItems[STREAMER_TYPE_CP])
	{
		std::unordered_map<int, Item::SharedCheckpoint>::iterator c = player.visibleCell->checkpoints.begin();
		while (c != player.visibleCell->checkpoints.end())
		{
			if (c->second->cell)
			{
				if (inDiscovery(c->second->cell))
				{
					c = player.visibleCell->checkpoints.erase(c);
				}
				else
				{
					++c;
				}
			}
			else
			{
				c = player.visibleCell->checkpoints.erase(c);
			}
		}
		playerCells.back()->checkpoints.swap(player.visibleCell->checkpoints);
	}
	if (player.enabledItems[STREAMER_TYPE_RACE_CP])
	{
		std::unordered_map<int, Item::SharedRaceCheckpoint>::iterator r = player.visibleCell->raceCheckpoints.begin();
		while (r != player.visibleCell->raceCheckpoints.end())
		{
			if (r->second->cell)
			{
				if (inDiscovery(r->second->cell))
				{
					r = player.visibleCell->raceCheckpoints.erase(r);
				}
				else
				{
					++r;
				}
			}
			else
			{
				r = player.visibleCell->raceCheckpoints.erase(r);
			}
		}
		playerCells.back()->raceCheckpoints.swap(player.visibleCell->raceCheckpoints);
	}
	if (player.enabledItems[STREAMER_TYPE_MAP_ICON])
	{
		std::unordered_map<int, Item::SharedMapIcon>::iterator m = player.visibleCell->mapIcons.begin();
		while (m != player.visibleCell->mapIcons.end())
		{
			if (m->second->cell)
			{
				if (inDiscovery(m->second->cell))
				{
					m = player.visibleCell->mapIcons.erase(m);
				}
				else
				{
					++m;
				}
			}
			else
			{
				m = player.visibleCell->mapIcons.erase(m);
			}
		}
		playerCells.back()->mapIcons.swap(player.visibleCell->mapIcons);
	}
	if (player.enabledItems[STREAMER_TYPE_3D_TEXT_LABEL])
	{
		std::unordered_map<int, Item::SharedTextLabel>::iterator t = player.visibleCell->textLabels.begin();
		while (t != player.visibleCell->textLabels.end())
		{
			if (t->second->cell)
			{
				if (inDiscovery(t->second->cell))
				{
					t = player.visibleCell->textLabels.erase(t);
				}
				else
				{
					++t;
				}
			}
			else
			{
				t = player.visibleCell->textLabels.erase(t);
			}
		}
		playerCells.back()->textLabels.swap(player.visibleCell->textLabels);
	}
	if (player.enabledItems[STREAMER_TYPE_AREA])
	{
		std::unordered_map<int, Item::SharedArea>::iterator a = player.visibleCell->areas.begin();
		while (a != player.visibleCell->areas.end())
		{
			if (a->second->cell)
			{
				if (inDiscovery(a->second->cell))
				{
					a = player.visibleCell->areas.erase(a);
				}
				else
				{
					++a;
				}
			}
			else
			{
				a = player.visibleCell->areas.erase(a);
			}
		}
		playerCells.back()->areas.swap(player.visibleCell->areas);
	}
}

void Grid::findAllCellsForPlayer(Player &player, std::vector<SharedCell> &playerCells)
{
	std::unordered_set<CellId, pair_hash> discoveredFineCells;
	std::unordered_set<CellId, pair_hash> discoveredCoarseCells;
	playerCells.reserve(playerCells.size() + 20); // 9 fine + 9 coarse + global + visible
	const Eigen::Vector2f base(player.position[0], player.position[1]);
	for (int i = 0; i < translationMatrix.cols(); ++i)
	{
		Eigen::Vector2f position = base + translationMatrix.col(i);
		auto c = cells.find(getCellId(position, false));
		if (c != cells.end())
		{
			discoveredFineCells.insert(c->first);
			playerCells.push_back(c->second);
		}
	}
	if (!coarseCells.empty())
	{
		for (int i = 0; i < coarseTranslationMatrix.cols(); ++i)
		{
			Eigen::Vector2f position = base + coarseTranslationMatrix.col(i);
			auto c = coarseCells.find(getCoarseCellId(position, false));
			if (c != coarseCells.end())
			{
				discoveredCoarseCells.insert(c->first);
				playerCells.push_back(c->second);
			}
		}
	}
	processDiscoveredCellsForPlayer(player, playerCells, discoveredFineCells, discoveredCoarseCells);
	playerCells.push_back(globalCell);
}

void Grid::findMinimalCellsForPlayer(Player &player, std::vector<SharedCell> &playerCells)
{
	playerCells.reserve(playerCells.size() + 19);
	const Eigen::Vector2f base(player.position[0], player.position[1]);
	for (int i = 0; i < translationMatrix.cols(); ++i)
	{
		Eigen::Vector2f position = base + translationMatrix.col(i);
		auto c = cells.find(getCellId(position, false));
		if (c != cells.end())
		{
			playerCells.push_back(c->second);
		}
	}
	if (!coarseCells.empty())
	{
		for (int i = 0; i < coarseTranslationMatrix.cols(); ++i)
		{
			Eigen::Vector2f position = base + coarseTranslationMatrix.col(i);
			auto c = coarseCells.find(getCoarseCellId(position, false));
			if (c != coarseCells.end())
			{
				playerCells.push_back(c->second);
			}
		}
	}
	playerCells.push_back(globalCell);
}

void Grid::findMinimalCellsForPoint(const Eigen::Vector2f &point, std::vector<SharedCell> &pointCells)
{
	pointCells.reserve(pointCells.size() + 19);
	for (int i = 0; i < translationMatrix.cols(); ++i)
	{
		Eigen::Vector2f position = point + translationMatrix.col(i);
		auto c = cells.find(getCellId(position, false));
		if (c != cells.end())
		{
			pointCells.push_back(c->second);
		}
	}
	if (!coarseCells.empty())
	{
		for (int i = 0; i < coarseTranslationMatrix.cols(); ++i)
		{
			Eigen::Vector2f position = point + coarseTranslationMatrix.col(i);
			auto c = coarseCells.find(getCoarseCellId(position, false));
			if (c != coarseCells.end())
			{
				pointCells.push_back(c->second);
			}
		}
	}
	pointCells.push_back(globalCell);
}

void Grid::findMinimalCellsForPoint(const Eigen::Vector2f &point, std::vector<SharedCell> &pointCells, float range)
{
	for (auto c = cells.begin(); c != cells.end(); ++c)
	{
		Eigen::Vector2f corner(static_cast<float>(c->first.first) - (cellSize / 2.0f), static_cast<float>(c->first.second) - (cellSize / 2.0f));
		Eigen::Vector2f delta(point[0] - std::max(corner[0], std::min(point[0], corner[0] + cellSize)), point[1] - std::max(corner[1], std::min(point[1], corner[1] + cellSize)));
		if (((delta[0] * delta[0]) + (delta[1] * delta[1])) < range)
		{
			pointCells.push_back(c->second);
		}
	}
	for (auto c = coarseCells.begin(); c != coarseCells.end(); ++c)
	{
		Eigen::Vector2f corner(static_cast<float>(c->first.first) - (coarseCellSize / 2.0f), static_cast<float>(c->first.second) - (coarseCellSize / 2.0f));
		Eigen::Vector2f delta(point[0] - std::max(corner[0], std::min(point[0], corner[0] + coarseCellSize)), point[1] - std::max(corner[1], std::min(point[1], corner[1] + coarseCellSize)));
		if (((delta[0] * delta[0]) + (delta[1] * delta[1])) < range)
		{
			pointCells.push_back(c->second);
		}
	}
	pointCells.push_back(globalCell);
}
