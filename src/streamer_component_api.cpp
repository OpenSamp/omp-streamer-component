/*
 * IStreamerComponent implementation. Body methods are lightly-refactored copies of the
 * corresponding PAWN natives in src/natives/*.cpp — they work directly against Core's
 * Item::Shared* containers with amx=nullptr so the existing per-AMX destroy logic
 * ignores them (open.mp components don't own an AMX like PAWN scripts do).
 */

#include "main.h"

#include "core.h"
#include "streamer_api.h"
#include "streamer_component_api.h"
#include "utility.h"

#include <algorithm>
#include <cstring>
#include <vector>

// ---- C++ event handler registry -----------------------------------------------------------
// callbacks.cpp reads this vector under no additional locking (streamer is single-threaded
// inside the open.mp tick; handlers are registered from the same thread).

static std::vector<IStreamerEventHandler*>& handlerList()
{
	static std::vector<IStreamerEventHandler*> s;
	return s;
}

const std::vector<IStreamerEventHandler*>& GetStreamerEventHandlers()
{
	return handlerList();
}

namespace
{
	inline bool readPos(const Eigen::Vector3f &v, float &x, float &y, float &z)
	{
		x = v[0];
		y = v[1];
		z = v[2];
		return true;
	}

	template <class Map>
	typename Map::iterator find(Map &m, int id)
	{
		return m.find(id);
	}
}

// --- Objects --------------------------------------------------------------------------------

int IStreamerComponent_createObject(
	int modelId,
	float posX, float posY, float posZ,
	float rotX, float rotY, float rotZ,
	int worldId, int interiorId, int playerId,
	float streamDistance, float drawDistance,
	int areaId, int priority)
{
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_OBJECT) == core->getData()->objects.size())
	{
		return INVALID_STREAMER_ID;
	}
	int objectId = Item::Object::identifier.get();
	Item::SharedObject object(new Item::Object);
	object->amx = nullptr;
	object->objectId = objectId;
	object->inverseAreaChecking = false;
	object->noCameraCollision = false;
	object->originalComparableStreamDistance = -1.0f;
	object->positionOffset = Eigen::Vector3f::Zero();
	object->streamCallbacks = false;
	object->modelId = modelId;
	object->position = Eigen::Vector3f(posX, posY, posZ);
	object->rotation = Eigen::Vector3f(rotX, rotY, rotZ);
	Utility::addToContainer(object->worlds, worldId);
	Utility::addToContainer(object->interiors, interiorId);
	Utility::addToContainer(object->players, playerId);
	object->comparableStreamDistance = streamDistance < STREAMER_STATIC_DISTANCE_CUTOFF
		? streamDistance : streamDistance * streamDistance;
	object->streamDistance = streamDistance;
	object->drawDistance = drawDistance;
	Utility::addToContainer(object->areas, areaId);
	object->priority = priority;
	core->getGrid()->addObject(object);
	core->getData()->objects.insert(std::make_pair(objectId, object));
	return objectId;
}

bool IStreamerComponent_destroyObject(int objectId)
{
	auto it = core->getData()->objects.find(objectId);
	if (it == core->getData()->objects.end()) return false;
	Utility::destroyObject(it);
	return true;
}

bool IStreamerComponent_isValidObject(int objectId)
{
	return core->getData()->objects.find(objectId) != core->getData()->objects.end();
}

int IStreamerComponent_moveObject(int objectId,
	float targetX, float targetY, float targetZ,
	float speed,
	float rotX, float rotY, float rotZ)
{
	if (speed == 0.0f) return 0;
	auto o = core->getData()->objects.find(objectId);
	if (o == core->getData()->objects.end()) return 0;
	if (o->second->attach) return 0;

	Eigen::Vector3f position(targetX, targetY, targetZ);
	Eigen::Vector3f rotation(rotX, rotY, rotZ);
	o->second->move = std::make_shared<Item::Object::Move>();
	o->second->move->duration = static_cast<int>(
		(static_cast<float>(boost::geometry::distance(position, o->second->position) / speed) * 1000.0f));
	std::get<0>(o->second->move->position) = position;
	std::get<1>(o->second->move->position) = o->second->position;
	std::get<2>(o->second->move->position) = (position - o->second->position)
		/ static_cast<float>(o->second->move->duration);
	std::get<0>(o->second->move->rotation) = rotation;
	if ((std::get<0>(o->second->move->rotation).maxCoeff() + 1000.0f)
		> std::numeric_limits<float>::epsilon())
	{
		std::get<1>(o->second->move->rotation) = o->second->rotation;
		std::get<2>(o->second->move->rotation) = (rotation - o->second->rotation)
			/ static_cast<float>(o->second->move->duration);
	}
	o->second->move->speed = speed;
	o->second->move->time = std::chrono::steady_clock::now();
	for (auto p = core->getData()->players.begin(); p != core->getData()->players.end(); ++p)
	{
		auto i = p->second.internalObjects.find(o->first);
		if (i != p->second.internalObjects.end())
		{
			StreamerApi::StopPlayerObject(p->first, i->second);
			StreamerApi::MovePlayerObject(p->first, i->second,
				std::get<0>(o->second->move->position)[0],
				std::get<0>(o->second->move->position)[1],
				std::get<0>(o->second->move->position)[2],
				o->second->move->speed,
				std::get<0>(o->second->move->rotation)[0],
				std::get<0>(o->second->move->rotation)[1],
				std::get<0>(o->second->move->rotation)[2]);
		}
	}
	core->getStreamer()->movingObjects.insert(o->second);
	return o->second->move->duration;
}

