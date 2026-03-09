// ======================================================================
//
// CityTerrainService.cpp
// copyright 2026 Titan
//
// Server-side handler for city terrain painting messages
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/CityTerrainService.h"

#include "serverGame/CityInfo.h"
#include "serverGame/CityInterface.h"
#include "serverGame/Client.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/GameServer.h"
#include "serverGame/ServerObject.h"
#include "serverGame/ServerWorld.h"
#include "sharedFoundation/DynamicVariableList.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedLog/Log.h"
#include "sharedNetworkMessages/CityTerrainMessages.h"

#include <ctime>
#include <cstdlib>
#include <cmath>
#include <sstream>

// ======================================================================

namespace CityTerrainServiceNamespace
{
	bool s_installed = false;

	int const MAX_RADIUS_BY_RANK[] = {0, 10, 15, 20, 30, 40, 50};
	int const NUM_RANKS = sizeof(MAX_RADIUS_BY_RANK) / sizeof(MAX_RADIUS_BY_RANK[0]);

	// Objvar prefixes for terrain region persistence
	std::string const TERRAIN_OBJVAR_ROOT = "city.terrain.regions";
	std::string const TERRAIN_OBJVAR_COUNT = "city.terrain.region_count";
}

using namespace CityTerrainServiceNamespace;

// ======================================================================

void CityTerrainService::install()
{
	DEBUG_FATAL(s_installed, ("CityTerrainService already installed"));
	s_installed = true;
	ExitChain::add(CityTerrainService::remove, "CityTerrainService::remove");
}

// ----------------------------------------------------------------------

void CityTerrainService::remove()
{
	DEBUG_FATAL(!s_installed, ("CityTerrainService not installed"));
	s_installed = false;
}

// ----------------------------------------------------------------------

void CityTerrainService::handlePaintRequest(Client const & client, CityTerrainPaintRequestMessage const & msg)
{
	int32 const cityId = msg.getCityId();
	int32 const modType = msg.getModificationType();
	std::string const & shader = msg.getShaderTemplate();
	float const centerX = msg.getCenterX();
	float const centerZ = msg.getCenterZ();
	float const radius = msg.getRadius();
	float const endX = msg.getEndX();
	float const endZ = msg.getEndZ();
	float const width = msg.getWidth();
	float const height = msg.getHeight();
	float const blendDist = msg.getBlendDistance();

	LOG("CityTerrain", ("handlePaintRequest: cityId=%d modType=%d shader=%s center=(%.1f,%.1f) radius=%.1f",
					   cityId, modType, shader.c_str(), centerX, centerZ, radius));

	// Validate mayor permission
	if (!validateMayorPermission(client, cityId))
	{
		sendResponse(client, false, "", "You do not have permission to modify this city's terrain.");
		return;
	}

	// Validate city bounds
	if (!validateCityBounds(cityId, centerX, centerZ, radius))
	{
		sendResponse(client, false, "", "Terrain modification extends outside city boundaries.");
		return;
	}

	// Check radius limits based on city rank (use citizen count as proxy for rank)
	if (!CityInterface::cityExists(cityId))
	{
		sendResponse(client, false, "", "City does not exist.");
		return;
	}

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
	int citizenCount = cityInfo.getCitizenCount();
	int cityRank = 1;
	if (citizenCount >= 50) cityRank = 5;
	else if (citizenCount >= 35) cityRank = 4;
	else if (citizenCount >= 20) cityRank = 3;
	else if (citizenCount >= 10) cityRank = 2;

	if (cityRank < 1 || cityRank >= NUM_RANKS)
	{
		cityRank = 1;
	}

	int maxRadius = MAX_RADIUS_BY_RANK[cityRank];
	if (modType == CityTerrainModificationType::MT_SHADER_CIRCLE && radius > maxRadius)
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "Maximum radius for your city rank is %d meters.", maxRadius);
		sendResponse(client, false, "", buf);
		return;
	}

	// Check for flatten permission (rank 3+)
	if (modType == CityTerrainModificationType::MT_FLATTEN)
	{
		if (cityRank < 3)
		{
			sendResponse(client, false, "", "City must be rank 3 or higher to flatten terrain.");
			return;
		}
		// Allow flatten radius up to city radius
		int cityRadius = cityInfo.getRadius();
		if (radius > static_cast<float>(cityRadius))
		{
			char buf[128];
			snprintf(buf, sizeof(buf), "Flatten radius cannot exceed city radius (%d meters).", cityRadius);
			sendResponse(client, false, "", buf);
			return;
		}
	}

	// Generate region ID
	std::string regionId = generateRegionId();

	// Save to city hall for persistence
	saveRegionToCityHall(cityId, regionId, modType, shader, centerX, centerZ, radius, endX, endZ, width, height, blendDist);

	// Broadcast to all clients in range
	broadcastToCity(cityId, modType, regionId, shader, centerX, centerZ, radius, endX, endZ, width, height, blendDist);

	// Send success response
	sendResponse(client, true, regionId, "");

	LOG("CityTerrain", ("Terrain painted successfully: regionId=%s", regionId.c_str()));
}

