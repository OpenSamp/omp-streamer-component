/*
 * Streamer public C++ API for open.mp components.
 *
 * Exposes a subset of the streamer's functionality as an IExtension so that other
 * components can call into the streamer without going through
 * the PAWN AMX native table.
 *
 * Consumer pattern:
 *     auto* streamerComp = list->queryComponent(UID{0x53744d72506c674eULL});
 *     if (streamerComp) {
 *         auto* streamer = queryExtension<IStreamerComponent>(streamerComp);
 *         int id = streamer->createObject(...);
 *     }
 *
 * All coordinates are passed as individual floats to keep ABI boundary simple and
 * independent of Eigen/glm headers.
 *
 * For parameters that in PAWN were "single-value arrays" (worlds, interiors, players,
 * areas) we accept the scalar value directly: pass -1 to mean "no filter / any".
 * If you need multi-world / multi-interior support, use the *Ex variants (TODO).
 */

#ifndef STREAMER_COMPONENT_API_H
#define STREAMER_COMPONENT_API_H

#include <component.hpp>
#include <cstdint>

// Shared UID: both StreamerComponent (as IComponent) and the exposed extension
// use the same underlying 64-bit constant, but live on different IID slots
// (component's IID vs. extension's ExtensionIID).
constexpr UID kStreamerComponentUID = UID(0x53744d72506c674eULL);      // "StMrPlgN"
constexpr UID kStreamerExtensionUID = UID(0x53744d72506c6758ULL);      // "StMrPlgX"

// ============================================================================
// Events — C++ equivalent of PAWN-public dispatchers.
//
// An IStreamerComponent host fires these callbacks in ADDITION to the PAWN-script
// dispatch (AMX amx_FindPublic). Register via IStreamerComponent::addEventHandler().
// All methods have default no-op implementations so implementers only override
// what they need.
//
// For "decision" events (Edit/Select/Shoot) the handler may return a
// StreamerHandlerResult that controls propagation to further handlers:
//   Continue    — not consuming the event, pass to the next handler and scripts
//   Consume     — swallow the event, don't pass downstream (Edit/Select semantics)
//   Veto        — deny the action (only meaningful for Shoot)
// ============================================================================

enum class StreamerHandlerResult : int
{
	Continue = 0,
	Consume  = 1,
	Veto     = 2,
};

struct IStreamerEventHandler
{
	virtual ~IStreamerEventHandler() = default;

	// Fire-and-forget events.
	virtual void onPlayerPickUpDynamicPickup(int playerId, int pickupId) {}
	virtual void onPlayerEnterDynamicCheckpoint(int playerId, int cpId) {}
	virtual void onPlayerLeaveDynamicCheckpoint(int playerId, int cpId) {}
	virtual void onPlayerEnterDynamicRaceCheckpoint(int playerId, int cpId) {}
	virtual void onPlayerLeaveDynamicRaceCheckpoint(int playerId, int cpId) {}
	virtual void onPlayerEnterDynamicArea(int playerId, int areaId) {}
	virtual void onPlayerLeaveDynamicArea(int playerId, int areaId) {}
	virtual void onDynamicObjectMoved(int objectId) {}
	virtual void onDynamicObjectStreamIn(int objectId, int forPlayerId) {}
	virtual void onDynamicObjectStreamOut(int objectId, int forPlayerId) {}
	virtual void onDynamicPickupStreamIn(int pickupId, int forPlayerId) {}
	virtual void onDynamicPickupStreamOut(int pickupId, int forPlayerId) {}
	virtual void onDynamicTextLabelStreamIn(int labelId, int forPlayerId) {}
	virtual void onDynamicTextLabelStreamOut(int labelId, int forPlayerId) {}
	virtual void onDynamicCheckpointStreamIn(int cpId, int forPlayerId) {}
	virtual void onDynamicCheckpointStreamOut(int cpId, int forPlayerId) {}
	virtual void onDynamicMapIconStreamIn(int iconId, int forPlayerId) {}
	virtual void onDynamicMapIconStreamOut(int iconId, int forPlayerId) {}

	// Decision events.
	virtual StreamerHandlerResult onPlayerEditDynamicObject(int playerId, int objectId, int response,
		float x, float y, float z, float rx, float ry, float rz)
	{
		return StreamerHandlerResult::Continue;
	}
	virtual StreamerHandlerResult onPlayerSelectDynamicObject(int playerId, int objectId,
		int modelId, float x, float y, float z)
	{
		return StreamerHandlerResult::Continue;
	}
	/// Return Veto to deny the shot, Continue otherwise.
	virtual StreamerHandlerResult onPlayerShootDynamicObject(int playerId, int weaponId, int objectId,
		float x, float y, float z)
	{
		return StreamerHandlerResult::Continue;
	}
};