bool IStreamerComponent_stopObject(int objectId)
{
	auto o = core->getData()->objects.find(objectId);
	if (o == core->getData()->objects.end()) return false;
	if (!o->second->move) return false;
	for (auto p = core->getData()->players.begin(); p != core->getData()->players.end(); ++p)
	{
		auto i = p->second.internalObjects.find(o->first);
		if (i != p->second.internalObjects.end())
		{
			StreamerApi::StopPlayerObject(p->first, i->second);
		}
	}
	o->second->move.reset();
	core->getStreamer()->movingObjects.erase(o->second);
	return true;
}

bool IStreamerComponent_isObjectMoving(int objectId)
{
	auto o = core->getData()->objects.find(objectId);
	return o != core->getData()->objects.end() && o->second->move;
}

bool IStreamerComponent_getObjectPos(int objectId, float &x, float &y, float &z)
{
	auto o = core->getData()->objects.find(objectId);
	if (o == core->getData()->objects.end()) return false;
	return readPos(o->second->position, x, y, z);
}

bool IStreamerComponent_setObjectPos(int objectId, float x, float y, float z)
{
	auto o = core->getData()->objects.find(objectId);
	if (o == core->getData()->objects.end()) return false;
	o->second->position = Eigen::Vector3f(x, y, z);
	for (auto p = core->getData()->players.begin(); p != core->getData()->players.end(); ++p)
	{
		auto i = p->second.internalObjects.find(o->first);
		if (i != p->second.internalObjects.end())
		{
			StreamerApi::SetPlayerObjectPos(p->first, i->second, x, y, z);
		}
	}
	core->getGrid()->removeObject(o->second, true);
	return true;
}

bool IStreamerComponent_getObjectRot(int objectId, float &x, float &y, float &z)
{
	auto o = core->getData()->objects.find(objectId);
	if (o == core->getData()->objects.end()) return false;
	return readPos(o->second->rotation, x, y, z);
}

bool IStreamerComponent_setObjectRot(int objectId, float x, float y, float z)
{
	auto o = core->getData()->objects.find(objectId);
	if (o == core->getData()->objects.end()) return false;
	o->second->rotation = Eigen::Vector3f(x, y, z);
	for (auto p = core->getData()->players.begin(); p != core->getData()->players.end(); ++p)
	{
		auto i = p->second.internalObjects.find(o->first);
		if (i != p->second.internalObjects.end())
		{
			StreamerApi::SetPlayerObjectRot(p->first, i->second, x, y, z);
		}
	}
	return true;
}

bool IStreamerComponent_setObjectMaterial(int objectId, int materialIndex,
	int modelId, const char *txdName, const char *textureName, uint32_t materialColor)
{
	auto o = core->getData()->objects.find(objectId);
	if (o == core->getData()->objects.end()) return false;
	Item::Object::Material material;
	material.main = std::make_shared<Item::Object::Material::Main>();
	material.main->modelId = modelId;
	material.main->txdFileName = txdName ? txdName : "";
	material.main->textureName = textureName ? textureName : "";
	material.main->materialColor = static_cast<int>(materialColor);
	o->second->materials[materialIndex] = material;
	for (auto p = core->getData()->players.begin(); p != core->getData()->players.end(); ++p)
	{
		auto i = p->second.internalObjects.find(o->first);
		if (i != p->second.internalObjects.end())
		{
			StreamerApi::SetPlayerObjectMaterial(p->first, i->second, materialIndex,
				material.main->modelId, material.main->txdFileName.c_str(),
				material.main->textureName.c_str(), material.main->materialColor);
		}
	}
	return true;
}

