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

#ifndef STREAMER_CALLBACKS_H
#define STREAMER_CALLBACKS_H

// Internal callbacks implemented in callbacks.cpp.
// Routed from the open.mp event dispatchers in openmp_component.cpp.
bool Streamer_OnPlayerConnect(int playerid);
bool Streamer_OnPlayerDisconnect(int playerid, int reason);
bool Streamer_OnPlayerSpawn(int playerid);
bool Streamer_OnPlayerRequestClass(int playerid, int classid);
bool Streamer_OnPlayerEnterCheckpoint(int playerid);
bool Streamer_OnPlayerLeaveCheckpoint(int playerid);
bool Streamer_OnPlayerEnterRaceCheckpoint(int playerid);
bool Streamer_OnPlayerLeaveRaceCheckpoint(int playerid);
bool Streamer_OnPlayerPickUpPickup(int playerid, int pickupid);
bool Streamer_OnPlayerEditObject(int playerid, bool playerobject, int objectid, int response, float fX, float fY, float fZ, float fRotX, float fRotY, float fRotZ);
bool Streamer_OnPlayerSelectObject(int playerid, int type, int objectid, int modelid, float x, float y, float z);
bool Streamer_OnPlayerWeaponShot(int playerid, int weaponid, int hittype, int hitid, float x, float y, float z);
bool Streamer_OnPlayerGiveDamageActor(int playerid, int actorid, float amount, int weaponid, int bodypart);
bool Streamer_OnActorStreamIn(int actorid, int forplayerid);
bool Streamer_OnActorStreamOut(int actorid, int forplayerid);

#endif
