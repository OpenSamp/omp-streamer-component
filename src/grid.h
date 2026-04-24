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

#ifndef GRID_H
#define GRID_H

#include "cell.h"

class Grid
{
public:
	Grid();

	void addActor(const Item::SharedActor &actor);
	void addArea(const Item::SharedArea &area);
	void addCheckpoint(const Item::SharedCheckpoint &checkpoint);
	void addMapIcon(const Item::SharedMapIcon &mapIcon);
	void addObject(const Item::SharedObject &object);
	void addPickup(const Item::SharedPickup &pickup);
	void addRaceCheckpoint(const Item::SharedRaceCheckpoint &raceCheckpoint);
	void addTextLabel(const Item::SharedTextLabel &textLabel);

	inline float getCellSize()
	{
		return cellSize;
	}

	inline float getCellDistance()
	{
		return cellDistance;
	}

	inline void setCellSize(float size)
	{
		cellSize = size;
	}

	inline void setCellDistance(float distance)
	{
		cellDistance = distance;
		comparableCellDistance = distance * distance;
	}

	// Coarse tier (two-level grid). Items with comparableStreamDistance in the band
	// (cellDistance², coarseCellDistance²] bucket into larger coarse cells rather than
	// dumping into globalCell. Default size = 1500 / distance = 2500m, tuned for the
	// big-radius objects (airports, stunt ramps, signboards) common in RP maps. Setting
	// coarseCellDistance = 0 disables the tier entirely (legacy behavior).
	inline float getCoarseCellSize() const { return coarseCellSize; }
	inline float getCoarseCellDistance() const { return coarseCellDistance; }
	inline void setCoarseCellSize(float size)
	{
		coarseCellSize = size;
		calculateCoarseTranslationMatrix();
	}
	inline void setCoarseCellDistance(float distance)
	{
		coarseCellDistance = distance;
		comparableCoarseCellDistance = distance * distance;
	}

	void rebuildGrid();

	void removeActor(const Item::SharedActor &actor, bool reassign = false);
	void removeArea(const Item::SharedArea &area, bool reassign = false);
	void removeCheckpoint(const Item::SharedCheckpoint &checkpoint, bool reassign = false);
	void removeMapIcon(const Item::SharedMapIcon &mapIcon, bool reassign = false);
	void removeObject(const Item::SharedObject &object, bool reassign = false);
	void removePickup(const Item::SharedPickup &pickup, bool reassign = false);
	void removeRaceCheckpoint(const Item::SharedRaceCheckpoint &raceCheckpoint, bool reassign = false);
	void removeTextLabel(const Item::SharedTextLabel &textLabel, bool reassign = false);

	void findAllCellsForPlayer(Player &player, std::vector<SharedCell> &playerCells);
	void findMinimalCellsForPlayer(Player &player, std::vector<SharedCell> &playerCells);
	void findMinimalCellsForPoint(const Eigen::Vector2f &point, std::vector<SharedCell> &pointCells);
	void findMinimalCellsForPoint(const Eigen::Vector2f &point, std::vector<SharedCell> &pointCells, float range);
private:
	float cellDistance;
	float cellSize;
	float comparableCellDistance;
	float coarseCellSize = 1500.0f;
	float coarseCellDistance = 2500.0f;
	float comparableCoarseCellDistance = 2500.0f * 2500.0f;
	SharedCell globalCell;

	std::unordered_map<CellId, SharedCell, pair_hash> cells;
	std::unordered_map<CellId, SharedCell, pair_hash> coarseCells;
	Eigen::Matrix<float, 2, 9> translationMatrix;
	Eigen::Matrix<float, 2, 9> coarseTranslationMatrix;

	inline void calculateTranslationMatrix()
	{
		translationMatrix << 0.0f, 0.0f, cellSize, cellSize, cellSize * -1.0f, 0.0f, cellSize * -1.0f, cellSize, cellSize * -1.0f,
		                     0.0f, cellSize, 0.0f, cellSize, 0.0f, cellSize * -1.0f, cellSize, cellSize * -1.0f, cellSize * -1.0f;
		calculateCoarseTranslationMatrix();
	}

	inline void calculateCoarseTranslationMatrix()
	{
		coarseTranslationMatrix << 0.0f, 0.0f, coarseCellSize, coarseCellSize, coarseCellSize * -1.0f, 0.0f, coarseCellSize * -1.0f, coarseCellSize, coarseCellSize * -1.0f,
		                           0.0f, coarseCellSize, 0.0f, coarseCellSize, 0.0f, coarseCellSize * -1.0f, coarseCellSize, coarseCellSize * -1.0f, coarseCellSize * -1.0f;
	}

	inline void eraseCellIfEmpty(const SharedCell &passedCell)
	{
		if (passedCell->areas.empty() && passedCell->checkpoints.empty() && passedCell->mapIcons.empty() && passedCell->objects.empty() && passedCell->pickups.empty() && passedCell->raceCheckpoints.empty() && passedCell->textLabels.empty() && passedCell->actors.empty())
		{
			if (passedCell->coarse)
			{
				coarseCells.erase(passedCell->cellId);
			}
			else
			{
				cells.erase(passedCell->cellId);
			}
		}
	}

	// Decide which tier a new item belongs to based on its (squared) stream distance.
	// Returns:
	//   0 = globalCell (static / extreme range / tier disabled)
	//   1 = coarseCells (medium range: cellDistance² < d ≤ coarseCellDistance²)
	//   2 = cells (fine tier: d ≤ cellDistance²)
	inline int pickTier(float comparableStreamDistance) const
	{
		if (comparableStreamDistance < STREAMER_STATIC_DISTANCE_CUTOFF)
		{
			return 0;
		}
		if (comparableStreamDistance <= comparableCellDistance)
		{
			return 2;
		}
		if (comparableCoarseCellDistance > 0.0f && comparableStreamDistance <= comparableCoarseCellDistance)
		{
			return 1;
		}
		return 0;
	}

	// Get-or-create cell in the specified tier.
	SharedCell acquireCell(const Eigen::Vector2f &position, bool coarse);

	// Fast dispatch to the map owning `cell` (fine or coarse). Centralises the
	// "which tier?" lookup so `remove*` / `find*` paths stay readable.
	inline std::unordered_map<CellId, SharedCell, pair_hash> &tierOf(const SharedCell &cell)
	{
		return cell->coarse ? coarseCells : cells;
	}

	CellId getCellId(const Eigen::Vector2f &position, bool insert = true);
	CellId getCoarseCellId(const Eigen::Vector2f &position, bool insert = true);
	void processDiscoveredCellsForPlayer(Player &player, std::vector<SharedCell> &playerCells,
		const std::unordered_set<CellId, pair_hash> &discoveredFineCells,
		const std::unordered_set<CellId, pair_hash> &discoveredCoarseCells);
};

#endif
