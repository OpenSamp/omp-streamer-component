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

// Implementation of StreamerApi:: — the streamer's internal wrapper API that used to be the
// sampgdk amalgamation. Every function here routes directly to an open.mp component method.
// Anything still marked as a one-shot warning stub is a path the streamer never hits with the
// current gamemodes; if one starts firing, port it here.

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

#include <core.hpp>
#include <player.hpp>
#include <Server/Components/Actors/actors.hpp>
#include <Server/Components/Checkpoints/checkpoints.hpp>
#include <Server/Components/Objects/objects.hpp>
#include <Server/Components/Pickups/pickups.hpp>
#include <Server/Components/TextLabels/textlabels.hpp>
#include <Server/Components/Vehicles/vehicles.hpp>

#include "streamer_api.h"
#include "openmp_component.h"

namespace
{
	// One-shot warning per unique function name so logs don't flood.
	std::unordered_set<std::string> g_reportedStubs;

	void reportStub(const char *name)
	{
		if (g_reportedStubs.insert(name).second)
		{
			ICore *c = StreamerRuntime::core();
			if (c)
			{
				c->logLn(LogLevel::Warning, "streamer: %s has no open.mp equivalent wired up yet", name);
			}
		}
	}

	IPlayer *resolvePlayer(int id)
	{
		ICore *c = StreamerRuntime::core();
		if (!c)
		{
			return nullptr;
		}
		return c->getPlayers().get(id);
	}

	IVehicle *resolveVehicle(int id)
	{
		IVehiclesComponent *v = StreamerRuntime::vehicles();
		if (!v)
		{
			return nullptr;
		}
		return v->get(id);
	}

	void writeVec3(Vector3 v, float *x, float *y, float *z)
	{
		if (x) *x = v.x;
		if (y) *y = v.y;
		if (z) *z = v.z;
	}

	// Helper for the `if (!entity) { zero-out outputs; return false; }` pattern that shows up
	// across every getter.
	bool zeroAndFail(float *x, float *y, float *z)
	{
		writeVec3({}, x, y, z);
		return false;
	}

	IPlayerObjectData *playerObjects(int playerid)
	{
		IPlayer *p = resolvePlayer(playerid);
		return p ? queryExtension<IPlayerObjectData>(p) : nullptr;
	}

	IPlayerObject *playerObject(int playerid, int objectid)
	{
		IPlayerObjectData *data = playerObjects(playerid);
		return data ? data->get(objectid) : nullptr;
	}

	IObject *globalObject(int objectid)
	{
		IObjectsComponent *oc = StreamerRuntime::objects();
		return oc ? oc->get(objectid) : nullptr;
	}

	IActor *resolveActor(int actorid)
	{
		IActorsComponent *ac = StreamerRuntime::actors();
		return ac ? ac->get(actorid) : nullptr;
	}
}

namespace StreamerApi
{
	void logprintf(const char *format, ...)
	{
		char buffer[2048];
		va_list args;
		va_start(args, format);
		std::vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);

