// ======================================================================
//
// CityTerrainService.h
// copyright 2026 Titan
//
// Server-side handler for city terrain painting messages
// Manages terrain modifications for player cities including:
// - Shader painting (circle and line/road)
// - Terrain flattening with height control
// - Persistence across server restarts
// - Height queries for object placement
// ======================================================================

#ifndef INCLUDED_CityTerrainService_H
#define INCLUDED_CityTerrainService_H

#include "sharedFoundation/NetworkId.h"
#include <map>
#include <string>
#include <vector>

class Client;
class CityTerrainPaintRequestMessage;
class CityTerrainRemoveRequestMessage;
class CityTerrainSyncRequestMessage;
class ServerObject;

// ======================================================================

// Terrain region types
namespace CityTerrainModificationType
{
	int const MT_SHADER_CIRCLE = 0;
	int const MT_SHADER_LINE = 1;
	int const MT_FLATTEN = 2;
	int const MT_REMOVE = 3;
	int const MT_CLEAR_ALL = 4;
}

// ======================================================================

// Server-side terrain region data
struct CityTerrainRegion
{
	std::string regionId;
	int32 cityId;
	int32 type;
	std::string shaderTemplate;
	float centerX;
	float centerZ;
	float radius;
	float endX;
	float endZ;
	float width;
	float height;
	float blendDistance;
	int64 timestamp;
	int32 priority;
	bool active;
};

// ======================================================================

class CityTerrainService
{
public:
	static void install();
	static void remove();

	// Message handlers
	static void handlePaintRequest(Client const & client, CityTerrainPaintRequestMessage const & msg);
	static void handleRemoveRequest(Client const & client, CityTerrainRemoveRequestMessage const & msg);
	static void handleSyncRequest(Client const & client, CityTerrainSyncRequestMessage const & msg);

	// Send terrain sync to a specific client when they enter a city
	static void sendTerrainSyncToClient(Client const & client, int32 cityId);

	// Send terrain sync for all cities within range of a position
	// Used when client first loads to prevent buildings from appearing to float
	static void sendNearbyCitiesTerrainSync(Client const & client, float worldX, float worldZ, float range);

	// Called when a city hall is loaded to restore terrain data
	static void onCityHallLoaded(ServerObject * cityHall, int32 cityId);

	// Get modified terrain height at a world position (considers city flatten regions)
	// Returns true if a modification applies, false if original terrain should be used
	static bool getModifiedTerrainHeight(float x, float z, float originalHeight, float & outHeight);

	// Adjust a single object's height based on city terrain modifications
	static void adjustObjectToTerrainHeight(ServerObject * object);

	// Adjust all buildout objects within a city's terrain-modified areas
	static void adjustBuildoutObjectsInCity(int32 cityId);

	// Check if a point is within any city's terrain modification area
	static bool isInCityTerrainArea(float x, float z, int32 & outCityId);

	// Get all active regions for a city
	static void getRegionsForCity(int32 cityId, std::vector<CityTerrainRegion const *> & outRegions);

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

	// Adjust objects in region to new terrain height
	static void adjustObjectsInRegion(int32 cityId, float centerX, float centerZ, float radius, float newHeight);

	// In-memory region management
	static void addRegionToMemory(CityTerrainRegion const & region);
	static void removeRegionFromMemory(std::string const & regionId);
	static void clearCityRegionsFromMemory(int32 cityId);

	// Persistence helpers
	static void saveRegionToCityHall(int32 cityId, std::string const & regionId,
									  int32 modType, std::string const & shader,
									  float centerX, float centerZ, float radius,
									  float endX, float endZ, float width,
									  float height, float blendDist);
	static void removeRegionFromCityHall(int32 cityId, std::string const & regionId);
	static void loadRegionsFromCityHall(ServerObject * cityHall, int32 cityId);

	// Helper math functions
	static bool isPointInCircle(float px, float pz, float cx, float cz, float radius);
	static bool isPointInLine(float px, float pz, float x1, float z1, float x2, float z2, float width);
	static float calculateBlendWeight(float distance, float blendDistance);
};

// ======================================================================

#endif // INCLUDED_CityTerrainService_H


