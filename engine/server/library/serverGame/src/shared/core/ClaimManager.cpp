// ======================================================================
//
// ClaimManager.cpp
//
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/ClaimManager.h"

#include "serverGame/Client.h"
#include "serverGame/ConfigServerGame.h"
#include "serverGame/GameServer.h"
#include "serverGame/ContainerInterface.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/MessageToQueue.h"
#include "serverGame/ObserveTracker.h"
#include "serverGame/PlayerCreatureController.h"
#include "serverGame/PlayerObject.h"
#include "serverGame/ServerWorld.h"
#include "serverUtility/ServerClock.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedLog/Log.h"
#include "sharedNetworkMessages/ClaimFootprintSyncMessage.h"
#include "sharedNetworkMessages/ClaimManipulateStateMessage.h"
#include <algorithm>
#include <cmath>

namespace ClaimObjvars
{
static char const *const ms_claimId = "claim.id";
static char const *const ms_claimMarker = "claim.is_marker";
static char const *const ms_claimTerminal = "claim.is_terminal";
}

namespace
{
bool isMarkerOrTerminal(ServerObject const &obj)
{
	int v = 0;
	if (obj.getObjVars().getItem(ClaimObjvars::ms_claimMarker, v) && v != 0)
		return true;
	if (obj.getObjVars().getItem(ClaimObjvars::ms_claimTerminal, v) && v != 0)
		return true;
	return false;
}

bool tryGetClaimId(ServerObject const &obj, int &claimIdOut)
{
	return obj.getObjVars().getItem(ClaimObjvars::ms_claimId, claimIdOut) && claimIdOut > 0;
}

StationId getStationIdForCreature(ServerObject const *o)
{
	CreatureObject const *const c = o ? o->asCreatureObject() : nullptr;
	if (!c)
		return 0;
	PlayerObject const *const po = PlayerCreatureController::getPlayerObject(c);
	if (!po)
		return 0;
	return po->getStationId();
}

Vector getObjectWorldPosition(ServerObject const *o)
{
	if (!o)
		return Vector::zero;
	ServerObject const *top = o;
	for (ServerObject const *walk = o; walk; walk = safe_cast<ServerObject const *>(ContainerInterface::getContainedByObject(*walk)))
		top = walk;
	return top->getPosition_w();
}

void removeClaimIdFromScene(std::map<std::string, std::vector<uint32> > &byScene, std::string const &sceneId, uint32 claimId)
{
	if (sceneId.empty())
		return;
	std::map<std::string, std::vector<uint32> >::iterator s = byScene.find(sceneId);
	if (s == byScene.end())
		return;
	std::vector<uint32> &v = s->second;
	v.erase(std::remove(v.begin(), v.end(), claimId), v.end());
	if (v.empty())
		byScene.erase(s);
}
}

// ======================================================================

ClaimManager::ClaimManager() :
	Singleton2<ClaimManager>(),
	m_claims(),
	m_nextId(1)
{
	ExitChain::add(&remove, "ClaimManager::remove");
}

// ----------------------------------------------------------------------

ClaimManager::~ClaimManager()
{
}

// ----------------------------------------------------------------------

ClaimManager::ClaimRecord *ClaimManager::findRecord(uint32 id)
{
	ClaimMap::iterator i = m_claims.find(id);
	return i == m_claims.end() ? nullptr : &i->second;
}

// ----------------------------------------------------------------------

ClaimManager::ClaimRecord const *ClaimManager::findRecord(uint32 id) const
{
	ClaimMap::const_iterator i = m_claims.find(id);
	return i == m_claims.end() ? nullptr : &i->second;
}

// ----------------------------------------------------------------------

bool ClaimManager::circlesOverlap(Vector const &aCenter, float aRadius, Vector const &bCenter, float bRadius) const
{
	float const dx = aCenter.x - bCenter.x;
	float const dz = aCenter.z - bCenter.z;
	float const dist = std::sqrt(dx * dx + dz * dz);
	return dist < (aRadius + bRadius);
}

