// ======================================================================
//
// PlayerShipController.h
// asommers
//
// copyright 2004, sony online entertainment
//
// ======================================================================

#ifndef INCLUDED_PlayerShipController_H
#define INCLUDED_PlayerShipController_H

// ======================================================================

#include "serverGame/ShipController.h"
#include "serverUtility/RecentMaxSyncedValue.h"
#include "sharedFoundation/Timer.h"
#include "sharedMath/Vector.h"

#include <vector>

// ======================================================================

class ShipUpdateTransformMessage;

// ======================================================================

class PlayerShipController : public ShipController
{
public:

	static void install();
	/** Titan: a vertical cylinder no pilot may fly into (atmospheric flight; gods and server-flown ships exempt), by owner. */
	static void setNoFlyZone(NetworkId const & owner, float x, float z, float radius, float ceilingY);
	static void clearNoFlyZone(NetworkId const & owner);

public:

	explicit PlayerShipController(ShipObject * const owner);
	virtual ~PlayerShipController();

	virtual void endBaselines();
	virtual void setAuthoritative(bool newAuthoritative);
	virtual void onClientReady();
	virtual void onClientLost();
	virtual void teleport(Transform const &goal, ServerObject *goalObj);

	virtual PlayerShipController * asPlayerShipController();
	virtual PlayerShipController const * asPlayerShipController() const;

	void receiveTransform(ShipUpdateTransformMessage const & shipUpdateTransformMessage);
	void onShipAccelerationRateChanged(float value);
	void onShipDecelerationRateChanged(float value);
	void onShipYprMaximumChanged(float value);
	void onShipSpeedMaximumChanged(float value);

	virtual bool shouldCheckForEnemies() const;
	void updateGunnerWeaponIndex(NetworkId const & player, int const gunnerWeaponIndex);
	void setWeaponIndexPlayerControlled(int const weaponIndex, bool const playerControlled);

	virtual void addAiTargetingMe(NetworkId const & unit);
	bool isTeleporting() const;

	void setAutopilotTarget(Vector const & target, float takeoffAltitude, float landingAltitude, bool holdCruise = false);
	void setAutopilotTarget(Vector const & target, float takeoffAltitude, float landingAltitude, bool holdCruise, bool fighterMode);
	/** While autopilot is active, move horizontal goal without resetting climb/cruise phase (atmospheric follow). */
	void updateAutopilotTargetXZ(float x, float z);
	/** Titan: descend to this world height at the target instead of terrain + landing altitude (call after setAutopilotTarget). */
	void setAutopilotLandingY(float y);
	void clearAutopilot();
	/** Non-POB fighter autopilot: begin elevator descent to 20m AGL at current horizontal target (script calls after stationary ticks). */
	bool fighterAutopilotBeginDescent();
	bool isAutopilotActive() const;
	int  getAutopilotPhase() const;

	enum AutopilotPhase
	{
		AP_NONE       = 0,
		AP_ASCENDING  = 1,
		AP_CRUISING   = 2,
		AP_DESCENDING = 3,
		AP_ARRIVED    = 4
	};

	/** One key of a scripted flight path: parent-space position and heading/pitch/roll (radians, pitch > 0 = nose down, as Transform::pitch_l). */
	struct FlightPathKey
	{
		float  time;
		Vector position;
		float  yaw;
		float  pitch;
		float  roll;
	};

	/**
	 * Scripted flight path (Titan vessels: shuttle approach, touchdown and climb-out). The ship follows the keys
	 * exactly (Hermite through the keys, at rest at both ends) and then holds the last key until cleared. While a
	 * path is set the atmospheric terrain floor is not applied, so a path may end on the ground (landed on its gear).
	 */
	void  setFlightPath(std::vector<FlightPathKey> const & keys);
	void  clearFlightPath();
	bool  isFlightPathActive() const;
	float getFlightPathElapsed() const;
	float getFlightPathDuration() const;

protected:
	virtual void handleMessage(int message, float value, const MessageQueue::Data* data, uint32 flags);

	virtual float realAlter(float elapsedTime);
	virtual void handleNetUpdateTransform(MessageQueueDataTransform const & message);
	virtual void handleNetUpdateTransformWithParent(MessageQueueDataTransformWithParent const & message);

private:

	PlayerShipController();
	PlayerShipController(PlayerShipController const &);
	PlayerShipController & operator=(PlayerShipController const &);

	void preventMovementUpdates();
	void handleTeleportAck(int sequenceId);
	void resyncMovementUpdates();

	virtual void experiencedCollision();

	uint32 getCurSyncStamp() const;
	bool checkValidMove(Transform const &transform, Vector const &velocity, float speed, uint32 syncStamp);
	void logMoveFail(char const *reasonFmt, ...) const;

	void alterFlightPath(ShipObject & owner, float elapsedTime);

private:

	typedef std::map<NetworkId, int> GunnerWeaponIndexList;

	uint32 m_clientToServerLastSyncStamp;
	std::set<int> m_teleportIds;
	RecentMaxSyncedValue m_shipAccelRate;
	RecentMaxSyncedValue m_shipDecelRate;
	RecentMaxSyncedValue m_shipYprMaximum;
	RecentMaxSyncedValue m_shipSpeedMaximum;
	Transform m_lastVerifiedTransform;
	float m_lastVerifiedSpeed;
	uint32 m_lastVerifiedSyncStamp;
	GunnerWeaponIndexList * const m_gunnerWeaponIndexList;
	Timer m_targetedByAiTimer;

	bool m_autopilotActive;
	Vector m_autopilotTarget;
	float m_autopilotTakeoffAltitude;
	float m_autopilotLandingAltitude;
	int   m_autopilotPhase;
	float m_autopilotDesiredY;
	bool  m_autopilotHoldCruise;
	bool  m_autopilotFighterMode;
	bool  m_autopilotHasLandingY;
	float m_autopilotLandingY;
	bool  m_serverDrivenDocking;     // Titan: C_docking set by us while the server flies the ship
	unsigned long m_noFlyWarnedAtMs;     // Titan: last restricted-airspace warning to the pilot

	std::vector<FlightPathKey> m_flightPath;
	float m_flightPathElapsed;
	float m_flightPathHoldTime;
};

// ======================================================================

#endif