		ICore *c = StreamerRuntime::core();
		if (c)
		{
			c->printLn("%s", buffer);
		}
		else
		{
			std::fputs(buffer, stdout);
			std::fputc('\n', stdout);
		}
	}

	namespace
	{
		// Sentinel natives used by the legacy StreamerApi::FindNative + StreamerApi::InvokeNative
		// invocation path that the streamer still uses for a handful of calls. Under open.mp
		// we don't actually run pawn natives here — the sentinel pointer just identifies which
		// operation the caller wants, and InvokeNative dispatches to the direct open.mp wrapper.
		cell AMX_NATIVE_CALL nativeAttachObjectToObject(AMX *, cell *) { return 0; }
		cell AMX_NATIVE_CALL nativeAttachObjectToPlayer(AMX *, cell *) { return 0; }
		// "SetPlayerGravity" is only ever FindNative'd as a YSF-presence check; the streamer
		// never InvokeNative's it, so this sentinel just needs to be non-null.
		cell AMX_NATIVE_CALL nativeYsfCapabilityMarker(AMX *, cell *) { return 1; }
	}

	AMX_NATIVE FindNative(const char *name)
	{
		if (!name) return nullptr;
		if (std::strcmp(name, "AttachPlayerObjectToObject") == 0) return &nativeAttachObjectToObject;
		if (std::strcmp(name, "AttachPlayerObjectToPlayer") == 0) return &nativeAttachObjectToPlayer;
		if (std::strcmp(name, "SetPlayerGravity") == 0) return &nativeYsfCapabilityMarker;
		reportStub("FindNative");
		return nullptr;
	}

	cell InvokeNative(AMX_NATIVE native, const char *format, ...)
	{
		va_list args;
		va_start(args, format);
		cell result = 0;

		if (native == &nativeAttachObjectToObject)
		{
			// "dddffffffb": int, int, int, float x6, bool
			int playerid = va_arg(args, int);
			int objectid = va_arg(args, int);
			int attachId = va_arg(args, int);
			float ox = static_cast<float>(va_arg(args, double));
			float oy = static_cast<float>(va_arg(args, double));
			float oz = static_cast<float>(va_arg(args, double));
			float rx = static_cast<float>(va_arg(args, double));
			float ry = static_cast<float>(va_arg(args, double));
			float rz = static_cast<float>(va_arg(args, double));
			int sync = va_arg(args, int); // bool promoted to int
			result = AttachPlayerObjectToObject(playerid, objectid, attachId, ox, oy, oz, rx, ry, rz, sync != 0) ? 1 : 0;
		}
		else if (native == &nativeAttachObjectToPlayer)
		{
			// "dddffffffd": int, int, int, float x6, int (sync flag, discarded under open.mp)
			int playerid = va_arg(args, int);
			int objectid = va_arg(args, int);
			int attachPlayer = va_arg(args, int);
			float ox = static_cast<float>(va_arg(args, double));
			float oy = static_cast<float>(va_arg(args, double));
			float oz = static_cast<float>(va_arg(args, double));
			float rx = static_cast<float>(va_arg(args, double));
			float ry = static_cast<float>(va_arg(args, double));
			float rz = static_cast<float>(va_arg(args, double));
			(void)va_arg(args, int);
			result = AttachPlayerObjectToPlayer(playerid, objectid, attachPlayer, ox, oy, oz, rx, ry, rz) ? 1 : 0;
		}
		else
		{
			reportStub("InvokeNative");
		}

		va_end(args);
		return result;
	}

	// Actors
	int CreateActor(int modelid, float x, float y, float z, float rotation)
	{
		IActorsComponent *ac = StreamerRuntime::actors();
		if (!ac) return INVALID_ACTOR_ID;
		IActor *actor = ac->create(modelid, Vector3(x, y, z), rotation);
		return actor ? actor->getID() : INVALID_ACTOR_ID;
	}

	bool DestroyActor(int actorid)
	{
		IActorsComponent *ac = StreamerRuntime::actors();
		if (!ac) return false;
		ac->release(actorid);
		return true;
	}

	bool IsValidActor(int actorid)
	{
		return resolveActor(actorid) != nullptr;
	}

	bool IsActorStreamedIn(int actorid, int forplayerid)
	{
		IActor *a = resolveActor(actorid);
		IPlayer *p = resolvePlayer(forplayerid);
		return a && p && a->isStreamedInForPlayer(*p);
	}

	bool SetActorVirtualWorld(int actorid, int vworld)
	{
		IActor *a = resolveActor(actorid);
		if (!a) return false;
		a->setVirtualWorld(vworld);
		return true;
	}

	bool ApplyActorAnimation(int actorid, const char *animlib, const char *animname, float delta, bool loop, bool lockx, bool locky, bool freeze, int time)
	{
		IActor *a = resolveActor(actorid);
		if (!a) return false;
		AnimationData anim(delta, loop, lockx, locky, freeze, static_cast<uint32_t>(time),
			StringView(animlib ? animlib : ""), StringView(animname ? animname : ""));
		a->applyAnimation(anim);
		return true;
	}

	bool ClearActorAnimations(int actorid)
	{
		IActor *a = resolveActor(actorid);
		if (!a) return false;
		a->clearAnimations();
		return true;
	}

	bool SetActorPos(int actorid, float x, float y, float z)
	{
		IActor *a = resolveActor(actorid);
		if (!a) return false;
		a->setPosition(Vector3(x, y, z));
		return true;
	}

	bool SetActorHealth(int actorid, float health)
	{
		IActor *a = resolveActor(actorid);
		if (!a) return false;
		a->setHealth(health);
		return true;
	}

	bool SetActorInvulnerable(int actorid, bool invulnerable)
	{
		IActor *a = resolveActor(actorid);
		if (!a) return false;
		a->setInvulnerable(invulnerable);
		return true;
	}

	// Pickups
	int CreatePickup(int model, int type, float x, float y, float z, int virtualworld)
	{
		IPickupsComponent *pc = StreamerRuntime::pickups();
		if (!pc) { reportStub("CreatePickup"); return 0; }
		IPickup *pickup = pc->create(model, static_cast<PickupType>(type), Vector3(x, y, z), static_cast<uint32_t>(virtualworld), false);
		return pickup ? pickup->getID() : 0;
	}

	bool DestroyPickup(int pickupid)
	{
		IPickupsComponent *pc = StreamerRuntime::pickups();
		if (!pc) { reportStub("DestroyPickup"); return false; }
		pc->release(pickupid);
		return true;
	}

	// Player objects
	int CreatePlayerObject(int playerid, int modelid, float x, float y, float z, float rX, float rY, float rZ, float drawDistance)
	{
		IPlayerObjectData *data = playerObjects(playerid);
		if (!data) return INVALID_OBJECT_ID;
		IPlayerObject *obj = data->create(modelid, Vector3(x, y, z), Vector3(rX, rY, rZ), drawDistance);
		return obj ? obj->getID() : INVALID_OBJECT_ID;
	}

	bool DestroyPlayerObject(int playerid, int objectid)
	{
		IPlayerObjectData *data = playerObjects(playerid);
		if (!data) return false;
		data->release(objectid);
		return true;
	}

	bool MovePlayerObject(int playerid, int objectid, float x, float y, float z, float speed, float rX, float rY, float rZ)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		if (!obj) return false;
		ObjectMoveData m;
		m.targetPos = Vector3(x, y, z);
		m.targetRot = Vector3(rX, rY, rZ);
		m.speed = speed;
		obj->move(m);
		return true;
	}

	bool StopPlayerObject(int playerid, int objectid)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		if (!obj) return false;
		obj->stop();
		return true;
	}

	bool SetPlayerObjectPos(int playerid, int objectid, float x, float y, float z)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		if (!obj) return false;
		obj->setPosition(Vector3(x, y, z));
		return true;
	}

	bool GetPlayerObjectPos(int playerid, int objectid, float *x, float *y, float *z)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		if (!obj) { return zeroAndFail(x, y, z); }
		writeVec3(obj->getPosition(), x, y, z);
		return true;
	}

	bool SetPlayerObjectRot(int playerid, int objectid, float rX, float rY, float rZ)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		if (!obj) return false;
		obj->setRotation(GTAQuat(rX, rY, rZ));
		return true;
	}

	bool GetPlayerObjectRot(int playerid, int objectid, float *rX, float *rY, float *rZ)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		if (!obj) { return zeroAndFail(rX, rY, rZ); }
		writeVec3(obj->getRotation().ToEuler(), rX, rY, rZ);
		return true;
	}

	bool GetObjectPos(int objectid, float *x, float *y, float *z)
	{
		IObject *obj = globalObject(objectid);
		if (!obj) { return zeroAndFail(x, y, z); }
		writeVec3(obj->getPosition(), x, y, z);
		return true;
	}

	bool GetObjectRot(int objectid, float *rX, float *rY, float *rZ)
	{
		IObject *obj = globalObject(objectid);
		if (!obj) { return zeroAndFail(rX, rY, rZ); }
		writeVec3(obj->getRotation().ToEuler(), rX, rY, rZ);
		return true;
	}

	bool SetPlayerObjectNoCameraCol(int playerid, int objectid)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		if (!obj) return false;
		obj->setCameraCollision(false);
		return true;
	}

	bool SetPlayerObjectMaterial(int playerid, int objectid, int materialindex, int modelid, const char *txdname, const char *texturename, int materialcolor)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		if (!obj) return false;
		obj->setMaterial(static_cast<uint32_t>(materialindex), modelid, StringView(txdname ? txdname : ""), StringView(texturename ? texturename : ""), Colour::FromARGB(static_cast<uint32_t>(materialcolor)));
		return true;
	}

	bool SetPlayerObjectMaterialText(int playerid, int objectid, const char *text, int materialindex, int materialsize, const char *fontface, int fontsize, bool bold, int fontcolor, int backcolor, int textalignment)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		if (!obj) return false;
		obj->setMaterialText(static_cast<uint32_t>(materialindex),
			StringView(text ? text : ""),
			static_cast<ObjectMaterialSize>(materialsize),
			StringView(fontface ? fontface : ""),
			fontsize,
			bold,
			Colour::FromARGB(static_cast<uint32_t>(fontcolor)),
			Colour::FromARGB(static_cast<uint32_t>(backcolor)),
			static_cast<ObjectMaterialTextAlign>(textalignment));
		return true;
	}

	bool AttachPlayerObjectToVehicle(int playerid, int objectid, int vehicleid, float ox, float oy, float oz, float rX, float rY, float rZ)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		IVehicle *veh = resolveVehicle(vehicleid);
		if (!obj || !veh) return false;
		obj->attachToVehicle(*veh, Vector3(ox, oy, oz), Vector3(rX, rY, rZ));
		return true;
	}

	bool AttachPlayerObjectToObject(int playerid, int objectid, int attachToObjectId, float ox, float oy, float oz, float rX, float rY, float rZ, bool /*syncRotation*/)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		IPlayerObject *base = playerObject(playerid, attachToObjectId);
		if (!obj || !base) return false;
		// open.mp's IPlayerObject::attachToObject has no separate syncRotation parameter;
		// rotations always follow the attachment target.
		obj->attachToObject(*base, Vector3(ox, oy, oz), Vector3(rX, rY, rZ));
		return true;
	}

	bool AttachPlayerObjectToPlayer(int playerid, int objectid, int attachToPlayerId, float ox, float oy, float oz, float rX, float rY, float rZ)
	{
		IPlayerObject *obj = playerObject(playerid, objectid);
		IPlayer *target = resolvePlayer(attachToPlayerId);
		if (!obj || !target) return false;
		obj->attachToPlayer(*target, Vector3(ox, oy, oz), Vector3(rX, rY, rZ));
		return true;
	}

	bool AttachCameraToPlayerObject(int playerid, int objectid)
	{
		IPlayer *p = resolvePlayer(playerid);
		IPlayerObject *obj = playerObject(playerid, objectid);
		if (!p || !obj) return false;
		p->attachCameraToObject(*obj);
		return true;
	}

	bool EditPlayerObject(int playerid, int objectid)
	{
		IPlayerObjectData *data = playerObjects(playerid);
		IPlayerObject *obj = playerObject(playerid, objectid);
		if (!data || !obj) return false;
		data->beginEditing(*obj);
		return true;
	}

	int GetPlayerCameraTargetObject(int playerid)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return INVALID_OBJECT_ID;
		IObject *obj = p->getCameraTargetObject();
		return obj ? obj->getID() : INVALID_OBJECT_ID;
	}

	int GetPlayerCameraTargetActor(int playerid)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return INVALID_ACTOR_ID;
		IActor *actor = p->getCameraTargetActor();
		return actor ? actor->getID() : INVALID_ACTOR_ID;
	}

	int GetPlayerTargetActor(int playerid)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return INVALID_ACTOR_ID;
		IActor *actor = p->getTargetActor();
		return actor ? actor->getID() : INVALID_ACTOR_ID;
	}

	// Checkpoints
	bool SetPlayerCheckpoint(int playerid, float x, float y, float z, float size)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return false;
		IPlayerCheckpointData *data = queryExtension<IPlayerCheckpointData>(p);
		if (!data) return false;
		ICheckpointData &cp = data->getCheckpoint();
		cp.setPosition(Vector3(x, y, z));
		cp.setRadius(size);
		cp.enable();
		return true;
	}

	bool DisablePlayerCheckpoint(int playerid)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return false;
		IPlayerCheckpointData *data = queryExtension<IPlayerCheckpointData>(p);
		if (!data) return false;
		data->getCheckpoint().disable();
		return true;
	}

	bool SetPlayerRaceCheckpoint(int playerid, int type, float x, float y, float z, float nextx, float nexty, float nextz, float size)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return false;
		IPlayerCheckpointData *data = queryExtension<IPlayerCheckpointData>(p);
		if (!data) return false;
		IRaceCheckpointData &cp = data->getRaceCheckpoint();
		cp.setType(static_cast<RaceCheckpointType>(type));
		cp.setPosition(Vector3(x, y, z));
		cp.setNextPosition(Vector3(nextx, nexty, nextz));
		cp.setRadius(size);
		cp.enable();
		return true;
	}

	bool DisablePlayerRaceCheckpoint(int playerid)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return false;
		IPlayerCheckpointData *data = queryExtension<IPlayerCheckpointData>(p);
		if (!data) return false;
		data->getRaceCheckpoint().disable();
		return true;
	}

	// Map icons
	bool SetPlayerMapIcon(int playerid, int iconid, float x, float y, float z, int markertype, int color, int style)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return false;
		p->setMapIcon(iconid, Vector3(x, y, z), markertype, Colour::FromRGBA(static_cast<uint32_t>(color)), static_cast<MapIconStyle>(style));
		return true;
	}

	bool RemovePlayerMapIcon(int playerid, int iconid)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return false;
		p->unsetMapIcon(iconid);
		return true;
	}

	// Player getters / setters
	bool GetPlayerPos(int id, float *x, float *y, float *z)
	{
		IPlayer *p = resolvePlayer(id);
		if (!p) { return zeroAndFail(x, y, z); }
		writeVec3(p->getPosition(), x, y, z);
		return true;
	}

	bool SetPlayerPos(int id, float x, float y, float z)
	{
		IPlayer *p = resolvePlayer(id);
		if (!p) return false;
		p->setPosition(Vector3(x, y, z));
		return true;
	}

	bool GetPlayerCameraPos(int id, float *x, float *y, float *z)
	{
		IPlayer *p = resolvePlayer(id);
		if (!p) { return zeroAndFail(x, y, z); }
		writeVec3(p->getCameraPosition(), x, y, z);
		return true;
	}

	bool GetPlayerFacingAngle(int id, float *angle)
	{
		IPlayer *p = resolvePlayer(id);
		if (!p) { if (angle) *angle = 0; return false; }
		if (angle) *angle = p->getRotation().ToEuler().z;
		return true;
	}

	bool GetPlayerVelocity(int id, float *x, float *y, float *z)
	{
		IPlayer *p = resolvePlayer(id);
		if (!p) { return zeroAndFail(x, y, z); }
		writeVec3(p->getVelocity(), x, y, z);
		return true;
	}

	int GetPlayerInterior(int id)
	{
		IPlayer *p = resolvePlayer(id);
		return p ? static_cast<int>(p->getInterior()) : 0;
	}

	int GetPlayerVirtualWorld(int id)
	{
		IPlayer *p = resolvePlayer(id);
		return p ? p->getVirtualWorld() : 0;
	}

	int GetPlayerState(int id)
	{
		IPlayer *p = resolvePlayer(id);
		return p ? static_cast<int>(p->getState()) : PlayerState_None;
	}

	int GetPlayerVehicleID(int id)
	{
		IPlayer *p = resolvePlayer(id);
		if (!p) return 0;
		IPlayerVehicleData *data = queryExtension<IPlayerVehicleData>(p);
		if (!data) return 0;
		IVehicle *v = data->getVehicle();
		return v ? v->getID() : 0;
	}

	bool IsPlayerInAnyVehicle(int id)
	{
		IPlayer *p = resolvePlayer(id);
		if (!p) return false;
		IPlayerVehicleData *data = queryExtension<IPlayerVehicleData>(p);
		return data && data->getVehicle() != nullptr;
	}

	bool IsPlayerNPC(int id)
	{
		IPlayer *p = resolvePlayer(id);
		return p && p->isBot();
	}

	bool TogglePlayerControllable(int id, bool toggle)
	{
		IPlayer *p = resolvePlayer(id);
		if (!p) return false;
		p->setControllable(toggle);
		return true;
	}

	// Vehicle getters
	bool GetVehiclePos(int id, float *x, float *y, float *z)
	{
		IVehicle *v = resolveVehicle(id);
		if (!v) { return zeroAndFail(x, y, z); }
		writeVec3(v->getPosition(), x, y, z);
		return true;
	}

	bool GetVehicleRotationQuat(int id, float *w, float *x, float *y, float *z)
	{
		IVehicle *v = resolveVehicle(id);
		if (!v)
		{
			if (w) *w = 1; if (x) *x = 0; if (y) *y = 0; if (z) *z = 0;
			return false;
		}
		GTAQuat q = v->getRotation();
		if (w) *w = q.q.w;
		if (x) *x = q.q.x;
		if (y) *y = q.q.y;
		if (z) *z = q.q.z;
		return true;
	}

	bool GetVehicleVelocity(int id, float *X, float *Y, float *Z)
	{
		IVehicle *v = resolveVehicle(id);
		if (!v) { return zeroAndFail(X, Y, Z); }
		writeVec3(v->getVelocity(), X, Y, Z);
		return true;
	}

	int GetVehicleVirtualWorld(int id)
	{
		IVehicle *v = resolveVehicle(id);
		return v ? v->getVirtualWorld() : 0;
	}

	bool GetVehicleZAngle(int id, float *z_angle)
	{
		IVehicle *v = resolveVehicle(id);
		if (!v) { if (z_angle) *z_angle = 0; return false; }
		if (z_angle) *z_angle = v->getZAngle();
		return true;
	}

	int CreatePlayer3DTextLabel(int playerid, const char *text, int color, float x, float y, float z, float drawDistance, int attachedplayer, int attachedvehicle, bool testLOS)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return INVALID_3DTEXT_ID;
		IPlayerTextLabelData *data = queryExtension<IPlayerTextLabelData>(p);
		if (!data) return INVALID_3DTEXT_ID;

		StringView txt(text ? text : "");
		Colour c = Colour::FromRGBA(static_cast<uint32_t>(color));
		Vector3 pos(x, y, z);
		IPlayerTextLabel *label = nullptr;

		if (attachedplayer != INVALID_PLAYER_ID)
		{
			IPlayer *att = resolvePlayer(attachedplayer);
			if (att) label = data->create(txt, c, pos, drawDistance, testLOS, *att);
		}
		else if (attachedvehicle != INVALID_VEHICLE_ID)
		{
			IVehicle *veh = resolveVehicle(attachedvehicle);
			if (veh) label = data->create(txt, c, pos, drawDistance, testLOS, *veh);
		}
		if (!label)
		{
			label = data->create(txt, c, pos, drawDistance, testLOS);
		}
		return label ? label->getID() : INVALID_3DTEXT_ID;
	}

	bool DeletePlayer3DTextLabel(int playerid, int id)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return false;
		IPlayerTextLabelData *data = queryExtension<IPlayerTextLabelData>(p);
		if (!data) return false;
		data->release(id);
		return true;
	}

	bool UpdatePlayer3DTextLabelText(int playerid, int id, int color, const char *text)
	{
		IPlayer *p = resolvePlayer(playerid);
		if (!p) return false;
		IPlayerTextLabelData *data = queryExtension<IPlayerTextLabelData>(p);
		if (!data) return false;
		IPlayerTextLabel *label = data->get(id);
		if (!label) return false;
		label->setColourAndText(Colour::FromRGBA(static_cast<uint32_t>(color)), StringView(text ? text : ""));
		return true;
	}
}