bool IStreamerComponent_setObjectMaterialText(int objectId, int materialIndex,
	const char *text, int materialSize, const char *fontFace,
	int fontSize, bool bold, uint32_t fontColor, uint32_t backColor, int alignment)
{
	auto o = core->getData()->objects.find(objectId);
	if (o == core->getData()->objects.end()) return false;
	Item::Object::Material material;
	material.text = std::make_shared<Item::Object::Material::Text>();
	material.text->materialText = text ? text : "";
	material.text->materialSize = materialSize;
	material.text->fontFace = fontFace ? fontFace : "";
	material.text->fontSize = fontSize;
	material.text->bold = bold;
	material.text->fontColor = static_cast<int>(fontColor);
	material.text->backColor = static_cast<int>(backColor);
	material.text->textAlignment = alignment;
	o->second->materials[materialIndex] = material;
	for (auto p = core->getData()->players.begin(); p != core->getData()->players.end(); ++p)
	{
		auto i = p->second.internalObjects.find(o->first);
		if (i != p->second.internalObjects.end())
		{
			StreamerApi::SetPlayerObjectMaterialText(p->first, i->second,
				material.text->materialText.c_str(), materialIndex,
				material.text->materialSize, material.text->fontFace.c_str(),
				material.text->fontSize, material.text->bold,
				material.text->fontColor, material.text->backColor,
				material.text->textAlignment);
		}
	}
	return true;
}

bool IStreamerComponent_editObject(int playerId, int objectId)
{
	auto p = core->getData()->players.find(playerId);
	if (p == core->getData()->players.end()) return false;
	auto o = core->getData()->objects.find(objectId);
	if (o == core->getData()->objects.end()) return false;
	auto i = p->second.internalObjects.find(objectId);
	if (i == p->second.internalObjects.end()) return false;
	StreamerApi::EditPlayerObject(playerId, i->second);
	return true;
}

// --- Pickups --------------------------------------------------------------------------------

int IStreamerComponent_createPickup(int modelId, int type,
	float posX, float posY, float posZ,
	int worldId, int interiorId, int playerId,
	float streamDistance, int areaId, int priority)
{
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_PICKUP) == core->getData()->pickups.size())
	{
		return INVALID_STREAMER_ID;
	}
	int pickupId = Item::Pickup::identifier.get();
	Item::SharedPickup pickup(new Item::Pickup);
	pickup->amx = nullptr;
	pickup->pickupId = pickupId;
	pickup->inverseAreaChecking = false;
	pickup->originalComparableStreamDistance = -1.0f;
	pickup->positionOffset = Eigen::Vector3f::Zero();
	pickup->streamCallbacks = false;
	pickup->modelId = modelId;
	pickup->type = type;
	pickup->position = Eigen::Vector3f(posX, posY, posZ);
	Utility::addToContainer(pickup->worlds, worldId);
	if (pickup->worlds.empty()) pickup->worlds.insert(-1);
	Utility::addToContainer(pickup->interiors, interiorId);
	Utility::addToContainer(pickup->players, playerId);
	pickup->comparableStreamDistance = streamDistance < STREAMER_STATIC_DISTANCE_CUTOFF
		? streamDistance : streamDistance * streamDistance;
	pickup->streamDistance = streamDistance;
	Utility::addToContainer(pickup->areas, areaId);
	pickup->priority = priority;
	core->getGrid()->addPickup(pickup);
	core->getData()->pickups.insert(std::make_pair(pickupId, pickup));
	return pickupId;
}

bool IStreamerComponent_destroyPickup(int pickupId)
{
	auto it = core->getData()->pickups.find(pickupId);
	if (it == core->getData()->pickups.end()) return false;
	Utility::destroyPickup(it);
	return true;
}

bool IStreamerComponent_isValidPickup(int pickupId)
{
	return core->getData()->pickups.find(pickupId) != core->getData()->pickups.end();
}

// --- 3D Text Labels -------------------------------------------------------------------------

int IStreamerComponent_createTextLabel(const char *text, uint32_t color,
	float posX, float posY, float posZ, float drawDistance,
	int attachedPlayer, int attachedVehicle, bool testLos,
	int worldId, int interiorId, int playerId,
	float streamDistance, int areaId, int priority)
{
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_3D_TEXT_LABEL) == core->getData()->textLabels.size())
	{
		return INVALID_STREAMER_ID;
	}
	int labelId = Item::TextLabel::identifier.get();
	Item::SharedTextLabel label(new Item::TextLabel);
	label->amx = nullptr;
	label->textLabelId = labelId;
	label->inverseAreaChecking = false;
	label->originalComparableStreamDistance = -1.0f;
	label->positionOffset = Eigen::Vector3f::Zero();
	label->streamCallbacks = false;
	label->text = text ? text : "";
	label->color = static_cast<int>(color);
	label->position = Eigen::Vector3f(posX, posY, posZ);
	label->drawDistance = drawDistance;
	if (attachedPlayer != INVALID_PLAYER_ID || attachedVehicle != INVALID_VEHICLE_ID)
	{
		label->attach = std::make_shared<Item::TextLabel::Attach>();
		label->attach->player = attachedPlayer;
		label->attach->vehicle = attachedVehicle;
		if (label->position.cwiseAbs().maxCoeff() > std::numeric_limits<float>::epsilon())
		{
			label->attach->position = label->position;
		}
	}
	label->testLOS = testLos;
	Utility::addToContainer(label->worlds, worldId);
	Utility::addToContainer(label->interiors, interiorId);
	Utility::addToContainer(label->players, playerId);
	label->comparableStreamDistance = streamDistance < STREAMER_STATIC_DISTANCE_CUTOFF
		? streamDistance : streamDistance * streamDistance;
	label->streamDistance = streamDistance;
	Utility::addToContainer(label->areas, areaId);
	label->priority = priority;
	core->getGrid()->addTextLabel(label);
	core->getData()->textLabels.insert(std::make_pair(labelId, label));
	return labelId;
}

