// ======================================================================
//
// ProceduralTerrainReplicationService.cpp
// copyright 2026 Titan
//
// God Client pushes procedural terrain .trn bytes to the game server; the
// server writes the active ground scene file, reloads server terrain, and
// fans the same chunks out to every connected client.
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/ProceduralTerrainReplicationService.h"

#include "serverGame/Client.h"
#include "serverGame/ConfigServerGame.h"
#include "serverGame/GameServer.h"

#include "sharedFile/TreeFile.h"
#include "sharedFoundation/Crc.h"
#include "sharedLog/Log.h"
#include "sharedNetworkMessages/ProceduralTerrainSyncMessages.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

// ----------------------------------------------------------------------

namespace ProceduralTerrainReplicationServiceNamespace
{
	uint32 const cms_maxBytes = 32u * 1024u * 1024u;
	uint32 const cms_chunkSize = 45000u;

	struct PendingReceive
	{
		std::string fileName;
		uint32 totalSize;
		uint32 crc;
		std::vector<unsigned char> buffer;
		uint32 bytesReceived;
	};

	static PendingReceive s_pending;

	static void resetPending()
	{
		s_pending = PendingReceive();
	}

	static bool writeBufferToTreeFile(std::string const & treeFileName, std::vector<unsigned char> const & data)
	{
		char path[8192];
		if (!TreeFile::getPathName(treeFileName.c_str(), path, sizeof(path)))
		{
			LOG("ProceduralTerrainSync", ("getPathName failed for [%s]", treeFileName.c_str()));
			return false;
		}
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			LOG("ProceduralTerrainSync", ("failed to open for write [%s]", path));
			return false;
		}
		out.write(reinterpret_cast<char const *>(&data[0]), static_cast<std::streamsize>(data.size()));
		return static_cast<bool>(out);
	}

	static void broadcastChunks(std::string const & treeFileName, std::vector<unsigned char> const & data, uint32 crc32)
	{
		uint32 const total = static_cast<uint32>(data.size());
		for (uint32 offset = 0; offset < total; )
		{
			uint32 const n = std::min(cms_chunkSize, total - offset);
			std::vector<unsigned char> chunk(
				data.begin() + static_cast<std::vector<unsigned char>::difference_type>(offset),
				data.begin() + static_cast<std::vector<unsigned char>::difference_type>(offset + n));
			ProceduralTerrainSyncChunkMessage const msg(treeFileName, total, crc32, offset, chunk);
			GameServer::getInstance().spamAllClients(msg, true);
			offset += n;
		}
	}
}

// ----------------------------------------------------------------------

void ProceduralTerrainReplicationService::handleSyncChunk(Client const & client, ProceduralTerrainSyncChunkMessage const & msg)
{
	using namespace ProceduralTerrainReplicationServiceNamespace;

	if (!client.isGod())
	{
		DEBUG_WARNING(true, ("ProceduralTerrainSync: rejecting chunk from non-god client."));
		resetPending();
		return;
	}

	char const * const groundScene = ConfigServerGame::getGroundScene();
	if (!groundScene || !groundScene[0])
	{
		resetPending();
		return;
	}

	if (msg.getTerrainTreeFileName() != groundScene)
	{
		DEBUG_WARNING(true, ("ProceduralTerrainSync: file [%s] does not match server ground scene [%s].",
			msg.getTerrainTreeFileName().c_str(), groundScene));
		resetPending();
		return;
	}

	if (msg.getTotalSize() == 0 || msg.getTotalSize() > cms_maxBytes)
	{
		resetPending();
		return;
	}

	std::vector<unsigned char> const & chunkBytes = msg.getChunkBytes();
	if (chunkBytes.empty())
	{
		resetPending();
		return;
	}

	if (msg.getByteOffset() == 0)
		resetPending();

	if (msg.getByteOffset() == 0)
	{
		s_pending.fileName = msg.getTerrainTreeFileName();
		s_pending.totalSize = msg.getTotalSize();
		s_pending.crc = msg.getCrc32();
		s_pending.buffer.resize(msg.getTotalSize());
		s_pending.bytesReceived = 0;
	}

	if (msg.getTerrainTreeFileName() != s_pending.fileName ||
		msg.getTotalSize() != s_pending.totalSize ||
		msg.getCrc32() != s_pending.crc)
	{
		resetPending();
		return;
	}

	if (msg.getByteOffset() + chunkBytes.size() > s_pending.totalSize ||
		msg.getByteOffset() != s_pending.bytesReceived)
	{
		resetPending();
		return;
	}

	std::memcpy(
		&s_pending.buffer[msg.getByteOffset()],
		&chunkBytes[0],
		chunkBytes.size());
	s_pending.bytesReceived = msg.getByteOffset() + static_cast<uint32>(chunkBytes.size());

	if (s_pending.bytesReceived < s_pending.totalSize)
		return;

	uint32 const verifyCrc = Crc::calculate(
		&s_pending.buffer[0],
		static_cast<int>(s_pending.buffer.size()),
		Crc::crcInit);

	if (verifyCrc != s_pending.crc)
	{
		LOG("ProceduralTerrainSync", ("CRC mismatch (expected %08x got %08x)", s_pending.crc, verifyCrc));
		resetPending();
		return;
	}

	if (!writeBufferToTreeFile(s_pending.fileName, s_pending.buffer))
	{
		resetPending();
		return;
	}

	TreeFile::clearCachedFiles();
	GameServer::getInstance().loadTerrain();

	LOG("ProceduralTerrainSync", ("applied %s (%u bytes), broadcasting to clients.", s_pending.fileName.c_str(), s_pending.totalSize));

	broadcastChunks(s_pending.fileName, s_pending.buffer, s_pending.crc);
	resetPending();
}
