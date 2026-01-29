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

#include <cstdint>

#include "sampgdk.h"

#if defined(_WIN32)
#define STREAMER_COMPONENT_EXPORT extern "C" __declspec(dllexport)
#else
#define STREAMER_COMPONENT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

class StreamerComponentBase
{
public:
	virtual ~StreamerComponentBase() = default;
	virtual const char *getName() const = 0;
	virtual const char *getAuthor() const = 0;
	virtual const char *getVersion() const = 0;
	virtual std::uint64_t getUID() const = 0;
	virtual bool onLoad(void **ppData) = 0;
	virtual void onUnload() = 0;
	virtual int onAmxLoad(AMX *amx) = 0;
	virtual int onAmxUnload(AMX *amx) = 0;
	virtual void onProcessTick() = 0;
};

STREAMER_COMPONENT_EXPORT StreamerComponentBase *ComponentEntryPoint();
STREAMER_COMPONENT_EXPORT StreamerComponentBase *GetComponent();
STREAMER_COMPONENT_EXPORT StreamerComponentBase *CreateComponent();

#endif