bool IStreamerComponent_destroyTextLabel(int labelId)
{
	auto it = core->getData()->textLabels.find(labelId);
	if (it == core->getData()->textLabels.end()) return false;
	Utility::destroyTextLabel(it);
	return true;
}

bool IStreamerComponent_updateTextLabelText(int labelId, uint32_t color, const char *text)
{
	auto it = core->getData()->textLabels.find(labelId);
	if (it == core->getData()->textLabels.end()) return false;
	it->second->color = static_cast<int>(color);
	it->second->text = text ? text : "";
	for (auto p = core->getData()->players.begin(); p != core->getData()->players.end(); ++p)
	{
		auto i = p->second.internalTextLabels.find(it->first);
		if (i != p->second.internalTextLabels.end())
		{
			StreamerApi::UpdatePlayer3DTextLabelText(p->first, i->second,
				it->second->color, it->second->text.c_str());
		}
	}
	return true;
}

bool IStreamerComponent_isValidTextLabel(int labelId)
{
	return core->getData()->textLabels.find(labelId) != core->getData()->textLabels.end();
}

// --- Map Icons ------------------------------------------------------------------------------

int IStreamerComponent_createMapIcon(float posX, float posY, float posZ,
	int type, uint32_t color,
	int worldId, int interiorId, int playerId,
	float streamDistance, int style, int areaId, int priority)
{
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_MAP_ICON) == core->getData()->mapIcons.size())
	{
		return INVALID_STREAMER_ID;
	}
	int iconId = Item::MapIcon::identifier.get();
	Item::SharedMapIcon icon(new Item::MapIcon);
	icon->amx = nullptr;
	icon->mapIconId = iconId;
	icon->inverseAreaChecking = false;
	icon->originalComparableStreamDistance = -1.0f;
	icon->positionOffset = Eigen::Vector3f::Zero();
	icon->streamCallbacks = false;
	icon->position = Eigen::Vector3f(posX, posY, posZ);
	icon->type = type;
	icon->color = static_cast<int>(color);
	Utility::addToContainer(icon->worlds, worldId);
	Utility::addToContainer(icon->interiors, interiorId);
	Utility::addToContainer(icon->players, playerId);
	icon->comparableStreamDistance = streamDistance < STREAMER_STATIC_DISTANCE_CUTOFF
		? streamDistance : streamDistance * streamDistance;
	icon->streamDistance = streamDistance;
	icon->style = style;
	Utility::addToContainer(icon->areas, areaId);
	icon->priority = priority;
	core->getGrid()->addMapIcon(icon);
	core->getData()->mapIcons.insert(std::make_pair(iconId, icon));
	return iconId;
}

bool IStreamerComponent_destroyMapIcon(int iconId)
{
	auto it = core->getData()->mapIcons.find(iconId);
	if (it == core->getData()->mapIcons.end()) return false;
	Utility::destroyMapIcon(it);
	return true;
}

bool IStreamerComponent_isValidMapIcon(int iconId)
{
	return core->getData()->mapIcons.find(iconId) != core->getData()->mapIcons.end();
}

// --- Checkpoints ----------------------------------------------------------------------------

int IStreamerComponent_createCheckpoint(float posX, float posY, float posZ, float size,
	int worldId, int interiorId, int playerId,
	float streamDistance, int areaId, int priority)
{
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_CP) == core->getData()->checkpoints.size())
	{
		return INVALID_STREAMER_ID;
	}
	int cpId = Item::Checkpoint::identifier.get();
	Item::SharedCheckpoint cp(new Item::Checkpoint);
	cp->amx = nullptr;
	cp->checkpointId = cpId;
	cp->inverseAreaChecking = false;
	cp->originalComparableStreamDistance = -1.0f;
	cp->positionOffset = Eigen::Vector3f::Zero();
	cp->streamCallbacks = false;
	cp->position = Eigen::Vector3f(posX, posY, posZ);
	cp->size = size;
	Utility::addToContainer(cp->worlds, worldId);
	Utility::addToContainer(cp->interiors, interiorId);
	Utility::addToContainer(cp->players, playerId);
	cp->comparableStreamDistance = streamDistance < STREAMER_STATIC_DISTANCE_CUTOFF
		? streamDistance : streamDistance * streamDistance;
	cp->streamDistance = streamDistance;
	Utility::addToContainer(cp->areas, areaId);
	cp->priority = priority;
	core->getGrid()->addCheckpoint(cp);
	core->getData()->checkpoints.insert(std::make_pair(cpId, cp));
	return cpId;
}

