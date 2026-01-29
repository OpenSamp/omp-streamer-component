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

#include "openmp_component.h"

#include "component_bridge.h"
#include "main.h"

namespace
{
	constexpr std::uint64_t kStreamerComponentUid = 0x73747265616d6572ULL;

	class StreamerComponent final : public StreamerComponentBase
	{
	public:
		const char *getName() const override
		{
			return "Streamer";
		}

		const char *getAuthor() const override
		{
			return "Incognito";
		}

		const char *getVersion() const override
		{
			return PLUGIN_VERSION;
		}

		std::uint64_t getUID() const override
		{
			return kStreamerComponentUid;
		}

		bool onLoad(void **ppData) override
		{
			return StreamerLoad(ppData);
		}

		void onUnload() override
		{
			StreamerUnload();
		}

		int onAmxLoad(AMX *amx) override
		{
			return StreamerAmxLoad(amx);
		}

		int onAmxUnload(AMX *amx) override
		{
			return StreamerAmxUnload(amx);
		}

		void onProcessTick() override
		{
			StreamerProcessTick();
		}
	};
}

STREAMER_COMPONENT_EXPORT StreamerComponentBase *ComponentEntryPoint()
{
	static StreamerComponent component;
	return &component;
}

STREAMER_COMPONENT_EXPORT StreamerComponentBase *GetComponent()
{
	return ComponentEntryPoint();
}

STREAMER_COMPONENT_EXPORT StreamerComponentBase *CreateComponent()
{
	return ComponentEntryPoint();
}
