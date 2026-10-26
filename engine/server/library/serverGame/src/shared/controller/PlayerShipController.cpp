// ======================================================================
//
// PlayerShipController.cpp
// asommers
//
// copyright 2003, sony online entertainment
//
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/PlayerShipController.h"

#include "serverGame/AiShipBehaviorDock.h"
#include "serverGame/ConfigServerGame.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/PlayerShipTurretTargetingSystem.h"
#include "serverGame/ServerShipObjectInterface.h"
#include "serverGame/ShipAiEnemySearchManager.h"
#include "serverGame/ShipClientUpdateTracker.h"
#include "serverGame/ShipObject.h"
#include "serverGame/TangibleObject.h"
#include "serverScript/GameScriptObject.h"
#include "serverScript/ScriptParameters.h"
#include "sharedDebug/DebugFlags.h"
#include "sharedFoundation/ConstCharCrcString.h"
#include "sharedFoundation/GameControllerMessage.h"
#include "sharedGame/ShipDynamicsModel.h"
#include "sharedGame/ShipObjectInterface.h"
#include "sharedLog/Log.h"
#include "sharedMathArchive/TransformArchive.h"
#include "sharedNetworkMessages/MessageQueueDataTransform.h"
#include "sharedNetworkMessages/MessageQueueGenericValueType.h"
#include "sharedNetworkMessages/MessageQueueSpaceMiningSellResource.h"
#include "sharedNetworkMessages/MessageQueueTeleportAck.h"
#include "sharedNetworkMessages/ShipUpdateTransformCollisionMessage.h"
#include "sharedNetworkMessages/ShipUpdateTransformMessage.h"
#include "sharedObject/AlterResult.h"
#include "sharedObject/NetworkIdManager.h"
#include "serverGame/ServerWorld.h"
#include "UnicodeUtils.h"
#include "serverGame/Chat.h"
#include "serverGame/Client.h"
#include "sharedCollision/CollisionWorld.h"
#include "sharedFoundation/Clock.h"
#include "sharedTerrain/TerrainObject.h"

#include <limits>
#include <map>

// ======================================================================
// PlayerShipControllerNamespace
// ======================================================================

namespace PlayerShipControllerNamespace
{
	bool ms_logHandleNetUpdateTransform;

	void remove();

	float const s_targetedByAiExpireTime = 20.0f;
	/** Meters per second vertical climb/descent during atmospheric autopilot (ascend/descend phases). */
	float const s_autopilotElevatorSpeed = 60.0f;

	void queueShipUpdatesForObservers(ShipObject & ship)
	{
		std::set<Client *> const & observers = ship.getObservers();
		for (std::set<Client *>::const_iterator i = observers.begin(); i != observers.end(); ++i)
		{
			Client * const client = *i;
			if (client)
				ShipClientUpdateTracker::queueForUpdate(*client, ship);
		}
	}

	int syncStampLongDeltaTime(uint32 stamp1, uint32 stamp2)
	{
		uint32 const delta = stamp1 - stamp2;
		if (delta > 0x7fffffff)
			return static_cast<int>(0xffffffff-delta);
		return static_cast<int>(delta);
	}

	class AutopilotShipObjectInterface : public ShipObjectInterface
	{
	public:
		explicit AutopilotShipObjectInterface(ShipObject const * shipObject)
		: m_shipObject(shipObject)
		{
		}

		virtual ~AutopilotShipObjectInterface() {}

		virtual float getMaximumSpeed() const
		{
			float baseSpeed = m_shipObject->getShipActualSpeedMaximum();
			return baseSpeed * 2.5f;
		}

		virtual float getSpeedAcceleration() const { return m_shipObject->getShipActualAccelerationRate() * 5.0f; }
		virtual float getSpeedDeceleration() const { return m_shipObject->getShipActualDecelerationRate() * 5.0f; }
		virtual float getSlideDampener() const     { return 0.0f; }
		virtual float getMaximumYaw() const        { return 2.0f; }
		virtual float getYawAcceleration() const   { return 4.0f; }
		virtual float getMaximumPitch() const      { return 0.0f; }
		virtual float getPitchAcceleration() const { return 0.0f; }
		virtual float getMaximumRoll() const       { return 0.0f; }
		virtual float getRollAcceleration() const  { return 0.0f; }

	private:
		ShipObject const * m_shipObject;
	};

	/** After the last flight path key, keep streaming transforms this long so every client settles exactly on it. */
	float const s_flightPathSettleTime = 2.0f;

	/** Cubic Hermite between p0 and p1 (tangents m0/m1 per second, segment length dt seconds) at s in [0, 1]. */
	void hermite(Vector const & p0, Vector const & m0, Vector const & p1, Vector const & m1, float const dt, float const s, Vector & value, Vector & rate)
	{
		float const s2 = s * s;
		float const s3 = s2 * s;
		value = p0 * (2.0f * s3 - 3.0f * s2 + 1.0f) + m0 * ((s3 - 2.0f * s2 + s) * dt) + p1 * (3.0f * s2 - 2.0f * s3) + m1 * ((s3 - s2) * dt);
		rate = (p0 * (6.0f * s2 - 6.0f * s) + m0 * ((3.0f * s2 - 4.0f * s + 1.0f) * dt) + p1 * (6.0f * s - 6.0f * s2) + m1 * ((3.0f * s2 - 2.0f * s) * dt)) * (1.0f / dt);
	}

	Vector flightPathPosition(PlayerShipController::FlightPathKey const & key)
	{
		return key.position;
	}

	Vector flightPathAngles(PlayerShipController::FlightPathKey const & key)
	{
		return Vector(key.yaw, key.pitch, key.roll);
	}

	/**
	 * Titan: no-fly zones (atmospheric flight): vertical cylinders round starports, registered by script (traffic
	 * control, space.atmo.starport_traffic) under the object that owns them. A pilot may not fly a ship into one or
	 * deeper into it; ships the server flies (autopilot, scripted paths: landing and leaving through traffic control)
	 * and gods are exempt.
	 */
	struct NoFlyZone
	{
		float x, z, radius, ceilingY;
	};
	typedef std::map<NetworkId, NoFlyZone> NoFlyZones;
	NoFlyZones s_noFlyZones;
	unsigned long const s_noFlyWarnIntervalMs = 4000;

	/** How far into a no-fly zone pos is (0 outside): to the zone's wall or up through its ceiling, whichever is nearer. */
	float noFlyPenetration(Vector const & pos)
	{
		float deepest = 0.0f;
		for (NoFlyZones::const_iterator i = s_noFlyZones.begin(); i != s_noFlyZones.end(); ++i)
		{
			NoFlyZone const & zone = i->second;
			float const dx = pos.x - zone.x;
			float const dz = pos.z - zone.z;
			float const toWall = zone.radius - sqrt(dx * dx + dz * dz);
			float const toCeiling = zone.ceilingY - pos.y;
			float const depth = std::min(toWall, toCeiling);
			if (depth > deepest)
				deepest = depth;
		}
		return deepest;
	}

	/** Titan: how far over a building floor a ship holds where the terrain is cut away (a starport's landing well). */
	float const s_shipFloorClearance = 2.0f;