// ----------------------------------------------------------------------

bool ClaimManager::pointInsideClaimXZ(ClaimRecord const &c, Vector const &worldPos) const
{
	if (c.radius <= 0.f)
		return false;
	float const dx = worldPos.x - c.center.x;
	float const dz = worldPos.z - c.center.z;
	return (dx * dx + dz * dz) <= (c.radius * c.radius);
}

// ----------------------------------------------------------------------

int ClaimManager::countActiveClaimsForAccount(StationId account) const
{
	int n = 0;
	for (ClaimMap::const_iterator i = m_claims.begin(); i != m_claims.end(); ++i)
	{
		if (i->second.status == 0 && i->second.ownerAccount == account)
			++n;
	}
	return n;
}

// ----------------------------------------------------------------------

uint32 ClaimManager::tryRegisterClaim(
	StationId ownerAccount,
	NetworkId const &ownerCharacter,
	NetworkId const &markerId,
	std::string const &sceneId,
	Vector const &center,
	float radiusMeters,
	NetworkId const &terminalId)
{
	if (!ConfigServerGame::getClaimSystemEnabled())
		return 0;

	if (ownerAccount == 0 || !markerId.isValid() || sceneId.empty() || radiusMeters <= 0.f)
		return 0;

	int const cap = std::max(1, ConfigServerGame::getClaimMaxActivePerAccount());
	if (countActiveClaimsForAccount(ownerAccount) >= cap)
		return 0;

	std::map<std::string, std::vector<uint32> >::const_iterator sci = m_claimsByScene.find(sceneId);
	if (sci != m_claimsByScene.end())
	{
		for (size_t k = 0; k < sci->second.size(); ++k)
		{
			uint32 const otherId = sci->second[k];
			ClaimRecord const *const other = findRecord(otherId);
			if (!other || other->status != 0)
				continue;
			if (circlesOverlap(center, radiusMeters, other->center, other->radius))
				return 0;
		}
	}

	uint32 const id = m_nextId++;
	ClaimRecord rec;
	rec.id = id;
	rec.sceneId = sceneId;
	rec.center = center;
	rec.radius = radiusMeters;
	rec.ownerAccount = ownerAccount;
	rec.ownerCharacter = ownerCharacter;
	rec.markerId = markerId;
	rec.terminalId = terminalId;
	unsigned long const now = ServerClock::getInstance().getGameTimeSeconds();
	int const days = std::max(1, ConfigServerGame::getClaimMaintenanceIntervalDays());
	rec.nextMaintenanceDueGameSeconds = now + static_cast<unsigned long>(days) * 24UL * 60UL * 60UL;
	rec.maintenancePrepayCredits = 0;
	rec.status = 0;
	m_claims[id] = rec;
	m_claimsByScene[sceneId].push_back(id);
	broadcastFootprintSync(rec, true);
	return id;
}

// ----------------------------------------------------------------------

void ClaimManager::unregisterClaim(uint32 claimId)
{
	ClaimMap::iterator i = m_claims.find(claimId);
	if (i == m_claims.end())
		return;
	ClaimRecord const rec = i->second;
	std::string const sceneId = rec.sceneId;
	broadcastFootprintSync(rec, false);
	removeClaimIdFromScene(m_claimsByScene, sceneId, claimId);
	m_claims.erase(i);
}

// ----------------------------------------------------------------------

void ClaimManager::broadcastFootprintSync(ClaimRecord const &rec, bool const active) const
{
	if (!rec.markerId.isValid())
		return;
	ClaimFootprintSyncMessage const msg(rec.markerId, rec.radius, active);
	GameServer::getInstance().spamAllClients(msg, true);
}

// ----------------------------------------------------------------------