bool IStreamerComponent_destroyCheckpoint(int cpId)
{
	auto it = core->getData()->checkpoints.find(cpId);
	if (it == core->getData()->checkpoints.end()) return false;
	Utility::destroyCheckpoint(it);
	return true;
}

bool IStreamerComponent_isValidCheckpoint(int cpId)
{
	return core->getData()->checkpoints.find(cpId) != core->getData()->checkpoints.end();
}

// --- Attach helpers -------------------------------------------------------------------------
// (объявление есть в streamer_component_api.h: attachObjectToObject/Player/Vehicle)

bool IStreamerComponent_attachObjectToObject(int objectId, int parentObjectId,
	float offX, float offY, float offZ,
	float rotX, float rotY, float rotZ,
	bool syncRotation)
{
	auto o = core->getData()->objects.find(objectId);
	auto p = core->getData()->objects.find(parentObjectId);
	if (o == core->getData()->objects.end() || p == core->getData()->objects.end()) return false;
	if (o->second->move) return false;
	if (!o->second->attach) o->second->attach = std::make_shared<Item::Object::Attach>();
	o->second->attach->object = parentObjectId;
	o->second->attach->player = INVALID_PLAYER_ID;
	o->second->attach->vehicle = INVALID_VEHICLE_ID;
	o->second->attach->position = Eigen::Vector3f(offX, offY, offZ);
	o->second->attach->rotation = Eigen::Vector3f(rotX, rotY, rotZ);
	o->second->attach->syncRotation = syncRotation;
	core->getStreamer()->attachedObjects.insert(o->second);
	return true;
}

bool IStreamerComponent_attachObjectToPlayer(int objectId, int playerId,
	float offX, float offY, float offZ,
	float rotX, float rotY, float rotZ)
{
	auto o = core->getData()->objects.find(objectId);
	if (o == core->getData()->objects.end()) return false;
	if (o->second->move) return false;
	if (!o->second->attach) o->second->attach = std::make_shared<Item::Object::Attach>();
	o->second->attach->object = INVALID_OBJECT_ID;
	o->second->attach->player = playerId;
	o->second->attach->vehicle = INVALID_VEHICLE_ID;
	o->second->attach->position = Eigen::Vector3f(offX, offY, offZ);
	o->second->attach->rotation = Eigen::Vector3f(rotX, rotY, rotZ);
	core->getStreamer()->attachedObjects.insert(o->second);
	return true;
}

bool IStreamerComponent_attachObjectToVehicle(int objectId, int vehicleId,
	float offX, float offY, float offZ,
	float rotX, float rotY, float rotZ)
{
	auto o = core->getData()->objects.find(objectId);
	if (o == core->getData()->objects.end()) return false;
	if (o->second->move) return false;
	if (!o->second->attach) o->second->attach = std::make_shared<Item::Object::Attach>();
	o->second->attach->object = INVALID_OBJECT_ID;
	o->second->attach->player = INVALID_PLAYER_ID;
	o->second->attach->vehicle = vehicleId;
	o->second->attach->position = Eigen::Vector3f(offX, offY, offZ);
	o->second->attach->rotation = Eigen::Vector3f(rotX, rotY, rotZ);
	core->getStreamer()->attachedObjects.insert(o->second);
	return true;
}

// --- Actors ---------------------------------------------------------------------------------
// Bodies lifted from src/natives/actors.cpp with amx=nullptr. Streamer's internal actor
// objects ignore the nullptr amx during destruction; see Utility::destroyActor.

int IStreamerComponent_createActor(int modelId, float x, float y, float z, float rotation,
	bool invulnerable, float health, float streamDistance,
	int worldId, int interiorId, int playerId, int areaId, int priority)
{
	if (core->getData()->getGlobalMaxItems(STREAMER_TYPE_ACTOR) == core->getData()->actors.size())
	{
		return INVALID_STREAMER_ID;
	}
	int actorId = Item::Actor::identifier.get();
	Item::SharedActor actor(new Item::Actor);
	actor->amx = nullptr;
	actor->actorId = actorId;
	actor->inverseAreaChecking = false;
	actor->originalComparableStreamDistance = -1.0f;
	actor->positionOffset = Eigen::Vector3f::Zero();
	actor->modelId = modelId;
	actor->position = Eigen::Vector3f(x, y, z);
	actor->rotation = rotation;
	actor->invulnerable = invulnerable;
	actor->health = health;
	Utility::addToContainer(actor->worlds, worldId);
	if (actor->worlds.empty()) actor->worlds.insert(-1);
	Utility::addToContainer(actor->interiors, interiorId);
	Utility::addToContainer(actor->players, playerId);
	actor->comparableStreamDistance = streamDistance < STREAMER_STATIC_DISTANCE_CUTOFF
		? streamDistance : streamDistance * streamDistance;
	actor->streamDistance = streamDistance;
	Utility::addToContainer(actor->areas, areaId);
	actor->priority = priority;
	core->getGrid()->addActor(actor);
	core->getData()->actors.insert(std::make_pair(actorId, actor));
	return actorId;
}