	/**
	 * Titan: the lowest a ship may fly at pos in an atmospheric scene: 15 m over the terrain, or, where the terrain is
	 * cut away (a landing well and its bays), just over the building floor under it - so a pilot can fly down the well
	 * and set down in a bay, but not sink through a bay's floor. False when there is nothing under it (the open shaft).
	 */
	bool getShipMinimumAltitude(Vector const & pos, float & outMinimumY)
	{
		TerrainObject const * const terrain = TerrainObject::getConstInstance();
		float terrainHeight = 0.0f;
		bool const hasTerrain = terrain && terrain->getHeightForceChunkCreation(Vector(pos.x, 0.0f, pos.z), terrainHeight);

		//-- a building floor under a ship that is below the terrain's height: the terrain is cut away there (a
		//-- terrain chunk that isn't cut may still answer), so the floor is the ground
		float floorHeight = 0.0f;
		if (CollisionWorld::findObjectFloorHeightBelow(pos, floorHeight) && (!hasTerrain || pos.y < terrainHeight))
		{
			outMinimumY = floorHeight + s_shipFloorClearance;
			return true;
		}

		if (hasTerrain)
		{
			outMinimumY = terrainHeight + 15.0f;
			return true;
		}

		return false;
	}

	/** Catmull-Rom tangent at key i of a timed path; the path starts and ends at rest. */
	Vector flightPathTangent(std::vector<PlayerShipController::FlightPathKey> const & keys, size_t const i, Vector (*getter)(PlayerShipController::FlightPathKey const &))
	{
		if (i == 0 || i + 1 >= keys.size())
			return Vector::zero;
		float const dt = keys[i + 1].time - keys[i - 1].time;
		if (dt <= 0.0f)
			return Vector::zero;
		return (getter(keys[i + 1]) - getter(keys[i - 1])) * (1.0f / dt);
	}
}

using namespace PlayerShipControllerNamespace;

// ======================================================================

void PlayerShipController::install()
{
	DebugFlags::registerFlag(ms_logHandleNetUpdateTransform, "ServerGame/RemoteShipController", "logHandleNetUpdateTransform*");
	ExitChain::add(&remove, "PlayerShipController::remove");
}

// ----------------------------------------------------------------------

void PlayerShipControllerNamespace::remove()
{
	DebugFlags::unregisterFlag(ms_logHandleNetUpdateTransform);
}

// ======================================================================
// PUBLIC PlayerShipController
// ======================================================================

PlayerShipController::PlayerShipController(ShipObject * const owner) :
	ShipController(owner),
	m_clientToServerLastSyncStamp(0),
	m_teleportIds(),
	m_shipAccelRate(owner->getShipActualAccelerationRate(), ConfigServerGame::getShipMoveCheckIntervalMs(), ConfigServerGame::getShipMoveCheckFudgeTimeMs()),
	m_shipDecelRate(owner->getShipActualDecelerationRate(), ConfigServerGame::getShipMoveCheckIntervalMs(), ConfigServerGame::getShipMoveCheckFudgeTimeMs()),
	m_shipYprMaximum(owner->getShipActualYprRateMaximum(), ConfigServerGame::getShipMoveCheckIntervalMs(), ConfigServerGame::getShipMoveCheckFudgeTimeMs()),
	m_shipSpeedMaximum(owner->getShipActualSpeedMaximum(), ConfigServerGame::getShipMoveCheckIntervalMs(), ConfigServerGame::getShipMoveCheckFudgeTimeMs()),
	m_lastVerifiedTransform(),
	m_lastVerifiedSpeed(0.f),
	m_lastVerifiedSyncStamp(0),
	m_gunnerWeaponIndexList(new GunnerWeaponIndexList),
	m_targetedByAiTimer(s_targetedByAiExpireTime),
	m_autopilotActive(false),
	m_autopilotTarget(Vector::zero),
	m_autopilotTakeoffAltitude(500.0f),
	m_autopilotLandingAltitude(200.0f),
	m_autopilotPhase(AP_NONE),
	m_autopilotDesiredY(0.0f),
	m_autopilotHoldCruise(false),
	m_autopilotFighterMode(false),
	m_autopilotHasLandingY(false),
	m_autopilotLandingY(0.0f),
	m_serverDrivenDocking(false),
	m_noFlyWarnedAtMs(0),
	m_flightPath(),
	m_flightPathElapsed(0.0f),
	m_flightPathHoldTime(0.0f)
{
	preventMovementUpdates();
}

// ----------------------------------------------------------------------

PlayerShipController::~PlayerShipController()
{
	delete m_gunnerWeaponIndexList;
}

// ----------------------------------------------------------------------

void PlayerShipController::endBaselines()
{
	if (getAuthoritative())
	{
		ShipObject * const ship = getShipOwner();
		ship->clearCondition(static_cast<int>(TangibleObject::C_docking));

		m_shipAccelRate.setValue(ship->getShipActualAccelerationRate(), 0);
		m_shipDecelRate.setValue(ship->getShipActualDecelerationRate(), 0);
		m_shipYprMaximum.setValue(ship->getShipActualYprRateMaximum(), 0);
		m_shipSpeedMaximum.setValue(ship->getShipActualSpeedMaximum(), 0);
	}

	m_yawPosition = 0.f;
	m_pitchPosition = 0.f;
	m_rollPosition = 0.f;
	m_throttlePosition = 0.f;

	ShipController::endBaselines();
}

// ----------------------------------------------------------------------

void PlayerShipController::setAuthoritative(bool newAuthoritative)
{
	ShipController::setAuthoritative(newAuthoritative);

	if (!newAuthoritative)
		preventMovementUpdates();
	else
	{
		ShipObject const * const ship = NON_NULL(getShipOwner());
		if (ship->getClient())
			resyncMovementUpdates();

		m_shipAccelRate.setValue(ship->getShipActualAccelerationRate(), 0);
		m_shipDecelRate.setValue(ship->getShipActualDecelerationRate(), 0);
		m_shipYprMaximum.setValue(ship->getShipActualYprRateMaximum(), 0);
		m_shipSpeedMaximum.setValue(ship->getShipActualSpeedMaximum(), 0);
	}
}

// ----------------------------------------------------------------------

void PlayerShipController::onClientReady()
{
	m_clientToServerLastSyncStamp = 0;
	if (getAuthoritative())
		resyncMovementUpdates();
}

// ----------------------------------------------------------------------

void PlayerShipController::onClientLost()
{
	m_yawPosition = 0.f;
	m_pitchPosition = 0.f;
	m_rollPosition = 0.f;
	m_throttlePosition = 0.f;
	preventMovementUpdates();
	unDock();
}

// ----------------------------------------------------------------------