void ClaimManager::syncFootprintsToClient(Client *const client) const
{
	if (!client)
		return;
	std::string const & sceneId = ServerWorld::getSceneId();
	for (ClaimMap::const_iterator i = m_claims.begin(); i != m_claims.end(); ++i)
	{
		ClaimRecord const &rec = i->second;
		if (rec.status != 0)
			continue;
		if (rec.sceneId != sceneId)
			continue;
		if (!rec.markerId.isValid())
			continue;
		ClaimFootprintSyncMessage const msg(rec.markerId, rec.radius, true);
		client->send(msg, true);
	}
}

// ----------------------------------------------------------------------

void ClaimManager::sendManipulateStateToClient(Client *const client, bool const canManipulate) const
{
	if (!client || !ConfigServerGame::getClaimSystemEnabled())
		return;

	ClaimManipulateStateMessage const msg(canManipulate);
	client->send(msg, true);
}

// ----------------------------------------------------------------------

void ClaimManager::syncManipulateStateToClient(Client *const client, CreatureObject const *creature) const
{
	if (!client || !creature || !ConfigServerGame::getClaimSystemEnabled())
	{
		sendManipulateStateToClient(client, false);
		return;
	}

	if (client->isGod())
	{
		sendManipulateStateToClient(client, true);
		return;
	}

	Vector const pos = getObjectWorldPosition(creature);
	uint32 const claimId = findClaimIdAtPosition(creature->getSceneId(), pos);
	bool const canManipulate = claimId != 0 && canManipulateInClaim(creature, claimId);
	sendManipulateStateToClient(client, canManipulate);
}

// ----------------------------------------------------------------------

uint32 ClaimManager::findClaimIdAtPosition(std::string const &sceneId, Vector const &worldPosition) const
{
	std::map<std::string, std::vector<uint32> >::const_iterator sci = m_claimsByScene.find(sceneId);
	if (sci == m_claimsByScene.end())
		return 0;
	for (size_t k = 0; k < sci->second.size(); ++k)
	{
		uint32 const id = sci->second[k];
		ClaimRecord const *const r = findRecord(id);
		if (!r || r->status != 0)
			continue;
		if (pointInsideClaimXZ(*r, worldPosition))
			return id;
	}
	return 0;
}

// ----------------------------------------------------------------------

bool ClaimManager::getClaimFootprint(uint32 claimId, std::string &sceneIdOut, Vector &centerOut, float &radiusOut) const
{
	ClaimRecord const *const r = findRecord(claimId);
	if (!r)
		return false;
	sceneIdOut = r->sceneId;
	centerOut = r->center;
	radiusOut = r->radius;
	return true;
}

// ----------------------------------------------------------------------

bool ClaimManager::getClaimOwnerAccount(uint32 claimId, StationId &outAccount) const
{
	ClaimRecord const *const r = findRecord(claimId);
	if (!r)
		return false;
	outAccount = r->ownerAccount;
	return true;
}

// ----------------------------------------------------------------------

NetworkId ClaimManager::getClaimMarker(uint32 claimId) const
{
	ClaimRecord const *const r = findRecord(claimId);
	return r ? r->markerId : NetworkId::cms_invalid;
}

// ----------------------------------------------------------------------

bool ClaimManager::isCharacterBanned(uint32 claimId, NetworkId const &characterId) const
{
	ClaimRecord const *const r = findRecord(claimId);
	if (!r)
		return false;
	return r->banned.find(characterId) != r->banned.end();
}

// ----------------------------------------------------------------------

void ClaimManager::addBan(uint32 claimId, NetworkId const &bannedCharacter)
{
	ClaimRecord *const r = findRecord(claimId);
	if (!r)
		return;
	r->banned.insert(bannedCharacter);
}

// ----------------------------------------------------------------------

void ClaimManager::removeBan(uint32 claimId, NetworkId const &bannedCharacter)
{
	ClaimRecord *const r = findRecord(claimId);
	if (!r)
		return;
	r->banned.erase(bannedCharacter);
}

// ----------------------------------------------------------------------

bool ClaimManager::isCharacterAllowed(uint32 claimId, NetworkId const &characterId) const
{
	ClaimRecord const *const r = findRecord(claimId);
	if (!r)
		return false;
	return r->allowed.find(characterId) != r->allowed.end();
}