bool IStreamerComponent_destroyActor(int actorId)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	Utility::destroyActor(a);
	return true;
}

bool IStreamerComponent_isValidActor(int actorId)
{
	return core->getData()->actors.find(actorId) != core->getData()->actors.end();
}

bool IStreamerComponent_applyActorAnimation(int actorId, const char *animLib, const char *animName,
	float delta, bool loop, bool lockX, bool lockY, bool freeze, int timeMs)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	a->second->anim = std::make_shared<Item::Actor::Anim>();
	a->second->anim->lib = animLib ? animLib : "";
	a->second->anim->name = animName ? animName : "";
	a->second->anim->delta = delta;
	a->second->anim->loop = loop;
	a->second->anim->lockx = lockX;
	a->second->anim->locky = lockY;
	a->second->anim->freeze = freeze;
	a->second->anim->time = timeMs;
	for (auto w = a->second->worlds.begin(); w != a->second->worlds.end(); ++w)
	{
		auto i = core->getData()->internalActors.find(std::make_pair(a->first, *w));
		if (i != core->getData()->internalActors.end())
		{
			StreamerApi::ApplyActorAnimation(i->second, a->second->anim->lib.c_str(),
				a->second->anim->name.c_str(), a->second->anim->delta,
				a->second->anim->loop, a->second->anim->lockx, a->second->anim->locky,
				a->second->anim->freeze, a->second->anim->time);
		}
	}
	return true;
}

bool IStreamerComponent_clearActorAnimations(int actorId)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	a->second->anim = nullptr;
	for (auto w = a->second->worlds.begin(); w != a->second->worlds.end(); ++w)
	{
		auto i = core->getData()->internalActors.find(std::make_pair(a->first, *w));
		if (i != core->getData()->internalActors.end())
		{
			StreamerApi::ClearActorAnimations(i->second);
		}
	}
	return true;
}

bool IStreamerComponent_getActorPos(int actorId, float &outX, float &outY, float &outZ)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	outX = a->second->position[0];
	outY = a->second->position[1];
	outZ = a->second->position[2];
	return true;
}

bool IStreamerComponent_setActorPos(int actorId, float x, float y, float z)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	a->second->position = Eigen::Vector3f(x, y, z);
	for (auto w = a->second->worlds.begin(); w != a->second->worlds.end(); ++w)
	{
		auto i = core->getData()->internalActors.find(std::make_pair(a->first, *w));
		if (i != core->getData()->internalActors.end())
		{
			core->getGrid()->removeActor(a->second, true);
			StreamerApi::SetActorPos(i->second, x, y, z);
		}
	}
	return true;
}

bool IStreamerComponent_getActorFacingAngle(int actorId, float &outAngle)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	outAngle = a->second->rotation;
	return true;
}

bool IStreamerComponent_setActorFacingAngle(int actorId, float angle)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	a->second->rotation = angle;
	for (auto w = a->second->worlds.begin(); w != a->second->worlds.end(); ++w)
	{
		auto i = core->getData()->internalActors.find(std::make_pair(a->first, *w));
		if (i != core->getData()->internalActors.end())
		{
			StreamerApi::DestroyActor(i->second);
			i->second = StreamerApi::CreateActor(a->second->modelId,
				a->second->position[0], a->second->position[1], a->second->position[2],
				a->second->rotation);
			StreamerApi::SetActorInvulnerable(i->second, a->second->invulnerable);
			StreamerApi::SetActorHealth(i->second, a->second->health);
			StreamerApi::SetActorVirtualWorld(i->second, *w);
			if (a->second->anim)
			{
				StreamerApi::ApplyActorAnimation(i->second, a->second->anim->lib.c_str(),
					a->second->anim->name.c_str(), a->second->anim->delta,
					a->second->anim->loop, a->second->anim->lockx, a->second->anim->locky,
					a->second->anim->freeze, a->second->anim->time);
			}
		}
	}
	return true;
}

bool IStreamerComponent_getActorHealth(int actorId, float &outHealth)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	outHealth = a->second->health;
	return true;
}

bool IStreamerComponent_setActorHealth(int actorId, float health)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	a->second->health = health;
	for (auto w = a->second->worlds.begin(); w != a->second->worlds.end(); ++w)
	{
		auto i = core->getData()->internalActors.find(std::make_pair(a->first, *w));
		if (i != core->getData()->internalActors.end())
		{
			StreamerApi::SetActorHealth(i->second, a->second->health);
		}
	}
	return true;
}

