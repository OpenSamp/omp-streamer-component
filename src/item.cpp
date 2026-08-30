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

#include "main.h"

#include "item.h"
#include "identifier.h"

Identifier Item::Area::identifier;
Identifier Item::Checkpoint::identifier;
Identifier Item::MapIcon::identifier;
Identifier Item::Object::identifier;
Identifier Item::Pickup::identifier;
Identifier Item::RaceCheckpoint::identifier;
Identifier Item::TextLabel::identifier;
Identifier Item::Actor::identifier;

Item::Area::Area() {}
Item::Area::Attach::Attach() {}
Item::Checkpoint::Checkpoint() {}
Item::MapIcon::MapIcon() {}
Item::Object::Object() {}
Item::Object::Attach::Attach() {}
Item::Object::Material::Main::Main() {}
Item::Object::Material::Text::Text() {}
Item::Object::Move::Move() {}
Item::Pickup::Pickup() {}
Item::RaceCheckpoint::RaceCheckpoint() {}
Item::TextLabel::TextLabel() {}
Item::TextLabel::Attach::Attach() {}
Item::Actor::Actor() {}
Item::Actor::Anim::Anim() {}