// ----------------------------------------------------------------------

void ClaimManager::addAllowed(uint32 claimId, NetworkId const &allowedCharacter)
{
	ClaimRecord *const r = findRecord(claimId);
	if (!r)
		return;
	r->allowed.insert(allowedCharacter);
	r->banned.erase(allowedCharacter);
}

// ----------------------------------------------------------------------

void ClaimManager::removeAllowed(uint32 claimId, NetworkId const &allowedCharacter)
{
	ClaimRecord *const r = findRecord(claimId);
	if (!r)
		return;
	r->allowed.erase(allowedCharacter);
}

// ----------------------------------------------------------------------

bool ClaimManager::hasManipulatePermission(CreatureObject const *actor, uint32 const claimId) const
{
	if (!actor || claimId == 0)
		return false;

	ClaimRecord const *const r = findRecord(claimId);
	if (!r || r->status != 0)
		return false;

	if (isCharacterBanned(claimId, actor->getNetworkId()))
		return false;

	StationId const actorAccount = getStationIdForCreature(actor);
	if (actorAccount != 0 && actorAccount == r->ownerAccount)
		return true;

	if (actor->getNetworkId() == r->ownerCharacter)
		return true;

	return isCharacterAllowed(claimId, actor->getNetworkId());
}

// ----------------------------------------------------------------------

bool ClaimManager::canManipulateInClaim(CreatureObject const *actor, uint32 const claimId) const
{
	if (!hasManipulatePermission(actor, claimId))
		return false;

	ClaimRecord const *const r = findRecord(claimId);
	if (!r)
		return false;

	Vector const actorPos = getObjectWorldPosition(actor);
	return pointInsideClaimXZ(*r, actorPos);
}

// ----------------------------------------------------------------------

bool ClaimManager::validateManipulateWorldPosition(CreatureObject const *actor, ServerObject const &targetObject, Vector const &newWorldPos) const
{
	int claimIdInt = 0;
	if (!tryGetClaimId(targetObject, claimIdInt))
	{
		std::string const scene = targetObject.getSceneId();
		claimIdInt = static_cast<int>(findClaimIdAtPosition(scene, getObjectWorldPosition(targetObject)));
	}

	if (claimIdInt <= 0)
		return true;

	uint32 const claimId = static_cast<uint32>(claimIdInt);
	if (!canManipulateInClaim(actor, claimId))
		return false;

	ClaimRecord const *const r = findRecord(claimId);
	if (!r)
		return true;

	return pointInsideClaimXZ(*r, newWorldPos);
}

// ----------------------------------------------------------------------

int ClaimManager::getTaxBalance(uint32 claimId, std::string const &resourceKey) const
{
	ClaimRecord const *const r = findRecord(claimId);
	if (!r)
		return 0;
	std::map<std::string, int>::const_iterator i = r->taxBalances.find(resourceKey);
	return i == r->taxBalances.end() ? 0 : i->second;
}

// ----------------------------------------------------------------------

void ClaimManager::addTaxBalance(uint32 claimId, std::string const &resourceKey, int amount)
{
	if (amount <= 0)
		return;
	ClaimRecord *const r = findRecord(claimId);
	if (!r)
		return;
	r->taxBalances[resourceKey] += amount;
}

// ----------------------------------------------------------------------

bool ClaimManager::withdrawTaxBalance(uint32 claimId, std::string const &resourceKey, int amount)
{
	if (amount <= 0)
		return false;
	ClaimRecord *const r = findRecord(claimId);
	if (!r)
		return false;
	std::map<std::string, int>::iterator i = r->taxBalances.find(resourceKey);
	if (i == r->taxBalances.end() || i->second < amount)
		return false;
	i->second -= amount;
	if (i->second <= 0)
		r->taxBalances.erase(i);
	return true;
}

// ----------------------------------------------------------------------