bool IStreamerComponent_isActorInvulnerable(int actorId)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	return a->second->invulnerable;
}

bool IStreamerComponent_setActorInvulnerable(int actorId, bool invulnerable)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	a->second->invulnerable = invulnerable;
	// Invulnerability change requires actor re-creation (see natives/actors.cpp).
	for (auto w = a->second->worlds.begin(); w != a->second->worlds.end(); ++w)
	{
		auto i = core->getData()->internalActors.find(std::make_pair(a->first, *w));
		if (i != core->getData()->internalActors.end())
		{
			StreamerApi::DestroyActor(i->second);
			i->second = StreamerApi::CreateActor(a->second->modelId,
				a->second->position[0], a->second->position[1], a->second->position[2],
				a->second->rotation);
			StreamerApi::SetActorInvulnerable(i->second, a->second->invulnerable);
			StreamerApi::SetActorHealth(i->second, a->second->health);
			StreamerApi::SetActorVirtualWorld(i->second, *w);
			if (a->second->anim)
			{
				StreamerApi::ApplyActorAnimation(i->second, a->second->anim->lib.c_str(),
					a->second->anim->name.c_str(), a->second->anim->delta,
					a->second->anim->loop, a->second->anim->lockx, a->second->anim->locky,
					a->second->anim->freeze, a->second->anim->time);
			}
		}
	}
	return true;
}

int IStreamerComponent_getActorVirtualWorld(int actorId)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return 0;
	return Utility::getFirstValueInContainer(a->second->worlds);
}

bool IStreamerComponent_setActorVirtualWorld(int actorId, int worldId)
{
	auto a = core->getData()->actors.find(actorId);
	if (a == core->getData()->actors.end()) return false;
	Utility::setFirstValueInContainer(a->second->worlds, worldId);
	for (auto w = a->second->worlds.begin(); w != a->second->worlds.end(); ++w)
	{
		auto i = core->getData()->internalActors.find(std::make_pair(a->first, *w));
		if (i != core->getData()->internalActors.end())
		{
			StreamerApi::SetActorVirtualWorld(i->second, *w);
		}
	}
	return true;
}

// --- Extension wrapper ----------------------------------------------------------------------
// Concrete class that vtable-forwards to the C-style functions above.

class StreamerExtension : public IStreamerComponent
{
public:
	int createObject(int modelId,
		float posX, float posY, float posZ,
		float rotX, float rotY, float rotZ,
		int worldId, int interiorId, int playerId,
		float streamDistance, float drawDistance,
		int areaId, int priority) override
	{
		return IStreamerComponent_createObject(modelId, posX, posY, posZ, rotX, rotY, rotZ,
			worldId, interiorId, playerId, streamDistance, drawDistance, areaId, priority);
	}
	bool destroyObject(int objectId) override { return IStreamerComponent_destroyObject(objectId); }
	bool isValidObject(int objectId) override { return IStreamerComponent_isValidObject(objectId); }
	int  moveObject(int objectId,
		float tx, float ty, float tz,
		float speed,
		float rx, float ry, float rz) override
	{
		return IStreamerComponent_moveObject(objectId, tx, ty, tz, speed, rx, ry, rz);
	}
	bool stopObject(int objectId) override { return IStreamerComponent_stopObject(objectId); }
	bool isObjectMoving(int objectId) override { return IStreamerComponent_isObjectMoving(objectId); }
	bool getObjectPos(int objectId, float &x, float &y, float &z) override { return IStreamerComponent_getObjectPos(objectId, x, y, z); }
	bool setObjectPos(int objectId, float x, float y, float z) override { return IStreamerComponent_setObjectPos(objectId, x, y, z); }
	bool getObjectRot(int objectId, float &x, float &y, float &z) override { return IStreamerComponent_getObjectRot(objectId, x, y, z); }
	bool setObjectRot(int objectId, float x, float y, float z) override { return IStreamerComponent_setObjectRot(objectId, x, y, z); }
	bool attachObjectToObject(int o, int p, float ox, float oy, float oz, float rx, float ry, float rz, bool sync) override
	{ return IStreamerComponent_attachObjectToObject(o, p, ox, oy, oz, rx, ry, rz, sync); }
	bool attachObjectToPlayer(int o, int p, float ox, float oy, float oz, float rx, float ry, float rz) override
	{ return IStreamerComponent_attachObjectToPlayer(o, p, ox, oy, oz, rx, ry, rz); }
	bool attachObjectToVehicle(int o, int v, float ox, float oy, float oz, float rx, float ry, float rz) override
	{ return IStreamerComponent_attachObjectToVehicle(o, v, ox, oy, oz, rx, ry, rz); }
	bool setObjectMaterial(int o, int idx, int m, const char *txd, const char *tex, uint32_t col) override
	{ return IStreamerComponent_setObjectMaterial(o, idx, m, txd, tex, col); }
	bool setObjectMaterialText(int o, int idx, const char *t, int s, const char *f, int fs, bool b, uint32_t fc, uint32_t bc, int a) override
	{ return IStreamerComponent_setObjectMaterialText(o, idx, t, s, f, fs, b, fc, bc, a); }
	bool editObject(int playerId, int objectId) override { return IStreamerComponent_editObject(playerId, objectId); }

