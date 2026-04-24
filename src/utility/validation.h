/*
 * Copyright (C) 2017 Incognito
 * Copyright (C) 2026 VS:RP fork
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef UTILITY_VALIDATION_H
#define UTILITY_VALIDATION_H

#include <cmath>
#include <cstddef>
#include <limits>

#include "../streamer_api.h"

namespace Utility
{
	void logError(const char *format, ...);
}

namespace Validation
{
	// SA-MP / GTA:SA map extent is roughly [-20000, 20000]. Any coordinate beyond
	// ~1e6 is almost certainly a bug or a malformed input; the client can choke on
	// absurd positions (crash / desync).
	constexpr float kMaxAbsCoord = 20000.0f;
	// Stream / draw radii. Anything past this is a logical error; draw distances
	// beyond ~1500 don't exist in vanilla anyway.
	constexpr float kMaxRadius = 1.0e6f;
	// Model id upper bound. Real SA-MP IDs top out around ~20000; custom content
	// servers use higher but we still cap to protect clients.
	constexpr int kMaxModelId = 1000000;
	// Virtual world / interior caps. Pawn scripts use any int; we reject only
	// values that cannot be real (excluding the -1 "all worlds" sentinel).
	constexpr int kMaxWorldId = 2000000000;
	constexpr int kMaxInteriorId = 2000000000;
	// Upper bound for string lengths we accept from scripts (SA-MP native limit
	// is 1024; 3D text labels top at ~800). Keeps `amx_SetString` safe.
	constexpr std::size_t kMaxNativeStringLen = 2048;
	// Upper bound for variadic array parameters (attached offsets, polygons).
	constexpr std::size_t kMaxNativeArrayLen = 100000;

	inline bool isFinitef(float f) noexcept { return std::isfinite(f); }

	inline bool isFiniteVec3(float x, float y, float z) noexcept
	{
		return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
	}

	inline bool isCoordInRange(float c) noexcept
	{
		return std::isfinite(c) && std::fabs(c) <= kMaxAbsCoord;
	}

	inline bool isCoordVec3(float x, float y, float z) noexcept
	{
		return isCoordInRange(x) && isCoordInRange(y) && isCoordInRange(z);
	}

	// Rotation in degrees — allow arbitrary degrees but reject NaN/Inf.
	inline bool isRotationVec3(float x, float y, float z) noexcept
	{
		return isFiniteVec3(x, y, z);
	}

	// Player filter: -1 == broadcast to all, otherwise must be a valid slot.
	inline bool isPlayerIdOrAll(int id) noexcept
	{
		return id == -1 || (id >= 0 && id < MAX_PLAYERS);
	}

	// World filter: -1 == all worlds, otherwise must be a reasonable non-negative int.
	inline bool isWorldIdOrAll(int id) noexcept
	{
		return id == -1 || (id >= 0 && id <= kMaxWorldId);
	}

	inline bool isInteriorIdOrAll(int id) noexcept
	{
		return id == -1 || (id >= 0 && id <= kMaxInteriorId);
	}

	inline bool isModelId(int id) noexcept
	{
		return id >= 0 && id <= kMaxModelId;
	}

	// Stream distance: -1.0 signals static/infinite; otherwise must be finite and non-negative.
	inline bool isStreamDistance(float d) noexcept
	{
		if (!std::isfinite(d))
			return false;
		if (d < 0.0f)
			return true; // static sentinel (< STREAMER_STATIC_DISTANCE_CUTOFF)
		return d <= kMaxRadius;
	}

	// Draw distance: must be finite, non-negative, and within a sane cap.
	inline bool isDrawDistance(float d) noexcept
	{
		return std::isfinite(d) && d >= 0.0f && d <= kMaxRadius;
	}

	// Generic positive radius (checkpoints, areas).
	inline bool isRadius(float r) noexcept
	{
		return std::isfinite(r) && r >= 0.0f && r <= kMaxRadius;
	}

	inline bool isArraySize(int size) noexcept
	{
		return size >= 0 && static_cast<std::size_t>(size) <= kMaxNativeArrayLen;
	}

	// Polygon points come in pairs (x,y); array must hold an even count.
	inline bool isPolygonArraySize(int size) noexcept
	{
		return isArraySize(size) && (size % 2) == 0 && size >= 6; // at least triangle
	}
}

// Per-param validation macros; format matches CHECK_PARAMS in natives.h.
// Each macro logs via Utility::logError and returns 0 from the calling native.

#define CHECK_POS_VEC3(x, y, z) \
	do { \
		if (!Validation::isCoordVec3((x), (y), (z))) \
		{ \
			Utility::logError("%s: invalid position (%f, %f, %f) — NaN/Inf/out of range.", __func__, (x), (y), (z)); \
			return 0; \
		} \
	} while (0)

#define CHECK_FINITE(f, label) \
	do { \
		if (!Validation::isFinitef(f)) \
		{ \
			Utility::logError("%s: %s is NaN or infinite.", __func__, (label)); \
			return 0; \
		} \
	} while (0)

#define CHECK_ROT_VEC3(x, y, z) \
	do { \
		if (!Validation::isRotationVec3((x), (y), (z))) \
		{ \
			Utility::logError("%s: rotation contains NaN/Inf.", __func__); \
			return 0; \
		} \
	} while (0)

#define CHECK_PLAYER_ID_OR_ALL(id) \
	do { \
		if (!Validation::isPlayerIdOrAll(static_cast<int>(id))) \
		{ \
			Utility::logError("%s: player id %d out of range [-1, %d).", __func__, static_cast<int>(id), MAX_PLAYERS); \
			return 0; \
		} \
	} while (0)

#define CHECK_WORLD_ID_OR_ALL(id) \
	do { \
		if (!Validation::isWorldIdOrAll(static_cast<int>(id))) \
		{ \
			Utility::logError("%s: world id %d out of range.", __func__, static_cast<int>(id)); \
			return 0; \
		} \
	} while (0)

#define CHECK_INTERIOR_ID_OR_ALL(id) \
	do { \
		if (!Validation::isInteriorIdOrAll(static_cast<int>(id))) \
		{ \
			Utility::logError("%s: interior id %d out of range.", __func__, static_cast<int>(id)); \
			return 0; \
		} \
	} while (0)

#define CHECK_MODEL_ID(id) \
	do { \
		if (!Validation::isModelId(static_cast<int>(id))) \
		{ \
			Utility::logError("%s: invalid model id %d.", __func__, static_cast<int>(id)); \
			return 0; \
		} \
	} while (0)

#define CHECK_STREAM_DISTANCE(d) \
	do { \
		if (!Validation::isStreamDistance(d)) \
		{ \
			Utility::logError("%s: invalid stream distance %f.", __func__, (d)); \
			return 0; \
		} \
	} while (0)

#define CHECK_DRAW_DISTANCE(d) \
	do { \
		if (!Validation::isDrawDistance(d)) \
		{ \
			Utility::logError("%s: invalid draw distance %f.", __func__, (d)); \
			return 0; \
		} \
	} while (0)

#define CHECK_RADIUS(r) \
	do { \
		if (!Validation::isRadius(r)) \
		{ \
			Utility::logError("%s: invalid radius %f.", __func__, (r)); \
			return 0; \
		} \
	} while (0)

#define CHECK_ARRAY_SIZE(n) \
	do { \
		if (!Validation::isArraySize(static_cast<int>(n))) \
		{ \
			Utility::logError("%s: array size %d out of range.", __func__, static_cast<int>(n)); \
			return 0; \
		} \
	} while (0)

#define CHECK_POLYGON_ARRAY_SIZE(n) \
	do { \
		if (!Validation::isPolygonArraySize(static_cast<int>(n))) \
		{ \
			Utility::logError("%s: polygon array size %d invalid (must be even and >= 6).", __func__, static_cast<int>(n)); \
			return 0; \
		} \
	} while (0)

#endif