bool ClaimManager::payMaintenance(uint32 claimId, int creditsPaid)
{
	if (creditsPaid <= 0)
		return false;
	ClaimRecord *const r = findRecord(claimId);
	if (!r)
		return false;
	r->maintenancePrepayCredits += creditsPaid;
	int const fee = std::max(1, ConfigServerGame::getClaimMaintenanceFeeCredits());
	while (r->maintenancePrepayCredits >= fee)
	{
		r->maintenancePrepayCredits -= fee;
		int const days = std::max(1, ConfigServerGame::getClaimMaintenanceIntervalDays());
		r->nextMaintenanceDueGameSeconds += static_cast<unsigned long>(days) * 24UL * 60UL * 60UL;
	}
	return true;
}

// ----------------------------------------------------------------------

void ClaimManager::bindObjectToClaim(uint32 claimId, NetworkId const &objectId)
{
	ClaimRecord *const r = findRecord(claimId);
	if (!r)
		return;
	r->contentObjects.insert(objectId);
}

// ----------------------------------------------------------------------

void ClaimManager::unbindObjectFromClaim(NetworkId const &objectId)
{
	for (ClaimMap::iterator i = m_claims.begin(); i != m_claims.end(); ++i)
		i->second.contentObjects.erase(objectId);
}

// ----------------------------------------------------------------------

void ClaimManager::getClaimContentObjects(uint32 claimId, std::vector<NetworkId> &out) const
{
	out.clear();
	ClaimRecord const *const r = findRecord(claimId);
	if (!r)
		return;
	for (std::set<NetworkId>::const_iterator i = r->contentObjects.begin(); i != r->contentObjects.end(); ++i)
		out.push_back(*i);
}

// ----------------------------------------------------------------------

int ClaimManager::applyVisitorResourceTax(
	CreatureObject const *visitor,
	std::string const &sceneId,
	Vector const &worldPosition,
	std::string const &resourceKey,
	int grantAmount)
{
	if (!ConfigServerGame::getClaimSystemEnabled() || grantAmount <= 0)
		return grantAmount;

	uint32 const claimId = findClaimIdAtPosition(sceneId, worldPosition);
	if (claimId == 0)
		return grantAmount;

	ClaimRecord const *const r = findRecord(claimId);
	if (!r || r->status != 0)
		return grantAmount;

	StationId const visitorAccount = getStationIdForCreature(visitor);
	if (visitorAccount == 0 || visitorAccount == r->ownerAccount)
		return grantAmount;

	float taxRate = ConfigServerGame::getClaimVisitorResourceTaxRate();
	if (taxRate < 0.f)
		taxRate = 0.f;
	if (taxRate > 0.95f)
		taxRate = 0.95f;
	int const tax = static_cast<int>(std::floor(static_cast<float>(grantAmount) * taxRate));
	if (tax <= 0)
		return grantAmount;
	int const kept = grantAmount - tax;
	addTaxBalance(claimId, resourceKey.empty() ? "generic" : resourceKey, tax);
	return std::max(1, kept);
}

// ----------------------------------------------------------------------

bool ClaimManager::shouldSuppressClaimContentsObservation(Client const &client, ServerObject const &obj) const
{
	if (!ConfigServerGame::getClaimSystemEnabled())
		return false;

	int claimIdInt = 0;
	if (!tryGetClaimId(obj, claimIdInt))
		return false;

	if (isMarkerOrTerminal(obj))
		return false;

	CreatureObject const *const creature = client.getCharacterObject() ? client.getCharacterObject()->asCreatureObject() : nullptr;
	if (!creature)
		return false;

	if (client.isGod())
		return false;

	ClaimRecord const *const r = findRecord(static_cast<uint32>(claimIdInt));
	if (!r || r->status != 0)
		return false;

	if (obj.getSceneId() != r->sceneId)
		return false;

	Vector const p = getObjectWorldPosition(creature);
	return !pointInsideClaimXZ(*r, p);
}

// ----------------------------------------------------------------------