void PlayerShipController::receiveTransform(ShipUpdateTransformMessage const & shipUpdateTransformMessage)
{
	if (isTeleporting())
		return;

	if (m_autopilotActive || !m_flightPath.empty())
		return;

	ShipObject * const owner = NON_NULL(getShipOwner());

	if (owner->hasCondition(static_cast<int>(TangibleObject::C_docking)))
		return;

	uint32 const syncStamp = shipUpdateTransformMessage.getSyncStampLong();
	if (   m_clientToServerLastSyncStamp == 0
	    || syncStampLongDeltaTime(m_clientToServerLastSyncStamp, syncStamp) > 0)
	{
		m_clientToServerLastSyncStamp = syncStamp;

		Transform transform = shipUpdateTransformMessage.getTransform();
		Vector const &velocity = shipUpdateTransformMessage.getVelocity();
		float const speed = velocity.magnitude();

		if (ServerWorld::isAtmosphericFlightScene())
		{
			Vector const pos = transform.getPosition_p();
			float minAltitude = 0.0f;
			if (getShipMinimumAltitude(pos, minAltitude) && pos.y < minAltitude)
				transform.setPosition_p(Vector(pos.x, minAltitude, pos.z));
		}

		//-- Titan: no flying into (or on into) a starport's no-fly zone; the move is refused and the ship put back
		if (ServerWorld::isAtmosphericFlightScene() && !s_noFlyZones.empty())
		{
			Client const * const pilotClient = owner->getClient();
			if (!pilotClient || !pilotClient->isGod())
			{
				float const before = noFlyPenetration(owner->getPosition_p());
				float const after = noFlyPenetration(transform.getPosition_p());
				if (after > 0.0f && after > before + 0.25f)
				{
					unsigned long const now = Clock::timeMs();
					CreatureObject * const pilot = owner->getPilot();
					if (pilot && now - m_noFlyWarnedAtMs > s_noFlyWarnIntervalMs)
					{
						m_noFlyWarnedAtMs = now;
						Chat::sendSystemMessage(*pilot, Unicode::narrowToWide("\\#ff4444[Traffic Control]: Restricted airspace. Request landing clearance from the planet map; traffic control will bring you in."), Unicode::emptyString);
					}
					teleport(owner->getTransform_o2p(), 0);
					return;
				}
			}
		}

		if (!checkValidMove(transform, velocity, speed, syncStamp))
			teleport(m_lastVerifiedTransform, 0);
		else
		{
			m_shipDynamicsModel->setTransform(transform);
			m_shipDynamicsModel->setVelocity(velocity);

			ServerShipObjectInterface const serverShipObjectInterface(owner);

			//-- Transform speeds into throttle positions and compute delta time
			m_throttlePosition = 0.f;
			m_yawPosition = 0.f;
			m_pitchPosition = 0.f;
			m_rollPosition = 0.f;
			float deltaTime = 0.f;

			Client const * const client = owner->getClient();
			if (client)
			{
				CreatureObject const * const pilot = safe_cast<CreatureObject const *>(client->getCharacterObject());
				if (pilot && !pilot->getState(States::Frozen))
				{
					if (serverShipObjectInterface.getMaximumSpeed() > 0.f)
						m_throttlePosition = clamp(0.f, speed / serverShipObjectInterface.getMaximumSpeed(), 1.f);
					if (serverShipObjectInterface.getMaximumYaw() > 0.f)
						m_yawPosition = clamp(-1.f, shipUpdateTransformMessage.getYawRate() / serverShipObjectInterface.getMaximumYaw(), 1.f);
					if (serverShipObjectInterface.getMaximumPitch() > 0.f)
						m_pitchPosition = clamp(-1.f, shipUpdateTransformMessage.getPitchRate() / serverShipObjectInterface.getMaximumPitch(), 1.f);
					if (serverShipObjectInterface.getMaximumRoll() > 0.f)
						m_rollPosition = clamp(-1.f, shipUpdateTransformMessage.getRollRate() / serverShipObjectInterface.getMaximumRoll(), 1.f);

					deltaTime = client->computeDeltaTimeInSeconds(syncStamp);
				}
			}

			//-- Advance by delta time
			m_shipDynamicsModel->model(deltaTime, m_yawPosition, m_pitchPosition, m_rollPosition, m_throttlePosition, serverShipObjectInterface);

			DEBUG_REPORT_LOG(ms_logHandleNetUpdateTransform, ("PlayerShipController::handleNetUpdateTransform[%s]: %1.2f\n", owner->getNetworkId().getValueString().c_str(), m_throttlePosition));
		}
	}
	else
		DEBUG_REPORT_LOG(true, ("PlayerShipController::handleNetUpdateTransform[%s]: disregarding older packet\n", owner->getNetworkId().getValueString().c_str()));
}

// ----------------------------------------------------------------------

void PlayerShipController::teleport(Transform const &goal, ServerObject *goalObj)
{
	ShipObject * const ship = NON_NULL(getShipOwner());
	Client * const client = ship->getClient();

	if (client && !goalObj)
	{
		int const sequenceId = static_cast<int>(ship->getAndIncrementMoveSequenceNumber());
		IGNORE_RETURN(m_teleportIds.insert(sequenceId));

		appendMessage(
			CM_netUpdateTransform,
			0.f,
			new MessageQueueDataTransform(0, sequenceId, goal, 0.f, 0.f, false),
			GameControllerMessageFlags::RELIABLE|GameControllerMessageFlags::SEND|GameControllerMessageFlags::DEST_AUTH_CLIENT);

		m_lastVerifiedTransform = goal;
		m_lastVerifiedSpeed = 0;
		m_lastVerifiedSyncStamp = getCurSyncStamp();
	}

	ShipController::teleport(goal, goalObj);
}

// ----------------------------------------------------------------------

void PlayerShipController::handleMessage(int const message, float const value, MessageQueue::Data const * const data, uint32 const flags)
{
	switch (message)
	{
	case CM_teleportAck:
		{
			MessageQueueTeleportAck const * const msg = dynamic_cast<MessageQueueTeleportAck const *>(data);
			if (msg)
				handleTeleportAck(msg->getSequenceId());
		}
		break;
	case CM_shipStopFiring:
		{
			ShipObject * const ship = NON_NULL(getShipOwner());

			typedef MessageQueueGenericValueType<int> MessageType;			
			MessageType const * const msg = dynamic_cast<MessageType const *>(data);

			if (msg)
			{
				int const weaponIndex = msg->getValue();
				ship->stopFiringWeapon(weaponIndex);
			}
		}
		break;

	case CM_spaceMiningSaleSellResource:
		{
			MessageQueueSpaceMiningSellResource const * const msg = dynamic_cast<MessageQueueSpaceMiningSellResource const *>(data);
			if (msg != nullptr)
			{
				ServerObject * const spaceStation = safe_cast<ServerObject *>(NetworkIdManager::getObjectById(msg->m_spaceStationId));
				if (nullptr == spaceStation)
				{
					WARNING(true, ("PlayerCreatureController CM_spaceMiningSaleSellResource invalid station [%s]", msg->m_spaceStationId.getValueString().c_str()));
				}
				else
				{
					GameScriptObject * const scriptObject = spaceStation->getScriptObject();
					if (scriptObject)
					{
						ScriptParams params;
						params.addParam(getOwner()->getNetworkId());
						params.addParam(msg->m_shipId);
						params.addParam(msg->m_spaceStationId);
						params.addParam(msg->m_resourceId);
						params.addParam(msg->m_amount);
						scriptObject->trigAllScripts(Scripting::TRIG_SPACE_MINING_SELL_RESOURCE, params);
					}
				}
			}
		}
		break;

	default:
		ShipController::handleMessage(message, value, data, flags);
		break;
	}
}

// ----------------------------------------------------------------------

void PlayerShipController::onShipAccelerationRateChanged(float value)
{
	m_shipAccelRate.setValue(value, getCurSyncStamp());
}