	int createPickup(int m, int t, float x, float y, float z, int w, int i, int p, float sd, int a, int pr) override
	{ return IStreamerComponent_createPickup(m, t, x, y, z, w, i, p, sd, a, pr); }
	bool destroyPickup(int id) override { return IStreamerComponent_destroyPickup(id); }
	bool isValidPickup(int id) override { return IStreamerComponent_isValidPickup(id); }

	int createTextLabel(const char *t, uint32_t col, float x, float y, float z, float dd, int ap, int av, bool los,
		int w, int i, int p, float sd, int a, int pr) override
	{ return IStreamerComponent_createTextLabel(t, col, x, y, z, dd, ap, av, los, w, i, p, sd, a, pr); }
	bool destroyTextLabel(int id) override { return IStreamerComponent_destroyTextLabel(id); }
	bool updateTextLabelText(int id, uint32_t col, const char *t) override { return IStreamerComponent_updateTextLabelText(id, col, t); }
	bool isValidTextLabel(int id) override { return IStreamerComponent_isValidTextLabel(id); }

	int createMapIcon(float x, float y, float z, int type, uint32_t col, int w, int i, int p, float sd, int style, int a, int pr) override
	{ return IStreamerComponent_createMapIcon(x, y, z, type, col, w, i, p, sd, style, a, pr); }
	bool destroyMapIcon(int id) override { return IStreamerComponent_destroyMapIcon(id); }
	bool isValidMapIcon(int id) override { return IStreamerComponent_isValidMapIcon(id); }

	int createCheckpoint(float x, float y, float z, float size, int w, int i, int p, float sd, int a, int pr) override
	{ return IStreamerComponent_createCheckpoint(x, y, z, size, w, i, p, sd, a, pr); }
	bool destroyCheckpoint(int id) override { return IStreamerComponent_destroyCheckpoint(id); }
	bool isValidCheckpoint(int id) override { return IStreamerComponent_isValidCheckpoint(id); }

	int createActor(int modelId, float x, float y, float z, float rotation,
		bool invulnerable, float health, float streamDistance,
		int worldId, int interiorId, int playerId, int areaId, int priority) override
	{ return IStreamerComponent_createActor(modelId, x, y, z, rotation, invulnerable, health,
		streamDistance, worldId, interiorId, playerId, areaId, priority); }
	bool destroyActor(int id) override { return IStreamerComponent_destroyActor(id); }
	bool isValidActor(int id) override { return IStreamerComponent_isValidActor(id); }
	bool applyActorAnimation(int id, const char *lib, const char *name, float delta,
		bool loop, bool lx, bool ly, bool fr, int t) override
	{ return IStreamerComponent_applyActorAnimation(id, lib, name, delta, loop, lx, ly, fr, t); }
	bool clearActorAnimations(int id) override { return IStreamerComponent_clearActorAnimations(id); }
	bool getActorPos(int id, float &x, float &y, float &z) override { return IStreamerComponent_getActorPos(id, x, y, z); }
	bool setActorPos(int id, float x, float y, float z) override { return IStreamerComponent_setActorPos(id, x, y, z); }
	bool getActorFacingAngle(int id, float &a) override { return IStreamerComponent_getActorFacingAngle(id, a); }
	bool setActorFacingAngle(int id, float a) override { return IStreamerComponent_setActorFacingAngle(id, a); }
	bool getActorHealth(int id, float &h) override { return IStreamerComponent_getActorHealth(id, h); }
	bool setActorHealth(int id, float h) override { return IStreamerComponent_setActorHealth(id, h); }
	bool isActorInvulnerable(int id) override { return IStreamerComponent_isActorInvulnerable(id); }
	bool setActorInvulnerable(int id, bool inv) override { return IStreamerComponent_setActorInvulnerable(id, inv); }
	int getActorVirtualWorld(int id) override { return IStreamerComponent_getActorVirtualWorld(id); }
	bool setActorVirtualWorld(int id, int w) override { return IStreamerComponent_setActorVirtualWorld(id, w); }

	void addEventHandler(IStreamerEventHandler* h) override
	{
		if (!h) return;
		auto &list = handlerList();
		if (std::find(list.begin(), list.end(), h) == list.end())
		{
			list.push_back(h);
		}
	}

	void removeEventHandler(IStreamerEventHandler* h) override
	{
		auto &list = handlerList();
		list.erase(std::remove(list.begin(), list.end(), h), list.end());
	}
};

static StreamerExtension g_streamerExtension;

IStreamerComponent *GetStreamerExtension()
{
	return &g_streamerExtension;
}