void ClaimManager::onCreatureMoved(Client *client, CreatureObject const *creature, Vector const &start_w, Vector const &end_w)
{
	if (!ConfigServerGame::getClaimSystemEnabled() || !client || !creature)
		return;

	std::string const scene = creature->getSceneId();

	uint32 const startClaim = findClaimIdAtPosition(scene, start_w);
	uint32 const endClaim = findClaimIdAtPosition(scene, end_w);

	bool const startCanManipulate = startClaim != 0 && canManipulateInClaim(creature, startClaim);
	bool const endCanManipulate = endClaim != 0 && canManipulateInClaim(creature, endClaim);

	if (startClaim == endClaim)
	{
		if (endClaim != 0)
			IGNORE_RETURN(tryEjectBannedCreature(const_cast<CreatureObject &>(*creature)));
		if (startCanManipulate != endCanManipulate)
			sendManipulateStateToClient(client, endCanManipulate);
		return;
	}

	static std::vector<NetworkId> tmp;
	if (startClaim != 0)
	{
		getClaimContentObjects(startClaim, tmp);
		ObserveTracker::claimRefreshObservation(*client, tmp, false);
	}
	if (endClaim != 0)
	{
		getClaimContentObjects(endClaim, tmp);
		ObserveTracker::claimRefreshObservation(*client, tmp, true);
	}

	sendManipulateStateToClient(client, endCanManipulate);

	IGNORE_RETURN(tryEjectBannedCreature(const_cast<CreatureObject &>(*creature)));
}

// ----------------------------------------------------------------------

bool ClaimManager::allowContainerTransfer(ServerObject *transferer, ServerObject *destination, ServerObject &item) const
{
	if (!ConfigServerGame::getClaimSystemEnabled())
		return true;

	if (!transferer)
		return true;

	CreatureObject *const actor = transferer->asCreatureObject();
	if (!actor)
		return true;

	if (actor->getClient() && actor->getClient()->isGod())
		return true;

	StationId const actorAccount = getStationIdForCreature(actor);
	if (actorAccount == 0)
		return true;

	Vector const itemPos = getObjectWorldPosition(&item);
	Vector const destSamplePos = destination ? getObjectWorldPosition(destination) : itemPos;

	std::string const scene = item.getSceneId();

	uint32 const itemClaim = findClaimIdAtPosition(scene, itemPos);
	uint32 const destClaim = findClaimIdAtPosition(scene, destSamplePos);

	if (itemClaim == 0 && destClaim == 0)
		return true;

	auto canAccessClaim = [&](uint32 const claimId) -> bool
	{
		if (claimId == 0)
			return true;
		return canManipulateInClaim(actor, claimId);
	};

	if (!canAccessClaim(itemClaim))
		return false;

	if (!canAccessClaim(destClaim))
		return false;

	// Deny transfers that would place a claim-bound item outside the footprint.
	if (itemClaim != 0)
	{
		ClaimRecord const *const r = findRecord(itemClaim);
		if (r && r->status == 0 && !pointInsideClaimXZ(*r, destSamplePos))
			return false;
	}

	UNREF(actorAccount);

	return true;
}

// ----------------------------------------------------------------------

bool ClaimManager::allowWorldManipulation(ServerObject const *actorCreature, ServerObject const &targetObject) const
{
	if (!ConfigServerGame::getClaimSystemEnabled())
		return true;

	CreatureObject const *const actor = actorCreature ? actorCreature->asCreatureObject() : nullptr;
	if (!actor)
		return true;

	if (actor->getClient() && actor->getClient()->isGod())
		return true;

	StationId const actorAccount = getStationIdForCreature(actor);
	if (actorAccount == 0)
		return true;

	int claimIdInt = 0;
	if (!tryGetClaimId(targetObject, claimIdInt))
	{
		claimIdInt = static_cast<int>(findClaimIdAtPosition(targetObject.getSceneId(), getObjectWorldPosition(targetObject)));
		if (claimIdInt == 0)
			return true;
	}

	return canManipulateInClaim(actor, static_cast<uint32>(claimIdInt));
}

// ----------------------------------------------------------------------