struct IStreamerComponent : public IExtension
{
	PROVIDE_EXT_UID(kStreamerExtensionUID)

	// ============================================================================
	// Objects
	// ============================================================================

	/// CreateDynamicObject analogue. Returns objectId or INVALID_STREAMER_ID (0).
	virtual int createObject(int modelId,
		float posX, float posY, float posZ,
		float rotX, float rotY, float rotZ,
		int worldId = -1, int interiorId = -1, int playerId = -1,
		float streamDistance = 300.0f, float drawDistance = 0.0f,
		int areaId = -1, int priority = 0) = 0;

	virtual bool destroyObject(int objectId) = 0;
	virtual bool isValidObject(int objectId) = 0;

	/// Returns the moving-object ID from streamer.cpp or 0 on error.
	virtual int moveObject(int objectId,
		float targetX, float targetY, float targetZ,
		float speed,
		float rotX, float rotY, float rotZ) = 0;
	virtual bool stopObject(int objectId) = 0;
	virtual bool isObjectMoving(int objectId) = 0;

	virtual bool getObjectPos(int objectId, float& outX, float& outY, float& outZ) = 0;
	virtual bool setObjectPos(int objectId, float x, float y, float z) = 0;
	virtual bool getObjectRot(int objectId, float& outX, float& outY, float& outZ) = 0;
	virtual bool setObjectRot(int objectId, float x, float y, float z) = 0;

	virtual bool attachObjectToObject(int objectId, int parentObjectId,
		float offX, float offY, float offZ,
		float rotX, float rotY, float rotZ,
		bool syncRotation) = 0;
	virtual bool attachObjectToPlayer(int objectId, int playerId,
		float offX, float offY, float offZ,
		float rotX, float rotY, float rotZ) = 0;
	virtual bool attachObjectToVehicle(int objectId, int vehicleId,
		float offX, float offY, float offZ,
		float rotX, float rotY, float rotZ) = 0;

	/// text may be UTF-8; streamer converts internally. All fields mirror the PAWN native.
	virtual bool setObjectMaterial(int objectId, int materialIndex,
		int modelId, const char* txdName, const char* textureName,
		uint32_t materialColor) = 0;
	virtual bool setObjectMaterialText(int objectId, int materialIndex,
		const char* text, int materialSize, const char* fontFace,
		int fontSize, bool bold, uint32_t fontColor, uint32_t backColor,
		int alignment) = 0;

	/// Starts client-side object editing UI for the player. playerId must be valid.
	virtual bool editObject(int playerId, int objectId) = 0;

	// ============================================================================
	// Pickups
	// ============================================================================

	virtual int createPickup(int modelId, int type,
		float posX, float posY, float posZ,
		int worldId = -1, int interiorId = -1, int playerId = -1,
		float streamDistance = 200.0f,
		int areaId = -1, int priority = 0) = 0;
	virtual bool destroyPickup(int pickupId) = 0;
	virtual bool isValidPickup(int pickupId) = 0;

	// ============================================================================
	// 3D Text Labels
	// ============================================================================

	virtual int createTextLabel(const char* text, uint32_t color,
		float posX, float posY, float posZ, float drawDistance,
		int attachedPlayer = -1, int attachedVehicle = -1, bool testLos = false,
		int worldId = -1, int interiorId = -1, int playerId = -1,
		float streamDistance = 200.0f,
		int areaId = -1, int priority = 0) = 0;
	virtual bool destroyTextLabel(int labelId) = 0;
	virtual bool updateTextLabelText(int labelId, uint32_t color, const char* text) = 0;
	virtual bool isValidTextLabel(int labelId) = 0;

	// ============================================================================
	// Map Icons
	// ============================================================================

	virtual int createMapIcon(float posX, float posY, float posZ,
		int type, uint32_t color,
		int worldId = -1, int interiorId = -1, int playerId = -1,
		float streamDistance = 200.0f, int style = 0,
		int areaId = -1, int priority = 0) = 0;
	virtual bool destroyMapIcon(int iconId) = 0;
	virtual bool isValidMapIcon(int iconId) = 0;

	// ============================================================================
	// Checkpoints (race checkpoints covered by a separate API in a later iteration)
	// ============================================================================

	virtual int createCheckpoint(float posX, float posY, float posZ, float size,
		int worldId = -1, int interiorId = -1, int playerId = -1,
		float streamDistance = 200.0f,
		int areaId = -1, int priority = 0) = 0;
	virtual bool destroyCheckpoint(int cpId) = 0;
	virtual bool isValidCheckpoint(int cpId) = 0;

