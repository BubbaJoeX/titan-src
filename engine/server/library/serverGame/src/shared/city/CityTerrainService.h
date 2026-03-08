// ======================================================================
//
// CityTerrainService.h
// copyright 2026 Titan
//
// Server-side handler for city terrain painting messages
// ======================================================================

#ifndef INCLUDED_CityTerrainService_H
#define INCLUDED_CityTerrainService_H

#include "sharedFoundation/NetworkId.h"
#include <string>

class Client;
class CityTerrainPaintRequestMessage;
class CityTerrainRemoveRequestMessage;
class CityTerrainSyncRequestMessage;

// ======================================================================

class CityTerrainService
{
public:
	static void install();
	static void remove();

	static void handlePaintRequest(Client const & client, CityTerrainPaintRequestMessage const & msg);
	static void handleRemoveRequest(Client const & client, CityTerrainRemoveRequestMessage const & msg);
	static void handleSyncRequest(Client const & client, CityTerrainSyncRequestMessage const & msg);

private:
	CityTerrainService();
	~CityTerrainService();
	CityTerrainService(CityTerrainService const &);
	CityTerrainService & operator=(CityTerrainService const &);

	static bool validateMayorPermission(Client const & client, int32 cityId);
	static bool validateCityBounds(int32 cityId, float x, float z, float radius);
	static std::string generateRegionId();
	static void broadcastToCity(int32 cityId, int32 modType, std::string const & regionId,
								std::string const & shader, float centerX, float centerZ,
								float radius, float endX, float endZ, float width,
								float height, float blendDist);
	static void sendResponse(Client const & client, bool success, std::string const & regionId,
							 std::string const & errorMessage);
};

// ======================================================================

#endif // INCLUDED_CityTerrainService_H


