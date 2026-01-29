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

#include "component_bridge.h"

#include "core.h"
#include "main.h"
#include "natives.h"
#include "utility.h"

extern void *pAMXFunctions;
extern AMX_NATIVE_INFO natives[];

unsigned int StreamerSupports()
{
	return sampgdk::Supports() | SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES | SUPPORTS_PROCESS_TICK;
}

bool StreamerLoad(void **ppData)
{
	core.reset(new Core);
	pAMXFunctions = ppData[PLUGIN_DATA_AMX_EXPORTS];
	bool load = sampgdk::Load(ppData);
	sampgdk::logprintf("\n\n*** Streamer Plugin v%s by Incognito loaded ***\n", PLUGIN_VERSION);
	return load;
}

void StreamerUnload()
{
	core.reset();
	sampgdk::logprintf("\n\n*** Streamer Plugin v%s by Incognito unloaded ***\n", PLUGIN_VERSION);
	sampgdk::Unload();
}

int StreamerAmxLoad(AMX *amx)
{
	core->getData()->interfaces.insert(amx);
	core->getData()->amxUnloadDestroyItems.insert(amx);
	return Utility::checkInterfaceAndRegisterNatives(amx, natives);
}

int StreamerAmxUnload(AMX *amx)
{
	core->getData()->interfaces.erase(amx);
	if (core->getData()->amxUnloadDestroyItems.find(amx) != core->getData()->amxUnloadDestroyItems.end())
	{
		Utility::destroyAllItemsInInterface(amx);
		core->getData()->amxUnloadDestroyItems.erase(amx);
	}
	return AMX_ERR_NONE;
}

void StreamerProcessTick()
{
	core->getStreamer()->startAutomaticUpdate();
}