bool ClaimManager::tryEjectBannedCreature(CreatureObject &creature)
{
	if (!ConfigServerGame::getClaimSystemEnabled())
		return false;

	Client *const client = creature.getClient();
	if (!client)
		return false;

	PlayerObject const *const po = PlayerCreatureController::getPlayerObject(&creature);
	if (!po)
		return false;

	Vector const pos = getObjectWorldPosition(&creature);
	std::string const scene = creature.getSceneId();

	for (ClaimMap::iterator i = m_claims.begin(); i != m_claims.end(); ++i)
	{
		ClaimRecord &rec = i->second;
		if (rec.status != 0 || rec.sceneId != scene)
			continue;
		if (!pointInsideClaimXZ(rec, pos))
			continue;
		if (!isCharacterBanned(rec.id, creature.getNetworkId()))
			continue;

		Vector dir(pos.x - rec.center.x, 0.f, pos.z - rec.center.z);
		float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
		if (len < 0.001f)
			dir.set(1.f, 0.f, 0.f);
		else
		{
			dir.x /= len;
			dir.z /= len;
		}
		float const push = rec.radius + 2.f;
		Vector const dest(rec.center.x + dir.x * push, pos.y, rec.center.z + dir.z * push);

		creature.teleportObject(dest, NetworkId::cms_invalid, "", Vector(), "");
		if (client)
		{
			static std::vector<NetworkId> tmp;
			getClaimContentObjects(rec.id, tmp);
			ObserveTracker::claimRefreshObservation(*client, tmp, false);
		}
		return true;
	}
	return false;
}

// ----------------------------------------------------------------------

void ClaimManager::runRepossession(uint32 claimId)
{
	ClaimRecord *const r = findRecord(claimId);
	if (!r)
		return;

	broadcastFootprintSync(*r, false);

	r->status = 1;

	if (r->markerId.isValid())
		MessageToQueue::getInstance().sendMessageToC(r->markerId, "handleClaimRepossession", "", 1, false);

	for (std::set<NetworkId>::const_iterator i = r->contentObjects.begin(); i != r->contentObjects.end(); ++i)
	{
		ServerObject *const o = ServerWorld::findObjectByNetworkId(*i);
		if (o)
		{
			o->removeObjVarItem(ClaimObjvars::ms_claimId);
			o->removeObjVarItem(ClaimObjvars::ms_claimMarker);
			o->removeObjVarItem(ClaimObjvars::ms_claimTerminal);
		}
	}
	std::string const sceneCopy = r->sceneId;
	uint32 const idCopy = r->id;
	r->contentObjects.clear();
	removeClaimIdFromScene(m_claimsByScene, sceneCopy, idCopy);
	m_claims.erase(idCopy);
}

// ----------------------------------------------------------------------

void ClaimManager::updateFrame()
{
	if (!ConfigServerGame::getClaimSystemEnabled())
		return;

	unsigned long const now = ServerClock::getInstance().getGameTimeSeconds();

	std::vector<uint32> repossessList;

	for (ClaimMap::iterator i = m_claims.begin(); i != m_claims.end(); ++i)
	{
		ClaimRecord &rec = i->second;
		if (rec.status != 0)
			continue;

		if (now < rec.nextMaintenanceDueGameSeconds)
			continue;

		int const fee = std::max(1, ConfigServerGame::getClaimMaintenanceFeeCredits());
		if (rec.maintenancePrepayCredits >= fee)
		{
			rec.maintenancePrepayCredits -= fee;
			int const days = std::max(1, ConfigServerGame::getClaimMaintenanceIntervalDays());
			rec.nextMaintenanceDueGameSeconds = now + static_cast<unsigned long>(days) * 24UL * 60UL * 60UL;
			continue;
		}

		LOG("ClaimManager", ("Repossessing claim %u for failed maintenance.", rec.id));
		repossessList.push_back(rec.id);
	}

	for (size_t j = 0; j < repossessList.size(); ++j)
		runRepossession(repossessList[j]);
}