// ----------------------------------------------------------------------

void PlayerShipController::onShipDecelerationRateChanged(float value)
{
	m_shipDecelRate.setValue(value, getCurSyncStamp());
}

// ----------------------------------------------------------------------

void PlayerShipController::onShipYprMaximumChanged(float value)
{
	m_shipYprMaximum.setValue(value, getCurSyncStamp());
}

// ----------------------------------------------------------------------

void PlayerShipController::onShipSpeedMaximumChanged(float value)
{
	m_shipSpeedMaximum.setValue(value, getCurSyncStamp());
}

// ----------------------------------------------------------------------

bool PlayerShipController::isTeleporting() const
{
	return !m_teleportIds.empty();
}

// ======================================================================
// PROTECTED PlayerShipController
// ======================================================================

float PlayerShipController::realAlter(float const elapsedTime)
{
	PROFILER_AUTO_BLOCK_DEFINE("PlayerShipController::realAlter");

	ShipObject * const owner = getShipOwner();

	if (!owner || !owner->isInWorld())
		return ShipController::realAlter(elapsedTime);

	if (owner && owner->isInitialized())
	{
		if (m_pendingDockingBehavior != nullptr)
		{
			delete m_dockingBehavior;
			m_dockingBehavior = m_pendingDockingBehavior;
			m_pendingDockingBehavior = nullptr;

			getShipOwner()->setCondition(static_cast<int>(TangibleObject::C_docking));
			Client * const client = owner->getClient();

			if (client != nullptr)
			{
				ShipClientUpdateTracker::queueForUpdate(*client, *owner);
			}
			else
			{
				unDock();
			}
		}

		if (   (m_dockingBehavior != nullptr)
		    && m_dockingBehavior->isDockFinished())
		{
			delete m_dockingBehavior;
			m_dockingBehavior = nullptr;

			getShipOwner()->clearCondition(static_cast<int>(TangibleObject::C_docking));
		}

		if (m_dockingBehavior != nullptr)
		{
			//-- The docking behavior will calculate new values for m_yaw/m_pitch/m_roll/m_throttle[Position] implicitly through calling ShipController members
			m_dockingBehavior->alter(elapsedTime);
		}

		if (m_autopilotActive && m_dockingBehavior == nullptr)
		{
			Vector const shipPos = owner->getPosition_w();

			float const elevatorSpeed = 30.0f;
			float const elevatorStep = elevatorSpeed * elapsedTime;

			switch (m_autopilotPhase)
			{
			case AP_ASCENDING:
				{
					m_yawPosition = 0.0f;
					m_pitchPosition = 0.0f;
					m_rollPosition = 0.0f;
					m_throttlePosition = 0.0f;

					float const altDelta = m_autopilotDesiredY - shipPos.y;
					if (altDelta <= elevatorStep)
						m_autopilotPhase = AP_CRUISING;
				}
				break;

			case AP_CRUISING:
				{
					Vector const delta(m_autopilotTarget.x - shipPos.x, 0.0f, m_autopilotTarget.z - shipPos.z);
					float const horizDist = delta.magnitude();

					// Compute yaw angle to target in ship-local space
					Transform const & shipTransform_w = owner->getTransform_o2w();
					Vector const goal_o = shipTransform_w.rotateTranslate_p2l(Vector(m_autopilotTarget.x, shipPos.y, m_autopilotTarget.z));
					float const goalYaw = goal_o.theta();

					float const deadZone = 0.02f; // ~1 degree
					if (fabs(goalYaw) < deadZone)
					{
						m_yawPosition = 0.0f;
						m_shipDynamicsModel->setYawRate(0.0f);
					}
					else
					{
						m_yawPosition = clamp(-1.0f, goalYaw * 3.0f, 1.0f);
					}

					m_pitchPosition = 0.0f;
					m_rollPosition = 0.0f;

					if (m_autopilotHoldCruise && !m_autopilotFighterMode && horizDist <= 35.0f)
					{
						m_throttlePosition = 0.0f;
						break;
					}

					if (m_autopilotFighterMode)
					{
						if (horizDist > 80.0f)
							m_throttlePosition = 1.0f;
						else if (horizDist > 30.0f)
							m_throttlePosition = clamp(0.2f, horizDist / 80.0f, 1.0f);
						else
							m_throttlePosition = clamp(0.1f, horizDist / 60.0f, 0.45f);
						break;
					}

					if (horizDist > 80.0f)
						m_throttlePosition = 1.0f;
					else if (horizDist > 30.0f)
						m_throttlePosition = clamp(0.2f, horizDist / 80.0f, 1.0f);
					else
					{
						m_throttlePosition = 0.0f;
						m_yawPosition = 0.0f;
						m_shipDynamicsModel->setYawRate(0.0f);

						if (m_autopilotHasLandingY)
							m_autopilotDesiredY = m_autopilotLandingY;      // Titan: a set height (a hover over a landing well)
						else
						{
							TerrainObject const * const terrain = TerrainObject::getConstInstance();
							float terrainAtDest = 0.0f;
							if (!terrain || !terrain->getHeightForceChunkCreation(Vector(m_autopilotTarget.x, 0.0f, m_autopilotTarget.z), terrainAtDest))
							{
								//-- Titan: no terrain there (cut away under a building): the building floor under the
								//-- cruise point instead of sea level
								float floorAtDest = 0.0f;
								if (CollisionWorld::findObjectFloorHeightBelow(Vector(m_autopilotTarget.x, shipPos.y, m_autopilotTarget.z), floorAtDest))
									terrainAtDest = floorAtDest;
							}
							m_autopilotDesiredY = terrainAtDest + m_autopilotLandingAltitude;
						}
						m_autopilotPhase = AP_DESCENDING;
					}
				}
				break;

			case AP_DESCENDING:
				{
					m_yawPosition = 0.0f;
					m_pitchPosition = 0.0f;
					m_rollPosition = 0.0f;
					m_throttlePosition = 0.0f;

					float const altDelta = shipPos.y - m_autopilotDesiredY;
					if (altDelta <= elevatorStep)
						m_autopilotPhase = AP_ARRIVED;
				}
				break;

			case AP_ARRIVED:
			default:
				m_yawPosition = 0.0f;
				m_pitchPosition = 0.0f;
				m_rollPosition = 0.0f;
				m_throttlePosition = 0.0f;
				break;
			}
		}

		//-- A scripted flight path owns the transform: no flight model and no terrain floor (it may end on the ground)
		if (!m_flightPath.empty())
			alterFlightPath(*owner, elapsedTime);
		else if (!owner->isInWorldCell())
		{
			//-- Titan: parked in another pob's cell (a vessel in a hangar).  Its transform is cell-relative, so the
			//-- world flight model and the terrain floor below would throw it out of the cell; it stays put.
		}
		else
		{
			//-- Update flight model
			if (m_autopilotActive && (m_autopilotPhase == AP_CRUISING))
			{
				AutopilotShipObjectInterface const autopilotInterface(owner);
				m_shipDynamicsModel->model(elapsedTime, m_yawPosition, m_pitchPosition, m_rollPosition, m_throttlePosition, autopilotInterface);
			}
			else
			{
				ServerShipObjectInterface const serverShipObjectInterface(owner);
				m_shipDynamicsModel->model(elapsedTime, m_yawPosition, m_pitchPosition, m_rollPosition, m_throttlePosition, serverShipObjectInterface);
			}

			//-- Update the server position based on the model
			Transform shipTransform = m_shipDynamicsModel->getTransform();

			if (m_autopilotActive && (m_autopilotPhase == AP_ASCENDING || m_autopilotPhase == AP_DESCENDING))
			{
				float const elevatorSpeed = s_autopilotElevatorSpeed;
				Vector pos = shipTransform.getPosition_p();
				if (m_autopilotPhase == AP_ASCENDING)
				{
					pos.y += elevatorSpeed * elapsedTime;
					if (pos.y >= m_autopilotDesiredY)
						pos.y = m_autopilotDesiredY;
				}
				else
				{
					pos.y -= elevatorSpeed * elapsedTime;
					if (pos.y <= m_autopilotDesiredY)
						pos.y = m_autopilotDesiredY;
				}
				shipTransform.setPosition_p(pos);
				m_shipDynamicsModel->setTransform(shipTransform);
				m_shipDynamicsModel->setVelocity(Vector::zero);
			}
			else if (m_autopilotActive && m_autopilotPhase == AP_CRUISING)
			{
				Vector pos = shipTransform.getPosition_p();
				pos.y = m_autopilotDesiredY;
				shipTransform.setPosition_p(pos);
				m_shipDynamicsModel->setTransform(shipTransform);
			}

			if (ServerWorld::isAtmosphericFlightScene())
			{
				Vector const pos = shipTransform.getPosition_p();
				float minAltitude = 0.0f;
				if (getShipMinimumAltitude(pos, minAltitude) && pos.y < minAltitude)
				{
					shipTransform.setPosition_p(Vector(pos.x, minAltitude, pos.z));
					m_shipDynamicsModel->setTransform(shipTransform);
				}
			}
			owner->setTransform_o2p(shipTransform);
		}

		//-- Titan: in atmospheric flight a ship flown by the server (its autopilot, a scripted path: a starport's
		//-- landing well) is marked docking, so its pilot's client follows the server's transform instead of flying
		//-- it from the seat (it ignores the server for its own ship otherwise); cleared when the server lets go
		if (ServerWorld::isAtmosphericFlightScene() && m_dockingBehavior == nullptr)
		{
			bool const serverDriven = (m_autopilotActive && m_autopilotPhase != AP_ARRIVED && m_autopilotPhase != AP_NONE)
				|| !m_flightPath.empty();
			if (serverDriven && !owner->hasCondition(static_cast<int>(TangibleObject::C_docking)))
			{
				owner->setCondition(static_cast<int>(TangibleObject::C_docking));
				m_serverDrivenDocking = true;
			}
			else if (!serverDriven && m_serverDrivenDocking)
			{
				owner->clearCondition(static_cast<int>(TangibleObject::C_docking));
				m_serverDrivenDocking = false;
			}
		}

		// Server autopilot drives transform while receiveTransform() ignores client packets — push
		// ShipUpdateTransform to all observers (including the pilot) each alter tick.
		if (m_autopilotActive)
			queueShipUpdatesForObservers(*owner);

		// Request new enemies for any turrets

		if (   hasTurretTargetingSystem()
		    && !m_enemyCheckQueued)
		{
			m_enemyCheckQueued = true;
			ShipAiEnemySearchManager::add(*owner);
		}

		if (m_targetedByAiTimer.updateNoReset(elapsedTime))
		{
			owner->clearCondition(static_cast<int>(TangibleObject::C_spaceCombatMusic));
		}
	}

	//-- Chain back up to parent
	float alterResult = ShipController::realAlter(elapsedTime);

	AlterResult::incorporateAlterResult(alterResult, AlterResult::cms_alterNextFrame);
	return alterResult;
}

