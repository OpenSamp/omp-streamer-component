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

#ifndef OPENMP_COMPONENT_H
#define OPENMP_COMPONENT_H

struct ICore;
struct IPawnComponent;
struct IVehiclesComponent;
struct IObjectsComponent;
struct IPickupsComponent;
struct IActorsComponent;
struct ICheckpointsComponent;
struct IClassesComponent;

// Accessors to the open.mp runtime, usable anywhere in the streamer once onInit has fired
// (sub-components become available then). Any of these can return nullptr if the corresponding
// component is not loaded by the server.
namespace StreamerRuntime
{
	ICore *core();
	IPawnComponent *pawn();
	IVehiclesComponent *vehicles();
	IObjectsComponent *objects();
	IPickupsComponent *pickups();
	IActorsComponent *actors();
	ICheckpointsComponent *checkpoints();
	IClassesComponent *classes();
}

#endif