// ----------------------------------------------------------------------

void CityTerrainService::handleRemoveRequest(Client const & client, CityTerrainRemoveRequestMessage const & msg)
{
	int32 const cityId = msg.getCityId();
	std::string const & regionId = msg.getRegionId();

	LOG("CityTerrain", ("handleRemoveRequest: cityId=%d regionId=%s", cityId, regionId.c_str()));

	if (!validateMayorPermission(client, cityId))
	{
		return;
	}

	// Remove from persistence
	removeRegionFromCityHall(cityId, regionId);

	// Broadcast removal to all clients
	broadcastToCity(cityId, CityTerrainModificationType::MT_REMOVE, regionId, "", 0, 0, 0, 0, 0, 0, 0, 0);
}

// ----------------------------------------------------------------------

void CityTerrainService::handleSyncRequest(Client const & client, CityTerrainSyncRequestMessage const & msg)
{
	int32 const cityId = msg.getCityId();

	LOG("CityTerrain", ("handleSyncRequest: cityId=%d", cityId));

	// Send all stored terrain regions to the client
	sendTerrainSyncToClient(client, cityId);
}

// ----------------------------------------------------------------------

bool CityTerrainService::validateMayorPermission(Client const & client, int32 cityId)
{
	ServerObject * const playerObject = client.getCharacterObject();
	if (!playerObject)
		return false;

	CreatureObject * const playerCreature = playerObject->asCreatureObject();
	if (!playerCreature)
		return false;

	// Check if player is god mode
	if (client.isGod())
		return true;

	// Check if city exists
	if (!CityInterface::cityExists(cityId))
		return false;

	// Check if player is mayor
	NetworkId const & playerId = playerCreature->getNetworkId();
	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);

	return cityInfo.getLeaderId() == playerId;
}

// ----------------------------------------------------------------------

bool CityTerrainService::validateCityBounds(int32 cityId, float x, float z, float radius)
{
	if (!CityInterface::cityExists(cityId))
		return false;

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);

	float cityCenterX = static_cast<float>(cityInfo.getX());
	float cityCenterZ = static_cast<float>(cityInfo.getZ());
	int cityRadius = cityInfo.getRadius();

	float dx = x - cityCenterX;
	float dz = z - cityCenterZ;
	float dist = std::sqrt(dx * dx + dz * dz);

	return (dist + radius) <= static_cast<float>(cityRadius);
}

// ----------------------------------------------------------------------

std::string CityTerrainService::generateRegionId()
{
	char buf[64];
	snprintf(buf, sizeof(buf), "R%ld_%d", static_cast<long>(time(0)), rand() % 10000);
	return std::string(buf);
}

// ----------------------------------------------------------------------