// ----------------------------------------------------------------------

void PlayerShipController::handleNetUpdateTransform(MessageQueueDataTransform const & /*message*/)
{
	//-- This message should be handled in handleMessage
	DEBUG_FATAL(true, ("PlayerShipController::handleNetUpdateTransform: not implemented (nor should it be)"));
}

// ----------------------------------------------------------------------

void PlayerShipController::handleNetUpdateTransformWithParent(MessageQueueDataTransformWithParent const & /*message*/)
{
	//-- This message should be handled in handleMessage
	DEBUG_FATAL(true, ("PlayerShipController::handleNetUpdateTransformWithParent: not implemented (nor should it be)"));
}

// ======================================================================
// PRIVATE PlayerShipController
// ======================================================================

void PlayerShipController::preventMovementUpdates()
{
	m_teleportIds.clear();
	IGNORE_RETURN(m_teleportIds.insert(-1));
}

// ----------------------------------------------------------------------

void PlayerShipController::handleTeleportAck(int sequenceId)
{
	IGNORE_RETURN(m_teleportIds.erase(sequenceId));
}

// ----------------------------------------------------------------------

void PlayerShipController::resyncMovementUpdates()
{
	// We resync updates by doing a fake teleport, giving a negative
	// sequence so the client does not actually do the movement.
	// This locks out movement updates until the teleport is acknowledged.

	ShipObject * const ship = NON_NULL(getShipOwner());
	Client * const client = ship->getClient();

	FATAL(!client || !ship->isAuthoritative(), ("Tried to resync ship movement updates either while nonauth or without a client!"));

	m_teleportIds.clear();

	int sequenceId = -1;
	while (sequenceId == -1)
		sequenceId = -static_cast<int>(ship->getAndIncrementMoveSequenceNumber());

	IGNORE_RETURN(m_teleportIds.insert(sequenceId));

	appendMessage(
		CM_netUpdateTransform,
		0.f,
		new MessageQueueDataTransform(0, sequenceId, Transform::identity, 0.f, 0.f, false),
		GameControllerMessageFlags::RELIABLE|GameControllerMessageFlags::SEND|GameControllerMessageFlags::DEST_AUTH_CLIENT);

	m_lastVerifiedTransform = m_shipDynamicsModel->getTransform();
	m_lastVerifiedSpeed = m_shipDynamicsModel->getSpeed();
	m_lastVerifiedSyncStamp = getCurSyncStamp();
}

// ----------------------------------------------------------------------

void PlayerShipController::experiencedCollision()
{
	ShipObject * const owner = getShipOwner();
	if (owner != 0)
	{
		Client* client = owner->getClient();

		if (client != 0)
		{
			ShipUpdateTransformCollisionMessage const collisionMessage(
				owner->getNetworkId(),
				getTransform(),
				getVelocity(),
				client->getServerSyncStampLong());
			client->send(collisionMessage, false);
		}
	}
}

// ----------------------------------------------------------------------

uint32 PlayerShipController::getCurSyncStamp() const
{
	Client const * const client = NON_NULL(getShipOwner())->getClient();
	return client ? client->getServerSyncStampLong() : 0;
}

// ----------------------------------------------------------------------

