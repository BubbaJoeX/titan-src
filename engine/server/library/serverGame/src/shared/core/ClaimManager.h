// ======================================================================
//
// ClaimManager.h
//
// Open-world claim regions: runtime registry, taxation balance, bans,
// maintenance scheduling, and observation refresh hooks.
//
// ======================================================================

#ifndef INCLUDED_ClaimManager_H
#define INCLUDED_ClaimManager_H

#include "sharedFoundation/NetworkId.h"
#include "sharedFoundation/StationId.h"
#include "sharedMath/Vector.h"
#include "Singleton/Singleton2.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class Client;
class CreatureObject;
class ServerObject;

// ======================================================================

class ClaimManager : public Singleton2<ClaimManager>
{
public:
	struct ClaimRecord;

	ClaimManager();
	~ClaimManager();

	//-- Registration (circle footprint in world XZ around center)
	uint32 tryRegisterClaim(
		StationId ownerAccount,
		NetworkId const &ownerCharacter,
		NetworkId const &markerId,
		std::string const &sceneId,
		Vector const &center,
		float radiusMeters,
		NetworkId const &terminalId = NetworkId::cms_invalid);

	void unregisterClaim(uint32 claimId);

	/// Full server-side teardown: footprint sync, observation refresh, optional world object cleanup, registry removal.
	bool removeClaim(uint32 claimId, bool destroyWorldObjects);

	/// Removes every claim owned by the account (all characters on that station).
	int purgeClaimsForAccount(StationId ownerAccount, bool destroyWorldObjects);

	/// Removes every claim whose owner character matches.
	int purgeClaimsForCharacter(NetworkId const &ownerCharacter, bool destroyWorldObjects);

	uint32 findClaimIdByMarker(NetworkId const &markerId) const;
	void collectClaimIdsForAccount(StationId ownerAccount, std::vector<uint32> &out) const;
	void collectClaimIdsForCharacter(NetworkId const &ownerCharacter, std::vector<uint32> &out) const;

	uint32 findClaimIdAtPosition(std::string const &sceneId, Vector const &worldPosition) const;
	uint32 findClaimIdForObject(ServerObject const &obj) const;
	bool getClaimFootprint(uint32 claimId, std::string &sceneIdOut, Vector &centerOut, float &radiusOut) const;
	bool getClaimOwnerAccount(uint32 claimId, StationId &outAccount) const;
	NetworkId getClaimMarker(uint32 claimId) const;

	bool isCharacterBanned(uint32 claimId, NetworkId const &characterId) const;
	void addBan(uint32 claimId, NetworkId const &bannedCharacter);
	void removeBan(uint32 claimId, NetworkId const &bannedCharacter);

	bool isCharacterAllowed(uint32 claimId, NetworkId const &characterId) const;
	void addAllowed(uint32 claimId, NetworkId const &allowedCharacter);
	void removeAllowed(uint32 claimId, NetworkId const &allowedCharacter);

	bool hasManipulatePermission(CreatureObject const *actor, uint32 claimId) const;
	bool isClaimOwner(CreatureObject const *actor, ClaimRecord const &claim) const;
	bool canManipulateInClaim(CreatureObject const *actor, uint32 claimId) const;
	bool canManipulateInClaim(CreatureObject const *actor, uint32 claimId, Vector const &operationPos) const;
	bool canManipulateClaimObject(CreatureObject const *actor, ServerObject const &obj) const;
	bool validateManipulateWorldPosition(CreatureObject const *actor, ServerObject const &targetObject, Vector const &newWorldPos) const;

	int getTaxBalance(uint32 claimId, std::string const &resourceKey) const;
	void addTaxBalance(uint32 claimId, std::string const &resourceKey, int amount);
	bool withdrawTaxBalance(uint32 claimId, std::string const &resourceKey, int amount);

	bool payMaintenance(uint32 claimId, int creditsPaid);
	int getMaintenancePrepayCredits(uint32 claimId) const;
	unsigned long getNextMaintenanceDueGameSeconds(uint32 claimId) const;
	int getClaimStatus(uint32 claimId) const;
	void updateFrame();

	void bindObjectToClaim(uint32 claimId, NetworkId const &objectId);
	void unbindObjectFromClaim(NetworkId const &objectId);

	void getClaimContentObjects(uint32 claimId, std::vector<NetworkId> &out) const;

	//-- Visitor resource tax: returns units the visitor keeps after tax.
	int applyVisitorResourceTax(
		CreatureObject const *visitor,
		std::string const &sceneId,
		Vector const &worldPosition,
		std::string const &resourceKey,
		int grantAmount);

	//-- Observation: suppress baselines for claim contents when viewer is outside footprint.
	bool shouldSuppressClaimContentsObservation(Client const &client, ServerObject const &obj) const;

	//-- After player movement, observe/unobserve claim contents when crossing footprint boundary.
	void onCreatureMoved(Client *client, CreatureObject const *creature, Vector const &start_w, Vector const &end_w);

	//-- Transfer permission: returns false to block the transfer.
	bool allowContainerTransfer(ServerObject *transferer, ServerObject *destination, ServerObject &item) const;

	/// noTrade / nomove items may not be dropped into the open world (claim ground cell).
	bool isItemBlockedFromOpenWorldPlacement(ServerObject const &item) const;

	/// Allows depositing noTrade items into a claim container when the actor has decorate permission (not extraction to inventory).
	bool canDepositNoTradeIntoClaimContainer(CreatureObject const *actor, ServerObject const *destination, ServerObject const &item) const;

	//-- Furniture / manipulation permission for claim-bound objects.
	bool allowWorldManipulation(ServerObject const *actorCreature, ServerObject const &targetObject) const;

	bool tryEjectBannedCreature(CreatureObject &creature);

	void syncFootprintsToClient(Client *client) const;
	void syncManipulateStateToClient(Client *client, CreatureObject const *creature) const;

	struct ClaimRecord
	{
		uint32 id;
		std::string sceneId;
		Vector center;
		float radius;
		StationId ownerAccount;
		NetworkId ownerCharacter;
		NetworkId markerId;
		NetworkId terminalId;
		unsigned long nextMaintenanceDueGameSeconds;
		int maintenancePrepayCredits;
		int status; // 0 = active, 1 = suspended / repossessed
		std::set<NetworkId> banned;
		std::set<NetworkId> allowed;
		std::map<std::string, int> taxBalances;
		std::set<NetworkId> contentObjects;
	};

	typedef std::map<uint32, ClaimRecord> ClaimMap;
	ClaimMap m_claims;
	uint32 m_nextId;
	/// Claims grouped by scene for overlap checks and point queries (no spatial tree yet).
	std::map<std::string, std::vector<uint32> > m_claimsByScene;

	bool circlesOverlap(Vector const &aCenter, float aRadius, Vector const &bCenter, float bRadius) const;
	bool pointInsideClaimXZ(ClaimRecord const &c, Vector const &worldPos) const;
	int countActiveClaimsForAccount(StationId account) const;
	ClaimRecord *findRecord(uint32 id);
	ClaimRecord const *findRecord(uint32 id) const;
	void runRepossession(uint32 claimId);
	void broadcastFootprintSync(ClaimRecord const &rec, bool active) const;
	void sendManipulateStateToClient(Client *client, bool canManipulate) const;
	void refreshClaimObservationForAllClients(uint32 claimId, bool wantObserve) const;
	void clearClaimObjvarsOnObject(ServerObject &obj) const;
};

// ======================================================================

#endif