void CityTerrainService::broadcastToCity(int32 cityId, int32 modType, std::string const & regionId,
										  std::string const & shader, float centerX, float centerZ,
										  float radius, float endX, float endZ, float width,
										  float height, float blendDist)
{
	CityTerrainModifyMessage const msg(cityId, modType, regionId, shader, centerX, centerZ, radius, endX, endZ, width, height, blendDist);

	LOG("CityTerrain", ("broadcastToCity: cityId=%d modType=%d regionId=%s shader=%s",
		cityId, modType, regionId.c_str(), shader.c_str()));

	// Check if city exists
	if (!CityInterface::cityExists(cityId))
	{
		LOG("CityTerrain", ("broadcastToCity: city %d does not exist", cityId));
		return;
	}

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);

	float cityCenterX = static_cast<float>(cityInfo.getX());
	float cityCenterZ = static_cast<float>(cityInfo.getZ());
	int cityRadius = cityInfo.getRadius();

	Vector cityCenter(cityCenterX, 0.0f, cityCenterZ);

	// Find all objects in range and send to player clients
	std::vector<ServerObject *> objectsInRange;
	ServerWorld::findObjectsInRange(cityCenter, static_cast<float>(cityRadius + 100), objectsInRange);

	int sentCount = 0;
	for (std::vector<ServerObject *>::const_iterator it = objectsInRange.begin(); it != objectsInRange.end(); ++it)
	{
		if (*it == 0)
			continue;

		CreatureObject * const creature = (*it)->asCreatureObject();
		if (creature && creature->isPlayerControlled() && creature->getClient())
		{
			creature->getClient()->send(msg, true);
			sentCount++;
		}
	}

	LOG("CityTerrain", ("broadcastToCity: sent to %d clients", sentCount));
}

// ----------------------------------------------------------------------

void CityTerrainService::sendResponse(Client const & client, bool success, std::string const & regionId,
									   std::string const & errorMessage)
{
	CityTerrainPaintResponseMessage const msg(success, regionId, errorMessage);
	client.send(msg, true);
}

// ----------------------------------------------------------------------

void CityTerrainService::saveRegionToCityHall(int32 cityId, std::string const & regionId,
											   int32 modType, std::string const & shader,
											   float centerX, float centerZ, float radius,
											   float endX, float endZ, float width,
											   float height, float blendDist)
{
	if (!CityInterface::cityExists(cityId))
	{
		LOG("CityTerrain", ("saveRegionToCityHall: city %d does not exist", cityId));
		return;
	}

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
	NetworkId const & cityHallId = cityInfo.getCityHallId();
	ServerObject * const cityHall = ServerWorld::findObjectByNetworkId(cityHallId);
	if (!cityHall)
	{
		LOG("CityTerrain", ("saveRegionToCityHall: city hall not found for city %d", cityId));
		return;
	}

	// Pack region data into a single string: "regionId|modType|shader|centerX|centerZ|radius|endX|endZ|width|height|blendDist"
	std::ostringstream oss;
	oss << regionId << "|"
		<< modType << "|"
		<< shader << "|"
		<< centerX << "|"
		<< centerZ << "|"
		<< radius << "|"
		<< endX << "|"
		<< endZ << "|"
		<< width << "|"
		<< height << "|"
		<< blendDist;

	std::string regionData = oss.str();

	// Get current region count
	int regionCount = 0;
	cityHall->getObjVars().getItem(TERRAIN_OBJVAR_COUNT, regionCount);

	// Store the new region
	std::string regionObjvar = TERRAIN_OBJVAR_ROOT + "." + regionId;
	cityHall->setObjVarItem(regionObjvar, regionData);

	// Update count
	cityHall->setObjVarItem(TERRAIN_OBJVAR_COUNT, regionCount + 1);

	LOG("CityTerrain", ("saveRegionToCityHall: saved region %s (count now %d)", regionId.c_str(), regionCount + 1));
}

// ----------------------------------------------------------------------