void PlayerShipController::logMoveFail(char const *reasonFmt, ...) const
{
	ShipObject const * const ship = NON_NULL(getShipOwner());
	Client const * const client = NON_NULL(ship->getClient());

	char reason[512];
	va_list ap;
	va_start(ap, reasonFmt);
	IGNORE_RETURN(_vsnprintf(reason, sizeof(reason)-1, reasonFmt, ap));
	reason[sizeof(reason)-1] = '\0';

	LOG(
		"shipmove",
		(
			"fail - ship %s creature %s stationId %u - %s",
			ship->getNetworkId().getValueString().c_str(),
			client->getCharacterObjectId().getValueString().c_str(),
			client->getStationId(),
			reason));
	va_end(ap);
}

// ----------------------------------------------------------------------

bool PlayerShipController::checkValidMove(Transform const &transform, Vector const &velocity, float speed, uint32 const currentClientSyncStamp)
{
	if (!ConfigServerGame::getShipMoveValidationEnabled())
		return true;

	ShipObject * const owner = NON_NULL(getShipOwner());
	Client const * const client = NON_NULL(owner->getClient());

	// gods can move however they like, and assume their moves are valid
	if (!client->isGod())
	{
		uint32 const currentServerSyncStamp = client->getServerSyncStampLong();
		uint32 const previousClientSyncStamp = m_lastVerifiedSyncStamp;
		int const timeDiffMs = syncStampLongDeltaTime(previousClientSyncStamp, currentClientSyncStamp);
		float const timeDiffSec = static_cast<float>(timeDiffMs)*0.001f;
		float const tolerance = ConfigServerGame::getShipMoveCheckTolerance();
		float const timeTolerance = ConfigServerGame::getShipMoveCheckTimeToleranceSeconds();

		// is our new speed valid?
		float const speedMaximum = m_shipSpeedMaximum.getRecentMaximum(currentServerSyncStamp);
		if (speed > speedMaximum*tolerance)
		{
			logMoveFail("too fast (speed=%g, speedMax=%g, timeDiff=%g, time %u)", speed, speedMaximum, timeDiffSec, currentClientSyncStamp);
			return false;
		}

		// Only do position/rotation validation periodically, since their timestamps
		// can be affected by latency variation.
		if (timeDiffMs < ConfigServerGame::getShipMoveCheckIntervalMs())
		{
			// assume we're ok to move, but don't update the last verified
			return true;
		}

		// check for bogus timestamps, making sure to handle timestamp rollover
		if (ConfigServerGame::getMoveValidationCheckForPastTimestamp())
		{
			// new timestamp from client must be greater than previous timestamp from client (i.e. cannot travel back in time)
			if (currentClientSyncStamp < previousClientSyncStamp)
			{
				uint32 const rolloverFudgeFactor = static_cast<uint32>(100000); // 100 seconds

				// pretty sure that timestamp did not rollover
				if ((previousClientSyncStamp > rolloverFudgeFactor) && ((std::numeric_limits<uint32>::max() - rolloverFudgeFactor) > previousClientSyncStamp))
				{
					int const timeDiffMs = syncStampLongDeltaTime(previousClientSyncStamp, currentClientSyncStamp);

					// only fail movement if the difference is beyond a certain threshold,
					// to account for network latency, UDP unreliability, clock drift, etc.
					if (timeDiffMs > ConfigServerGame::getMoveValidationPastTimestampThresholdMs())
					{
						logMoveFail("out of order timestamps (%dms), oss=%lu, css=%lu (%lud:%luh:%lum:%lus), ssc=%lu",
							timeDiffMs,
							previousClientSyncStamp,
							currentServerSyncStamp,
							(currentServerSyncStamp / 1000) / (60 * 60 * 24),
							((currentServerSyncStamp / 1000) % (60 * 60 * 24)) / (60 * 60),
							((currentServerSyncStamp / 1000) % (60 * 60)) / 60,
							(currentServerSyncStamp / 1000) % 60,
							currentClientSyncStamp);

						return false;
					}
				}
			}
		}

		// check for bogus timestamps, making sure to handle timestamp rollover
		if (ConfigServerGame::getMoveValidationCheckForFutureTimestamp())
		{
			// new timestamp from client must be less than the current server timestamp (i.e. cannot travel into the future)
			if (currentClientSyncStamp > currentServerSyncStamp)
			{
				uint32 const rolloverFudgeFactor = static_cast<uint32>(100000); // 100 seconds

				// pretty sure that timestamp did not rollover
				if ((currentServerSyncStamp > rolloverFudgeFactor) && ((std::numeric_limits<uint32>::max() - rolloverFudgeFactor) > currentServerSyncStamp))
				{
					int const timeDiffMs = syncStampLongDeltaTime(currentClientSyncStamp, currentServerSyncStamp);

					// only fail movement if the difference is beyond a certain threshold,
					// to account for network latency, UDP unreliability, clock drift, etc.
					if (timeDiffMs > ConfigServerGame::getMoveValidationFutureTimestampThresholdMs())
					{
						logMoveFail("timestamp from the future (%dms), oss=%lu, css=%lu (%lud:%luh:%lum:%lus), ssc=%lu",
							timeDiffMs,
							previousClientSyncStamp,
							currentServerSyncStamp,
							(currentServerSyncStamp / 1000) / (60 * 60 * 24),
							((currentServerSyncStamp / 1000) % (60 * 60 * 24)) / (60 * 60),
							((currentServerSyncStamp / 1000) % (60 * 60)) / 60,
							(currentServerSyncStamp / 1000) % 60,
							currentClientSyncStamp);

						return false;
					}
				}
			}
		}

		// make sure the client and server time hasn't drifted too far apart
		if (ConfigServerGame::getMoveValidationCheckForTimestampDrift())
		{
			int const clientServerTimeDiff = syncStampLongDeltaTime(currentClientSyncStamp, currentServerSyncStamp);
			if (clientServerTimeDiff > ConfigServerGame::getMoveValidationTimestampDriftThresholdMs())
			{
				logMoveFail("timestamp drift (%dms), oss=%lu, css=%lu (%lud:%luh:%lum:%lus), ssc=%lu",
					clientServerTimeDiff,
					previousClientSyncStamp,
					currentServerSyncStamp,
					(currentServerSyncStamp / 1000) / (60 * 60 * 24),
					((currentServerSyncStamp / 1000) % (60 * 60 * 24)) / (60 * 60),
					((currentServerSyncStamp / 1000) % (60 * 60)) / 60,
					(currentServerSyncStamp / 1000) % 60,
					currentClientSyncStamp);

				return false;
			}
		}

		// could we possibly have changed position enough?
		Vector const &lastPosition = m_lastVerifiedTransform.getPosition_p();
		Vector const &position = transform.getPosition_p();
		float const distanceSquared = lastPosition.magnitudeBetweenSquared(position);
		if (distanceSquared > sqr(speedMaximum*(timeDiffSec+timeTolerance)*tolerance)+0.2f)
		{
			logMoveFail(
				"too far (distSquared=%g, maxDistSquared=%g, timeDiff=%g from [%g,%g,%g] to [%g,%g,%g], time %u)",
				distanceSquared, sqr(speedMaximum*timeDiffSec), timeDiffSec,
				lastPosition.x, lastPosition.y, lastPosition.z,
				position.x, position.y, position.z, currentClientSyncStamp);
			return false;
		}

		// could we possibly have changed speeds enough?
		float const oldSpeed = m_lastVerifiedSpeed;
		if (speed > oldSpeed)
		{
			float const accel = (oldSpeed-speed)/(timeDiffSec+timeTolerance);
			float const maxAccel = m_shipAccelRate.getRecentMaximum(currentServerSyncStamp);
			if (accel > maxAccel*tolerance)
			{
				logMoveFail("too much acceleration (accel=%g, maxAccel=%g, time %u)", accel, maxAccel, currentClientSyncStamp);
				return false;
			}
		}
		else if (speed < oldSpeed)
		{
			float const decel = (speed-oldSpeed)/(timeDiffSec+timeTolerance);
			float const maxDecel = m_shipDecelRate.getRecentMaximum(currentServerSyncStamp);
			if (decel > maxDecel*tolerance)
			{
				logMoveFail("too much deceleration (decel=%g, maxDecel=%g, time %u)", decel, maxDecel, currentClientSyncStamp);
				return false;
			}
		}

		// could we possibly have changed orientation enough?
		float const rotation = acos(transform.getLocalFrameK_p().dot(m_lastVerifiedTransform.getLocalFrameK_p()))/(timeDiffSec+timeTolerance);
		float const maxRotation = m_shipYprMaximum.getRecentMaximum(currentServerSyncStamp);
		// Actual max rotation is roughly sqrt(2*sqr(maxRotation))+0.001f, to allow
		// for pitch/yaw/roll together being applied toward the same direction, and
		// adjust slightly for precision.
		if (sqr(rotation) > 2*sqr(maxRotation*tolerance)+0.001f)
		{
			logMoveFail("too much rotation (rotation=%g, maxRotation=%g, time %u)", rotation, maxRotation, currentClientSyncStamp);
			return false;
		}
	}

	UNREF(velocity);
	m_lastVerifiedTransform = transform;
	m_lastVerifiedSpeed = speed;
	m_lastVerifiedSyncStamp = currentClientSyncStamp;
	return true;
}

