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

#ifndef ITEM_H
#define ITEM_H

#include "cell.h"
#include "identifier.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "streamer_api.h" // MAX_PLAYERS, INVALID_PLAYER_ID

// Compact per-item player-visibility mask. Replaces the old std::bitset<MAX_PLAYERS> "players"
// field, which was 128 bytes on EVERY item and in practice almost always fully set ("visible to
// everyone"). This stores only the exceptions to the base state:
//   all_ = true,  ids_ empty        -> visible to EVERYONE            (old bitset .set()  / add(-1))
//   all_ = false, ids_ empty        -> visible to NOBODY              (old bitset .reset()/ add(>=MAX))
//   all_ = false, ids_ = {a,b,...}  -> visible ONLY to those players  (those bits set)
//   all_ = true,  ids_ = {a,b,...}  -> visible to everyone EXCEPT those (all-set minus those bits)
// ids_ is kept sorted + unique. Every method reproduces the exact std::bitset helper semantics it
// replaces (see the addToContainer / removeFromContainer / getFirstValueInContainer overloads).
class PlayerVisibility
{
public:
	// bitset-compatible surface used directly by doesPlayerSatisfyConditions (a[b]) and array.h.
	bool test(std::size_t id) const
	{
		return all_ ^ std::binary_search(ids_.begin(), ids_.end(), static_cast<std::uint16_t>(id));
	}
	bool operator[](std::size_t id) const { return test(id); }
	bool all() const { return all_ && ids_.empty(); }
	bool any() const { return all_ ? ids_.size() < static_cast<std::size_t>(MAX_PLAYERS) : !ids_.empty(); }
	std::size_t count() const { return all_ ? static_cast<std::size_t>(MAX_PLAYERS) - ids_.size() : ids_.size(); }

	void set() { all_ = true; ids_.clear(); }    // visible to everyone
	void reset() { all_ = false; ids_.clear(); } // visible to nobody
	void set(std::size_t id) { setBit(static_cast<std::uint16_t>(id), true); }    // make id visible
	void reset(std::size_t id) { setBit(static_cast<std::uint16_t>(id), false); } // make id not visible

private:
	// visible=true: ensure id is shown; visible=false: ensure id is hidden. When all_, ids_ is an
	// EXCLUSION list (opposite polarity to the !all_ INCLUSION list), so membership flips.
	void setBit(std::uint16_t v, bool visible)
	{
		std::vector<std::uint16_t>::iterator it = std::lower_bound(ids_.begin(), ids_.end(), v);
		const bool present = (it != ids_.end() && *it == v);
		const bool wantInList = (visible != all_);
		if (wantInList && !present) ids_.insert(it, v);
		else if (!wantInList && present) ids_.erase(it);
	}

	bool all_ = false;
	std::vector<std::uint16_t> ids_;
};

namespace Item
{
	struct Actor
	{
		Actor();

		int actorId;
		AMX *amx;
		SharedCell cell;
		float comparableStreamDistance;
		float health;
		bool inverseAreaChecking;
		bool invulnerable;
		int modelId;
		float originalComparableStreamDistance;
		Eigen::Vector3f position;
		Eigen::Vector3f positionOffset;
		int priority;
		float rotation;
		float streamDistance;

		struct Anim
		{
			Anim();

			float delta;
			bool freeze;
			std::string lib;
			bool loop;
			bool lockx;
			bool locky;
			std::string name;
			int time;
		};

		std::shared_ptr<Anim> anim;

		std::unordered_set<int> areas;
		std::vector<int> extras;
		std::unordered_map<int, std::vector<int> > extraExtras;
		std::unordered_set<int> interiors;
		PlayerVisibility players;
		std::unordered_set<int> worlds;

		static Identifier identifier;

		EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	};

	struct Area
	{
		Area();

		AMX *amx;
		int areaId;
		SharedCell cell;
		float comparableSize;
		Eigen::Vector2f height;
		int priority;
		float size;
		bool spectateMode;
		int type;

		std::variant<Polygon2d, Box2d, Box3d, Eigen::Vector2f, Eigen::Vector3f> position;

		struct Attach
		{
			Attach();

			Eigen::Vector2f height;
			std::tuple<int, int, int> object;
			int player;
			std::variant<Polygon2d, Box2d, Box3d, Eigen::Vector2f, Eigen::Vector3f> position;
			Eigen::Vector3f positionOffset;
			int vehicle;

			EIGEN_MAKE_ALIGNED_OPERATOR_NEW
		};

		std::shared_ptr<Attach> attach;

		std::unordered_set<int> areas;
		std::vector<int> extras;
		std::unordered_map<int, std::vector<int> > extraExtras;
		std::unordered_set<int> interiors;
		PlayerVisibility players;
		std::unordered_set<int> worlds;

		static Identifier identifier;

		EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	};

	struct Checkpoint
	{
		Checkpoint();

		AMX *amx;
		SharedCell cell;
		int checkpointId;
		float comparableStreamDistance;
		bool inverseAreaChecking;
		float originalComparableStreamDistance;
		Eigen::Vector3f position;
		Eigen::Vector3f positionOffset;
		int priority;
		float size;
		bool streamCallbacks;
		float streamDistance;

		std::unordered_set<int> areas;
		std::vector<int> extras;
		std::unordered_map<int, std::vector<int> > extraExtras;
		std::unordered_set<int> interiors;
		PlayerVisibility players;
		std::unordered_set<int> worlds;