void CityTerrainService::removeRegionFromCityHall(int32 cityId, std::string const & regionId)
{
	if (!CityInterface::cityExists(cityId))
	{
		LOG("CityTerrain", ("removeRegionFromCityHall: city %d does not exist", cityId));
		return;
	}

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
	NetworkId const & cityHallId = cityInfo.getCityHallId();
	ServerObject * const cityHall = ServerWorld::findObjectByNetworkId(cityHallId);
	if (!cityHall)
	{
		LOG("CityTerrain", ("removeRegionFromCityHall: city hall not found for city %d", cityId));
		return;
	}

	std::string regionObjvar = TERRAIN_OBJVAR_ROOT + "." + regionId;

	if (cityHall->getObjVars().hasItem(regionObjvar))
	{
		cityHall->removeObjVarItem(regionObjvar);

		// Decrement count
		int regionCount = 0;
		cityHall->getObjVars().getItem(TERRAIN_OBJVAR_COUNT, regionCount);
		if (regionCount > 0)
		{
			cityHall->setObjVarItem(TERRAIN_OBJVAR_COUNT, regionCount - 1);
		}

		LOG("CityTerrain", ("removeRegionFromCityHall: removed region %s", regionId.c_str()));
	}
}

// ----------------------------------------------------------------------

void CityTerrainService::loadRegionsFromCityHall(ServerObject * cityHall, std::vector<std::string> & outRegionData)
{
	outRegionData.clear();

	if (!cityHall)
		return;

	DynamicVariableList const & objVars = cityHall->getObjVars();

	// Look for all objvars under TERRAIN_OBJVAR_ROOT
	DynamicVariableList::NestedList const nested(objVars, TERRAIN_OBJVAR_ROOT);
	for (DynamicVariableList::NestedList::const_iterator it(nested.begin()); it != nested.end(); ++it)
	{
		std::string value;
		if (nested.getItem(it.getName(), value))
		{
			outRegionData.push_back(value);
		}
	}

	LOG("CityTerrain", ("loadRegionsFromCityHall: loaded %d regions", static_cast<int>(outRegionData.size())));
}

// ----------------------------------------------------------------------

void CityTerrainService::sendTerrainSyncToClient(Client const & client, int32 cityId)
{
	if (!CityInterface::cityExists(cityId))
	{
		LOG("CityTerrain", ("sendTerrainSyncToClient: city %d does not exist", cityId));
		return;
	}

	CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
	NetworkId const & cityHallId = cityInfo.getCityHallId();
	ServerObject * const cityHall = ServerWorld::findObjectByNetworkId(cityHallId);
	if (!cityHall)
	{
		LOG("CityTerrain", ("sendTerrainSyncToClient: city hall not found for city %d", cityId));
		return;
	}

	std::vector<std::string> regionData;
	loadRegionsFromCityHall(cityHall, regionData);

	// Parse and send each region
	for (std::vector<std::string>::const_iterator it = regionData.begin(); it != regionData.end(); ++it)
	{
		// Parse: "regionId|modType|shader|centerX|centerZ|radius|endX|endZ|width|height|blendDist"
		std::istringstream iss(*it);
		std::string regionId, shader;
		int32 modType = 0;
		float centerX = 0, centerZ = 0, radius = 0, endX = 0, endZ = 0, width = 0, height = 0, blendDist = 0;

		std::getline(iss, regionId, '|');
		iss >> modType;
		iss.ignore(1); // skip '|'
		std::getline(iss, shader, '|');
		iss >> centerX;
		iss.ignore(1);
		iss >> centerZ;
		iss.ignore(1);
		iss >> radius;
		iss.ignore(1);
		iss >> endX;
		iss.ignore(1);
		iss >> endZ;
		iss.ignore(1);
		iss >> width;
		iss.ignore(1);
		iss >> height;
		iss.ignore(1);
		iss >> blendDist;

		CityTerrainModifyMessage const msg(cityId, modType, regionId, shader, centerX, centerZ, radius, endX, endZ, width, height, blendDist);
		client.send(msg, true);
	}

	LOG("CityTerrain", ("sendTerrainSyncToClient: sent %d regions to client", static_cast<int>(regionData.size())));
}

// ======================================================================