// ----------------------------------------------------------------------

PlayerShipController * PlayerShipController::asPlayerShipController()
{
	return this;
}

// ----------------------------------------------------------------------

PlayerShipController const * PlayerShipController::asPlayerShipController() const
{
	return this;
}

// ----------------------------------------------------------------------

bool PlayerShipController::shouldCheckForEnemies() const
{
	return (getShipOwner()->hasTurrets());
}

// ----------------------------------------------------------------------

void PlayerShipController::updateGunnerWeaponIndex(NetworkId const & player, int const gunnerWeaponIndex)
{
	GunnerWeaponIndexList::iterator iterGunnerWeaponIndexList = m_gunnerWeaponIndexList->find(player);

	if (iterGunnerWeaponIndexList != m_gunnerWeaponIndexList->end())
	{
		int const previousGunnerWeaponIndex = iterGunnerWeaponIndexList->second;
		iterGunnerWeaponIndexList->second = gunnerWeaponIndex;

		if (   (previousGunnerWeaponIndex >= 0)
			&& (gunnerWeaponIndex == -1))
		{
			// Gunner is exiting a gun

			setWeaponIndexPlayerControlled(previousGunnerWeaponIndex, false);
			m_gunnerWeaponIndexList->erase(iterGunnerWeaponIndexList);
		}
		else if (previousGunnerWeaponIndex != gunnerWeaponIndex)
		{
			// Gunner was previously in a gun and is moving to a new gun

			setWeaponIndexPlayerControlled(previousGunnerWeaponIndex, false);
			setWeaponIndexPlayerControlled(gunnerWeaponIndex, true);
		}
	}
	else
	{
		if (gunnerWeaponIndex >= 0)
		{
			// Gunner is entering a gun and was previously not in a gun

			setWeaponIndexPlayerControlled(gunnerWeaponIndex, true);
			IGNORE_RETURN(m_gunnerWeaponIndexList->insert(std::make_pair(player, gunnerWeaponIndex)));
		}
		else
		{
			// Gunner is not entering a gun and was not previously in a gun
		}
	}
}

// ----------------------------------------------------------------------

void PlayerShipController::setWeaponIndexPlayerControlled(int const weaponIndex, bool const playerControlled)
{
	DEBUG_FATAL((weaponIndex < 0), ("Invalid weaponIndex(%d)", weaponIndex));

	if (m_turretTargetingSystem != nullptr)
	{
		safe_cast<PlayerShipTurretTargetingSystem*>(m_turretTargetingSystem)->setWeaponIndexPlayerControlled(weaponIndex, playerControlled);
	}
}

// ----------------------------------------------------------------------

void PlayerShipController::addAiTargetingMe(NetworkId const & unit)
{
	ShipController::addAiTargetingMe(unit);

	if (m_targetedByAiTimer.isExpired())
	{
		getShipOwner()->setCondition(static_cast<int>(TangibleObject::C_spaceCombatMusic));
	}

	m_targetedByAiTimer.reset();
}

// ----------------------------------------------------------------------

void PlayerShipController::setAutopilotTarget(Vector const & target, float takeoffAltitude, float landingAltitude, bool holdCruise)
{
	setAutopilotTarget(target, takeoffAltitude, landingAltitude, holdCruise, false);
}

// ----------------------------------------------------------------------

void PlayerShipController::setAutopilotTarget(Vector const & target, float takeoffAltitude, float landingAltitude, bool holdCruise, bool fighterMode)
{
	m_autopilotActive = true;
	m_autopilotTarget = target;
	m_autopilotTakeoffAltitude = takeoffAltitude;
	m_autopilotLandingAltitude = landingAltitude;
	m_autopilotHoldCruise = holdCruise;
	m_autopilotFighterMode = fighterMode;
	m_autopilotHasLandingY = false;
	m_autopilotPhase = AP_ASCENDING;

	ShipObject * const ship = getShipOwner();
	if (ship)
	{
		Vector const shipPos = ship->getPosition_w();
		float terrainHere = 0.0f;
		if (ServerWorld::isAtmosphericFlightScene())
		{
			TerrainObject const * const terrain = TerrainObject::getConstInstance();
			if (terrain)
				terrain->getHeightForceChunkCreation(Vector(shipPos.x, 0.0f, shipPos.z), terrainHere);
		}
		m_autopilotDesiredY = terrainHere + m_autopilotTakeoffAltitude;

		Transform flatTransform;
		Vector const shipFwd = ship->getObjectFrameK_w();
		float const yawAngle = atan2(shipFwd.x, shipFwd.z);
		flatTransform.yaw_l(yawAngle);
		flatTransform.setPosition_p(shipPos);
		ship->setTransform_o2p(flatTransform);
	}

	if (m_shipDynamicsModel)
	{
		if (ship)
			m_shipDynamicsModel->setTransform(ship->getTransform_o2p());
		m_shipDynamicsModel->setVelocity(Vector::zero);
		m_shipDynamicsModel->setPitchRate(0.0f);
		m_shipDynamicsModel->setRollRate(0.0f);
		m_shipDynamicsModel->setYawRate(0.0f);
	}
}

// ----------------------------------------------------------------------

void PlayerShipController::setNoFlyZone(NetworkId const & owner, float const x, float const z, float const radius, float const ceilingY)
{
	NoFlyZone zone;
	zone.x = x;
	zone.z = z;
	zone.radius = radius;
	zone.ceilingY = ceilingY;
	s_noFlyZones[owner] = zone;
}

