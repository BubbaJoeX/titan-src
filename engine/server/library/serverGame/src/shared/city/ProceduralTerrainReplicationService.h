// ======================================================================
//
// ProceduralTerrainReplicationService.h
// copyright 2026 Titan
// ======================================================================

#ifndef INCLUDED_ProceduralTerrainReplicationService_H
#define INCLUDED_ProceduralTerrainReplicationService_H

class Client;
class ProceduralTerrainSyncChunkMessage;

namespace ProceduralTerrainReplicationService
{
	void handleSyncChunk(Client const & client, ProceduralTerrainSyncChunkMessage const & msg);
}

#endif // INCLUDED_ProceduralTerrainReplicationService_H