	// ============================================================================
	// Actors
	// ============================================================================

	virtual int createActor(int modelId, float x, float y, float z, float rotation,
		bool invulnerable, float health, float streamDistance,
		int worldId = -1, int interiorId = -1, int playerId = -1,
		int areaId = -1, int priority = 0) = 0;
	virtual bool destroyActor(int actorId) = 0;
	virtual bool isValidActor(int actorId) = 0;

	virtual bool applyActorAnimation(int actorId, const char* animLib, const char* animName,
		float delta, bool loop, bool lockX, bool lockY, bool freeze, int timeMs) = 0;
	virtual bool clearActorAnimations(int actorId) = 0;

	virtual bool getActorPos(int actorId, float& outX, float& outY, float& outZ) = 0;
	virtual bool setActorPos(int actorId, float x, float y, float z) = 0;
	virtual bool getActorFacingAngle(int actorId, float& outAngle) = 0;
	virtual bool setActorFacingAngle(int actorId, float angle) = 0;

	virtual bool getActorHealth(int actorId, float& outHealth) = 0;
	virtual bool setActorHealth(int actorId, float health) = 0;
	virtual bool isActorInvulnerable(int actorId) = 0;
	virtual bool setActorInvulnerable(int actorId, bool invulnerable) = 0;
	virtual int getActorVirtualWorld(int actorId) = 0;
	virtual bool setActorVirtualWorld(int actorId, int worldId) = 0;

	// ============================================================================
	// Telemetry (VS:RP fork) — per-phase cumulative timings + stream-in/out counters.
	// All counters are cumulative since the last resetPhaseStats() call.
	// `type` is one of STREAMER_TYPE_* (defined in common.h).
	// ============================================================================

	/// Nanoseconds spent in the process* loop for `type` (clamped to uint64 max).
	virtual uint64_t getPhaseTimeNs(int type) = 0;
	/// Average microseconds per player-tick in phase `type`. Divides by the number
	/// of recorded player-ticks since last reset; returns 0 if no ticks yet.
	virtual uint64_t getPhaseAvgUs(int type) = 0;
	/// Total number of per-player ticks recorded since last reset.
	virtual uint64_t getPhaseTickCount() = 0;
	/// Count of client-create (CreatePlayerObject / SetPlayerMapIcon /
	/// CreatePlayer3DTextLabel) calls issued since last reset.
	virtual uint64_t getPhaseStreamInCount(int type) = 0;
	/// Count of client-destroy calls issued since last reset.
	virtual uint64_t getPhaseStreamOutCount(int type) = 0;
	/// Zero every telemetry counter. Typically call once a minute after reading.
	virtual void resetPhaseStats() = 0;

	// ============================================================================
	// Anti-flicker hysteresis (VS:RP fork). Stream-out stickiness for per-player
	// items (OBJECT / MAP_ICON / 3D_TEXT_LABEL). Default 1.0 = legacy behavior.
	// ============================================================================

	virtual float getHysteresisFactor(int type) = 0;
	/// Valid range [1.0, 10.0]. Returns false on invalid type/value.
	virtual bool setHysteresisFactor(int type, float value) = 0;

	// ============================================================================
	// Two-tier grid (VS:RP fork). Items with streamDistance between cellDistance
	// and coarseCellDistance bucket into coarser cells instead of globalCell.
	// Set coarseCellDistance = 0 to disable the tier.
	// ============================================================================

	virtual float getCoarseCellSize() = 0;
	virtual bool setCoarseCellSize(float size) = 0;  // rebuilds grid
	virtual float getCoarseCellDistance() = 0;
	virtual bool setCoarseCellDistance(float distance) = 0;  // rebuilds grid

	// ============================================================================
	// Event handlers
	// ============================================================================

	/// Register a C++ event handler. Handler lifetime is caller-owned: call
	/// removeEventHandler before destroying the pointer. Safe to call multiple times
	/// with the same pointer (duplicates are ignored).
	virtual void addEventHandler(IStreamerEventHandler* handler) = 0;
	virtual void removeEventHandler(IStreamerEventHandler* handler) = 0;

	// IExtension
	void reset() override { /* streamer handles its own reset via Core; no-op here */ }
};

// Defined in streamer_component_api.cpp. Returns the process-wide extension singleton
// that StreamerComponent::getExtension() hands out when asked for IStreamerComponent.
IStreamerComponent *GetStreamerExtension();

// Flat list of currently-registered C++ event handlers (defined in streamer_component_api.cpp).
// Used by callbacks.cpp / streamer.cpp to forward events alongside the AMX-public dispatch.
#include <vector>
const std::vector<IStreamerEventHandler*>& GetStreamerEventHandlers();

#endif