// ----------------------------------------------------------------------

void PlayerShipController::clearNoFlyZone(NetworkId const & owner)
{
	s_noFlyZones.erase(owner);
}

// ----------------------------------------------------------------------

void PlayerShipController::setAutopilotLandingY(float const y)
{
	m_autopilotHasLandingY = true;
	m_autopilotLandingY = y;
}

// ----------------------------------------------------------------------

void PlayerShipController::updateAutopilotTargetXZ(float const x, float const z)
{
	if (!m_autopilotActive)
		return;
	m_autopilotTarget.x = x;
	m_autopilotTarget.z = z;
}

// ----------------------------------------------------------------------

void PlayerShipController::clearAutopilot()
{
	m_autopilotActive = false;
	m_autopilotTarget = Vector::zero;
	m_autopilotPhase = AP_NONE;
	m_autopilotDesiredY = 0.0f;
	m_autopilotHoldCruise = false;
	m_autopilotFighterMode = false;
	m_autopilotHasLandingY = false;

	if (m_shipDynamicsModel)
	{
		m_shipDynamicsModel->setVelocity(Vector::zero);
		m_shipDynamicsModel->makeStationary();
	}
}

// ----------------------------------------------------------------------

bool PlayerShipController::isAutopilotActive() const
{
	return m_autopilotActive;
}

// ----------------------------------------------------------------------

int PlayerShipController::getAutopilotPhase() const
{
	return m_autopilotPhase;
}

// ----------------------------------------------------------------------

bool PlayerShipController::fighterAutopilotBeginDescent()
{
	if (!m_autopilotActive || !m_autopilotFighterMode || m_autopilotPhase != AP_CRUISING)
		return false;

	TerrainObject const * const terrain = TerrainObject::getConstInstance();
	if (terrain)
	{
		float terrainAtDest = 0.0f;
		terrain->getHeightForceChunkCreation(Vector(m_autopilotTarget.x, 0.0f, m_autopilotTarget.z), terrainAtDest);
		m_autopilotDesiredY = terrainAtDest + 20.0f;
	}
	m_autopilotPhase = AP_DESCENDING;
	return true;
}

// ----------------------------------------------------------------------

void PlayerShipController::setFlightPath(std::vector<FlightPathKey> const & keys)
{
	m_flightPath = keys;
	m_flightPathElapsed = 0.0f;
	m_flightPathHoldTime = 0.0f;

	for (size_t i = 1; i < m_flightPath.size(); ++i)
	{
		FlightPathKey const & previous = m_flightPath[i - 1];
		FlightPathKey & key = m_flightPath[i];

		//-- Times must ascend; unwrap the angles so each segment turns the short way
		key.time = std::max(key.time, previous.time);
		while (key.yaw - previous.yaw > PI)
			key.yaw -= PI_TIMES_2;
		while (key.yaw - previous.yaw < -PI)
			key.yaw += PI_TIMES_2;
		while (key.roll - previous.roll > PI)
			key.roll -= PI_TIMES_2;
		while (key.roll - previous.roll < -PI)
			key.roll += PI_TIMES_2;
	}

	if (!m_flightPath.empty())
	{
		//-- The path replaces the autopilot
		m_autopilotActive = false;
		m_autopilotTarget = Vector::zero;
		m_autopilotPhase = AP_NONE;
		m_autopilotHoldCruise = false;
		m_autopilotFighterMode = false;
	}
}

// ----------------------------------------------------------------------

void PlayerShipController::clearFlightPath()
{
	if (m_flightPath.empty())
		return;

	m_flightPath.clear();
	m_flightPathElapsed = 0.0f;
	m_flightPathHoldTime = 0.0f;

	//-- Hand the ship back to the flight model where it is, at rest
	ShipObject const * const ship = getShipOwner();
	if (m_shipDynamicsModel)
	{
		if (ship)
			m_shipDynamicsModel->setTransform(ship->getTransform_o2p());
		m_shipDynamicsModel->setVelocity(Vector::zero);
		m_shipDynamicsModel->makeStationary();
	}
}

// ----------------------------------------------------------------------

bool PlayerShipController::isFlightPathActive() const
{
	return !m_flightPath.empty();
}

// ----------------------------------------------------------------------

float PlayerShipController::getFlightPathElapsed() const
{
	return m_flightPath.empty() ? -1.0f : m_flightPathElapsed;
}

// ----------------------------------------------------------------------

float PlayerShipController::getFlightPathDuration() const
{
	return m_flightPath.empty() ? -1.0f : m_flightPath.back().time;
}

// ----------------------------------------------------------------------

void PlayerShipController::alterFlightPath(ShipObject & owner, float const elapsedTime)
{
	float const duration = m_flightPath.back().time;
	bool const holding = m_flightPathElapsed >= duration;
	m_flightPathElapsed = std::min(m_flightPathElapsed + elapsedTime, duration);

	if (holding)
	{
		//-- Parked on the last key: settle every client on it, then stop streaming
		if (m_flightPathHoldTime >= s_flightPathSettleTime)
			return;
		m_flightPathHoldTime += elapsedTime;
	}

	//-- Find the segment holding the current time
	size_t segment = 0;
	while (segment + 2 < m_flightPath.size() && m_flightPath[segment + 1].time <= m_flightPathElapsed)
		++segment;

	Vector position = flightPathPosition(m_flightPath[segment]);
	Vector angles = flightPathAngles(m_flightPath[segment]);
	Vector velocity;
	Vector angleRates;

	if (m_flightPath.size() > 1)
	{
		FlightPathKey const & key0 = m_flightPath[segment];
		FlightPathKey const & key1 = m_flightPath[segment + 1];
		float const dt = key1.time - key0.time;
		if (dt > 0.0f)
		{
			float const s = clamp(0.0f, (m_flightPathElapsed - key0.time) / dt, 1.0f);
			hermite(key0.position, flightPathTangent(m_flightPath, segment, flightPathPosition), key1.position, flightPathTangent(m_flightPath, segment + 1, flightPathPosition), dt, s, position, velocity);
			hermite(flightPathAngles(key0), flightPathTangent(m_flightPath, segment, flightPathAngles), flightPathAngles(key1), flightPathTangent(m_flightPath, segment + 1, flightPathAngles), dt, s, angles, angleRates);
		}
		else
		{
			position = key1.position;
			angles = flightPathAngles(key1);
		}
	}

	if (holding)
	{
		velocity = Vector::zero;
		angleRates = Vector::zero;
	}

	Transform transform;
	transform.yaw_l(angles.x);
	transform.pitch_l(angles.y);
	transform.roll_l(angles.z);
	transform.setPosition_p(position);

	//-- Keep the model in step: clients predict from its velocity and rates between updates
	m_shipDynamicsModel->setTransform(transform);
	m_shipDynamicsModel->setVelocity(velocity);
	m_shipDynamicsModel->setYawRate(angleRates.x);
	m_shipDynamicsModel->setPitchRate(angleRates.y);
	m_shipDynamicsModel->setRollRate(angleRates.z);

	owner.setTransform_o2p(transform);
	queueShipUpdatesForObservers(owner);
}

// ======================================================================
