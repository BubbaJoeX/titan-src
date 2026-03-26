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

// ======================================================================

class ShipUpdateTransformMessage;

// ======================================================================

class PlayerShipController : public ShipController
{
public:

	static void install();

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
};

// ======================================================================

#endif
