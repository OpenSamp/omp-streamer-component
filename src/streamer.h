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

#ifndef STREAMER_H
#define STREAMER_H

#include <array>
#include <cstdint>

#include "cell.h"
#include "item.h"
#include "player.h"
#include "utility.h"

class Streamer
{
public:
	Streamer();

	inline float getLastUpdateTime()
	{
		return lastUpdateTime;
	}

	inline std::size_t getTickRate()
	{
		return tickRate;
	}

	inline bool setTickRate(std::size_t value)
	{
		if (value > 0)
		{
			tickRate = value;
			return true;
		}
		return false;
	}

	void startAutomaticUpdate();
	void startManualUpdate(Player &player, int type);

	bool processPlayerArea(Player &player, const Item::SharedArea &a, const int state);

	void processActiveItems();

	// Phase telemetry: per-tick, per-streamer-type time budget + streamed counts.
	// Cumulative until `resetStats()` is called. Indexed by STREAMER_TYPE_*.
	std::array<uint64_t, STREAMER_MAX_TYPES> phaseTimeNs{};
	std::array<uint64_t, STREAMER_MAX_TYPES> phaseStreamInCount{};
	std::array<uint64_t, STREAMER_MAX_TYPES> phaseStreamOutCount{};
	std::size_t phaseTickCount = 0;
	inline void resetStats()
	{
		phaseTimeNs.fill(0);
		phaseStreamInCount.fill(0);
		phaseStreamOutCount.fill(0);
		phaseTickCount = 0;
	}

	// Stream-out hysteresis (anti-flicker). Multiplier applied to the destroy-check
	// radius per type: an item already streamed in stays streamed in until the player
	// is `hysteresisFactor * streamDistance` away. Default 1.0 = off (legacy behavior).
	// Typical recommended value: 1.05 .. 1.10.
	std::array<float, STREAMER_MAX_TYPES> hysteresisFactor{};
	inline float getHysteresisFactor(int type) const
	{
		if (type < 0 || type >= STREAMER_MAX_TYPES) return 1.0f;
		const float v = hysteresisFactor[type];
		return v > 0.0f ? v : 1.0f;
	}
	inline bool setHysteresisFactor(int type, float value)
	{
		if (type < 0 || type >= STREAMER_MAX_TYPES) return false;
		if (!(value >= 1.0f && value <= 10.0f)) return false; // reject NaN, negative, silly large
		hysteresisFactor[type] = value;
		return true;
	}

	std::unordered_set<Item::SharedArea> attachedAreas;
	std::unordered_set<Item::SharedObject> attachedObjects;
	std::unordered_set<Item::SharedTextLabel> attachedTextLabels;
	std::unordered_set<Item::SharedObject> movingObjects;
private:
	void calculateAverageElapsedTime();

	void executeCallbacks();

	void performPlayerUpdate(Player &player, bool automatic);

	void discoverActors(Player &player, const std::vector<SharedCell> &cells);
	void streamActors();

	void processAreas(Player &player, const std::vector<SharedCell> &cells);
	void processCheckpoints(Player &player, const std::vector<SharedCell> &cells);
	void processRaceCheckpoints(Player &player, const std::vector<SharedCell> &cells);
	void processMapIcons(Player &player, const std::vector<SharedCell> &cells);
	void processObjects(Player &player, const std::vector<SharedCell> &cells);

	void discoverPickups(Player &player, const std::vector<SharedCell> &cells);
	void streamPickups();
	
	void processTextLabels(Player &player, const std::vector<SharedCell> &cells);

	void processMovingObjects();
	void processAttachedAreas();
	void processAttachedObjects();
	void processAttachedTextLabels();

	std::size_t tickCount;
	std::size_t tickRate;

	float averageElapsedTime;
	float lastUpdateTime;

	std::tuple<float, float> velocityBoundaries;

	std::multimap<int, std::tuple<int, int> > areaEnterCallbacks;
	std::multimap<int, std::tuple<int, int> > areaLeaveCallbacks;

	std::vector<int> objectMoveCallbacks;
protected:
	std::vector<std::tuple<int, int, int> > streamInCallbacks;
	std::vector<std::tuple<int, int, int> > streamOutCallbacks;

	template<std::size_t N, typename T>
	inline bool doesPlayerSatisfyConditions(const std::bitset<N> &a, const T &b, const std::unordered_set<T> &c, const T &d, const std::unordered_set<T> &e, const T &f)
	{
		return (a[b] && (c.empty() || c.find(d) != c.end()) && (e.empty() || e.find(f) != e.end()));
	}

	template<std::size_t N, typename T>
	inline bool doesPlayerSatisfyConditions(const std::bitset<N> &a, const T &b, const std::unordered_set<T> &c, const T &d, const std::unordered_set<T> &e, const T &f, const std::unordered_set<T> &g, const std::unordered_set<T> &h, bool i)
	{
		return (a[b] && (c.empty() || c.find(d) != c.end()) && (e.empty() || e.find(f) != e.end()) && (g.empty() || i ? !Utility::isContainerWithinContainer(g, h) : Utility::isContainerWithinContainer(g, h)));
	}
};

#endif
