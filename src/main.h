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

#ifndef MAIN_H
#define MAIN_H

#define INCLUDE_FILE_VERSION (0x296)
#define PLUGIN_VERSION "2.9.6"

// open.mp SDK constants (INVALID_PLAYER_ID, INVALID_VEHICLE_ID, INVALID_OBJECT_ID,
// INVALID_ACTOR_ID, PLAYER_POOL_SIZE, ...). Pulled in first so every source in the project
// sees the real constexpr from values.hpp rather than legacy shadow macros.
#include <values.hpp>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/geometries.hpp>

#include <Eigen/Core>

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <variant>
#include <tuple>

#include "common.h"
#include "streamer_api.h"

#endif
