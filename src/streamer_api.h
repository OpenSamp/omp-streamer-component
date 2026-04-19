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

// Internal wrapper API used by the streamer to reach open.mp component calls.
// Historically this header was sampgdk.h (the 17k-line SA-MP amalgamation) and the streamer
// sources were written against its functions. We keep the same function signatures and names
// so the rest of the codebase stays untouched, but every call now routes through open.mp's
// IPlayer/IObjectsComponent/IPickupsComponent/etc. The implementation lives in streamer_api.cpp.

#ifndef STREAMER_API_H
#define STREAMER_API_H

#include <cstddef>
#include <amx/amx.h>

// SA-MP-named aliases for values the streamer's SA-MP-era code references. IDs that also exist
// in open.mp's values.hpp (INVALID_PLAYER_ID/VEHICLE_ID/OBJECT_ID/ACTOR_ID) are pulled straight
// from there via main.h — we no longer shadow them with #defines, which used to clash on any
// translation unit that also included <sdk.hpp>.

constexpr int MAX_PLAYERS = 1000;

constexpr int EDIT_RESPONSE_CANCEL = 0;
constexpr int EDIT_RESPONSE_FINAL  = 1;
constexpr int EDIT_RESPONSE_UPDATE = 2;

constexpr int SELECT_OBJECT_GLOBAL_OBJECT = 1;
constexpr int SELECT_OBJECT_PLAYER_OBJECT = 2;

constexpr int BULLET_HIT_TYPE_NONE          = 0;
constexpr int BULLET_HIT_TYPE_PLAYER        = 1;
constexpr int BULLET_HIT_TYPE_VEHICLE       = 2;
constexpr int BULLET_HIT_TYPE_OBJECT        = 3;
constexpr int BULLET_HIT_TYPE_PLAYER_OBJECT = 4;

// open.mp's values.hpp exposes INVALID_TEXT_LABEL_ID; the streamer code uses the SA-MP name.
constexpr int INVALID_3DTEXT_ID = 0xFFFF;

// Match open.mp's PlayerState enum (player.hpp).
constexpr int PLAYER_STATE_NONE       = 0;
constexpr int PLAYER_STATE_ONFOOT     = 1;
constexpr int PLAYER_STATE_DRIVER     = 2;
constexpr int PLAYER_STATE_PASSENGER  = 3;
constexpr int PLAYER_STATE_WASTED     = 7;
constexpr int PLAYER_STATE_SPAWNED    = 8;
constexpr int PLAYER_STATE_SPECTATING = 9;

namespace StreamerApi
{
	// --- Logging (implemented, routes to ICore::printLn) ---------------------------------------
	void logprintf(const char *format, ...);

	// --- AMX native invocation (Stage 1 stubs; Stage 2 will route to IPawnScript::CallNative) ---
	AMX_NATIVE FindNative(const char *name);
	cell InvokeNative(AMX_NATIVE native, const char *format, ...);

	// --- SA-MP natives used by the streamer ----------------------------------------------------
	// All stubbed in Stage 1: return safe defaults and log a one-shot TODO per function.

	// Actors
	int  CreateActor(int modelid, float x, float y, float z, float rotation);
	bool DestroyActor(int actorid);
	bool IsValidActor(int actorid);
	bool IsActorStreamedIn(int actorid, int forplayerid);
	bool SetActorVirtualWorld(int actorid, int vworld);
	bool ApplyActorAnimation(int actorid, const char *animlib, const char *animname, float fDelta, bool loop, bool lockx, bool locky, bool freeze, int time);
	bool ClearActorAnimations(int actorid);
	bool SetActorPos(int actorid, float x, float y, float z);
	bool SetActorHealth(int actorid, float health);
	bool SetActorInvulnerable(int actorid, bool invulnerable);

	// Pickups
	int  CreatePickup(int model, int type, float x, float y, float z, int virtualworld);
	bool DestroyPickup(int pickup);