		static Identifier identifier;

		EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	};

	struct MapIcon
	{
		MapIcon();

		AMX *amx;
		SharedCell cell;
		int color;
		float comparableStreamDistance;
		bool inverseAreaChecking;
		int mapIconId;
		float originalComparableStreamDistance;
		Eigen::Vector3f position;
		Eigen::Vector3f positionOffset;
		int priority;
		bool streamCallbacks;
		float streamDistance;
		int style;
		int type;

		std::unordered_set<int> areas;
		std::vector<int> extras;
		std::unordered_map<int, std::vector<int> > extraExtras;
		std::unordered_set<int> interiors;
		PlayerVisibility players;
		std::unordered_set<int> worlds;

		static Identifier identifier;

		EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	};

	struct Object
	{
		Object();

		AMX *amx;
		SharedCell cell;
		float comparableStreamDistance;
		float drawDistance;
		bool inverseAreaChecking;
		int modelId;
		bool noCameraCollision;
		int objectId;
		float originalComparableStreamDistance;
		Eigen::Vector3f position;
		Eigen::Vector3f positionOffset;
		int priority;
		Eigen::Vector3f rotation;
		bool streamCallbacks;
		float streamDistance;

		struct Attach
		{
			Attach();

			int object;
			int player;
			Eigen::Vector3f position;
			Eigen::Vector3f positionOffset;
			Eigen::Vector3f rotation;
			bool syncRotation;
			int vehicle;

			std::unordered_set<int> worlds;

			EIGEN_MAKE_ALIGNED_OPERATOR_NEW
		};

		std::shared_ptr<Attach> attach;

		struct Material
		{
			struct Main
			{
				Main();

				int materialColor;
				int modelId;
				std::string textureName;
				std::string txdFileName;
			};

			std::shared_ptr<Main> main;

			struct Text
			{
				Text();

				int backColor;
				bool bold;
				int fontColor;
				std::string fontFace;
				int fontSize;
				int materialSize;
				std::string materialText;
				int textAlignment;
			};

			std::shared_ptr<Text> text;
		};

		std::unordered_map<int, Material> materials;

		struct Move
		{
			Move();

			int duration;
			std::tuple<Eigen::Vector3f, Eigen::Vector3f, Eigen::Vector3f> position;
			std::tuple<Eigen::Vector3f, Eigen::Vector3f, Eigen::Vector3f> rotation;
			float speed;
			std::chrono::steady_clock::time_point time;

			EIGEN_MAKE_ALIGNED_OPERATOR_NEW
		};

		std::shared_ptr<Move> move;

		std::unordered_set<int> areas;
		std::vector<int> extras;
		std::unordered_map<int, std::vector<int> > extraExtras;
		std::unordered_set<int> interiors;
		PlayerVisibility players;
		std::unordered_set<int> worlds;

		static Identifier identifier;

		EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	};

	struct Pickup
	{
		Pickup();

		AMX *amx;
		SharedCell cell;
		float comparableStreamDistance;
		bool inverseAreaChecking;
		int modelId;
		float originalComparableStreamDistance;
		int pickupId;
		Eigen::Vector3f position;
		Eigen::Vector3f positionOffset;
		int priority;
		bool streamCallbacks;
		float streamDistance;
		int type;

		std::unordered_set<int> areas;
		std::vector<int> extras;
		std::unordered_map<int, std::vector<int> > extraExtras;
		std::unordered_set<int> interiors;
		PlayerVisibility players;
		std::unordered_set<int> worlds;

		static Identifier identifier;

		EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	};

	struct RaceCheckpoint
	{
		RaceCheckpoint();

		AMX *amx;
		SharedCell cell;
		float comparableStreamDistance;
		bool inverseAreaChecking;
		Eigen::Vector3f next;
		float originalComparableStreamDistance;
		Eigen::Vector3f position;
		Eigen::Vector3f positionOffset;
		int priority;
		int raceCheckpointId;
		float size;
		bool streamCallbacks;
		float streamDistance;
		int type;

		std::unordered_set<int> areas;
		std::vector<int> extras;
		std::unordered_map<int, std::vector<int> > extraExtras;
		std::unordered_set<int> interiors;
		PlayerVisibility players;
		std::unordered_set<int> worlds;

		static Identifier identifier;

		EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	};

	struct TextLabel
	{
		TextLabel();

		AMX *amx;
		SharedCell cell;
		int color;
		float comparableStreamDistance;
		float drawDistance;
		bool inverseAreaChecking;
		float originalComparableStreamDistance;
		Eigen::Vector3f position;
		Eigen::Vector3f positionOffset;
		int priority;
		bool streamCallbacks;
		float streamDistance;
		bool testLOS;
		std::string text;
		int textLabelId;

		struct Attach
		{
			Attach();

			int player;
			Eigen::Vector3f position;
			int vehicle;

			std::unordered_set<int> worlds;

			EIGEN_MAKE_ALIGNED_OPERATOR_NEW
		};

		std::shared_ptr<Attach> attach;

		std::unordered_set<int> areas;
		std::vector<int> extras;
		std::unordered_map<int, std::vector<int> > extraExtras;
		std::unordered_set<int> interiors;
		PlayerVisibility players;
		std::unordered_set<int> worlds;

		static Identifier identifier;

		EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	};
}

#endif