	// Player objects (dynamic objects are materialised as per-player player objects)
	int  CreatePlayerObject(int playerid, int modelid, float x, float y, float z, float rX, float rY, float rZ, float DrawDistance);
	bool DestroyPlayerObject(int playerid, int objectid);
	bool MovePlayerObject(int playerid, int objectid, float x, float y, float z, float Speed, float RotX, float RotY, float RotZ);
	bool StopPlayerObject(int playerid, int objectid);
	bool SetPlayerObjectPos(int playerid, int objectid, float x, float y, float z);
	bool GetPlayerObjectPos(int playerid, int objectid, float *x, float *y, float *z);
	bool SetPlayerObjectRot(int playerid, int objectid, float rX, float rY, float rZ);
	bool GetPlayerObjectRot(int playerid, int objectid, float *rX, float *rY, float *rZ);
	bool GetObjectPos(int objectid, float *x, float *y, float *z);
	bool GetObjectRot(int objectid, float *rX, float *rY, float *rZ);
	bool SetPlayerObjectNoCameraCol(int playerid, int objectid);
	bool SetPlayerObjectMaterial(int playerid, int objectid, int materialindex, int modelid, const char *txdname, const char *texturename, int materialcolor);
	bool SetPlayerObjectMaterialText(int playerid, int objectid, const char *text, int materialindex, int materialsize, const char *fontface, int fontsize, bool bold, int fontcolor, int backcolor, int textalignment);
	bool AttachPlayerObjectToVehicle(int playerid, int objectid, int vehicleid, float fOffsetX, float fOffsetY, float fOffsetZ, float fRotX, float fRotY, float fRotZ);
	bool AttachPlayerObjectToObject(int playerid, int objectid, int attachToObjectId, float fOffsetX, float fOffsetY, float fOffsetZ, float fRotX, float fRotY, float fRotZ, bool syncRotation);
	bool AttachPlayerObjectToPlayer(int playerid, int objectid, int attachToPlayerId, float fOffsetX, float fOffsetY, float fOffsetZ, float fRotX, float fRotY, float fRotZ);
	bool AttachCameraToPlayerObject(int playerid, int objectid);
	bool EditPlayerObject(int playerid, int objectid);
	int  GetPlayerCameraTargetObject(int playerid);
	int  GetPlayerCameraTargetActor(int playerid);
	int  GetPlayerTargetActor(int playerid);

	// Checkpoints
	bool SetPlayerCheckpoint(int playerid, float x, float y, float z, float size);
	bool DisablePlayerCheckpoint(int playerid);
	bool SetPlayerRaceCheckpoint(int playerid, int type, float x, float y, float z, float nextx, float nexty, float nextz, float size);
	bool DisablePlayerRaceCheckpoint(int playerid);

	// Map icons
	bool SetPlayerMapIcon(int playerid, int iconid, float x, float y, float z, int markertype, int color, int style);
	bool RemovePlayerMapIcon(int playerid, int iconid);

	// Player getters
	bool GetPlayerPos(int playerid, float *x, float *y, float *z);
	bool SetPlayerPos(int playerid, float x, float y, float z);
	bool GetPlayerCameraPos(int playerid, float *x, float *y, float *z);
	bool GetPlayerFacingAngle(int playerid, float *angle);
	bool GetPlayerVelocity(int playerid, float *x, float *y, float *z);
	int  GetPlayerInterior(int playerid);
	int  GetPlayerVirtualWorld(int playerid);
	int  GetPlayerState(int playerid);
	int  GetPlayerVehicleID(int playerid);
	bool IsPlayerInAnyVehicle(int playerid);
	bool IsPlayerNPC(int playerid);
	bool TogglePlayerControllable(int playerid, bool toggle);

	// Vehicles
	bool GetVehiclePos(int vehicleid, float *x, float *y, float *z);
	bool GetVehicleRotationQuat(int vehicleid, float *w, float *x, float *y, float *z);
	bool GetVehicleVelocity(int vehicleid, float *X, float *Y, float *Z);
	int  GetVehicleVirtualWorld(int vehicleid);
	bool GetVehicleZAngle(int vehicleid, float *z_angle);

	// 3D text labels
	int  CreatePlayer3DTextLabel(int playerid, const char *text, int color, float x, float y, float z, float drawDistance, int attachedplayer, int attachedvehicle, bool testLOS);
	bool DeletePlayer3DTextLabel(int playerid, int id);
	bool UpdatePlayer3DTextLabelText(int playerid, int id, int color, const char *text);
}

#endif // STREAMER_API_H
