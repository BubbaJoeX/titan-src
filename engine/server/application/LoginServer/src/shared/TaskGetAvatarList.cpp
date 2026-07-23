// ======================================================================
//
// TaskGetAvatarList.cpp
// copyright (c) 2001 Sony Online Entertainment
//
// ======================================================================

#include "FirstLoginServer.h"
#include "TaskGetAvatarList.h"

#include "serverNetworkMessages/AvatarList.h"
#include "CentralServerConnection.h"
#include "ConfigLoginServer.h"
#include "DatabaseConnection.h"
#include "sharedLog/Log.h"
#include "serverNetworkMessages/TransferAccountData.h"
#include "serverNetworkMessages/TransferAccountDataArchive.h"
#include "serverNetworkMessages/TransferCharacterData.h"
#include "sharedDatabaseInterface/DbSession.h"
#include "sharedNetworkMessages/GenericValueTypeMessage.h"
#include "TaskUpgradeAccount.h"
#include <set>
#include <vector>

// ======================================================================

namespace
{
	std::set<uint32> s_reportedSlotPolicyMismatch;
}

// ======================================================================

TaskGetAvatarList::TaskGetAvatarList (StationId stationId, int clusterGroupId, const std::vector<uint32> & clusterIds, const TransferCharacterData * transferCharacterData) :
		TaskRequest(),
		m_stationId(stationId),
		m_stationIdNumberJediSlot(0),
		m_clusterGroupId(clusterGroupId),
		m_clusterIds(clusterIds),
		m_avatars(),
		m_availableCharacterSlots(),
		m_transferCharacterData(0),
		m_transferAccountData(0)
{
	if(transferCharacterData)
	{
		m_transferCharacterData = new TransferCharacterData(*transferCharacterData);
	}
}

// ----------------------------------------------------------------------

TaskGetAvatarList::TaskGetAvatarList (int clusterGroupId, const TransferAccountData * transferAccountData) :
		TaskRequest(),
		m_stationId(),
		m_stationIdNumberJediSlot(0),
		m_clusterGroupId(clusterGroupId),
		m_clusterIds(),
		m_avatars(),
		m_availableCharacterSlots(),
		m_transferCharacterData(0),
		m_transferAccountData(0)
{
	if(transferAccountData)
	{
		m_transferAccountData = new TransferAccountData(*transferAccountData);
		m_stationId = m_transferAccountData->getSourceStationId();
	}
}

// ----------------------------------------------------------------------

TaskGetAvatarList::~TaskGetAvatarList()
{
	delete m_transferCharacterData;
	m_transferCharacterData = 0;
	delete m_transferAccountData;
	m_transferAccountData = 0;
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::process(DB::Session *session)
{
	if (m_transferAccountData)
	{
		// if we are doing an account transfer, we need to also check the number of avatars in the destination station id 
		GetCharactersQuery qry;
		int rowsFetched;
		
		qry.station_id = m_transferAccountData->getDestinationStationId();
		qry.cluster_group_id = m_clusterGroupId;

		if (! (session->exec(&qry)))
			return false;

		bool hasAvatars = false;
		while ((rowsFetched = qry.fetch()) > 0)
		{
			hasAvatars = true;
		}

		m_transferAccountData->setDestinationHasAvatars(hasAvatars);
	}

	m_stationIdNumberJediSlot = 0;
	if (!m_transferAccountData)
	{
		TaskUpgradeAccount::QueryJediQuery qryHasJediSlot;
		qryHasJediSlot.station_id = m_stationId; //lint !e713 // loss of precision unsigned long to long
		qryHasJediSlot.character_type = 2;

		if (session->exec(&qryHasJediSlot))
		{
			m_stationIdNumberJediSlot = qryHasJediSlot.result.getValue();
		}

		qryHasJediSlot.done();
	}

	GetCharactersQuery qry;
	int rowsFetched;
	
	qry.station_id = m_stationId; //lint !e713 // loss of precision unsigned long to long
	qry.cluster_group_id = m_clusterGroupId;

	if (! (session->exec(&qry)))
		return false;

	AvatarRecord temp;
	
	while ((rowsFetched = qry.fetch()) > 0)
	{
		qry.character_name.getValue(temp.m_name);
		qry.object_template_id.getValue(temp.m_objectTemplateId);
		qry.object_id.getValue(temp.m_networkId);
		qry.cluster_id.getValue(temp.m_clusterId);
		qry.character_type.getValue(temp.m_characterType);

		m_avatars.push_back(temp);
	}
	qry.done();
	if (rowsFetched < 0)
		return false;

	if (!m_transferAccountData)
	{
		int const configuredMaximum = std::max(0, ConfigLoginServer::getMaxCharactersPerAccount());
		for (std::vector<uint32>::const_iterator clusterId = m_clusterIds.begin(); clusterId != m_clusterIds.end(); ++clusterId)
		{
			GetOpenSlotCapacityQuery capacityQuery;
			capacityQuery.station_id = static_cast<long>(m_stationId);
			capacityQuery.cluster_id = static_cast<long>(*clusterId);
			if (!session->exec(&capacityQuery))
				return false;
			capacityQuery.done();
			int const rawPolicyCapacity = static_cast<int>(capacityQuery.result.getValue());
			int const policyCapacity = std::max(0, rawPolicyCapacity);

			GetOpenCharacterSlotsQuery slotsQuery;
			slotsQuery.station_id = static_cast<long>(m_stationId);
			slotsQuery.cluster_id = static_cast<long>(*clusterId);

			if (!session->exec(&slotsQuery))
				return false;

			int slotsByType[4] = { 0, 0, 0, 0 };
			while ((rowsFetched = slotsQuery.fetch()) > 0)
			{
				int const type = static_cast<int>(slotsQuery.character_type_id.getValue());
				if (type >= 0 && type < 4)
					slotsByType[type] = std::max(0, static_cast<int>(slotsQuery.num_open_slots.getValue()));
			}
			slotsQuery.done();
			if (rowsFetched < 0)
				return false;

			int const rawCount = slotsByType[1] + std::min(slotsByType[2], slotsByType[3]);
			int const availableCount = std::max(0, std::min(configuredMaximum, std::min(policyCapacity, rawCount)));
			bool const policyMismatch = rawPolicyCapacity < 0 || std::min(policyCapacity, rawCount) > configuredMaximum;
			if (policyMismatch && s_reportedSlotPolicyMismatch.insert(*clusterId).second)
				DEBUG_WARNING(true, ("Available character slot policy mismatch for cluster %lu; capacity=%d calculated=%d configured=%d (further warnings suppressed)", *clusterId, rawPolicyCapacity, rawCount, configuredMaximum));
			m_availableCharacterSlots.push_back(std::make_pair(*clusterId, availableCount));
		}
	}

	return true;
}

// ----------------------------------------------------------------------

void TaskGetAvatarList::onComplete()
{
	if (m_transferAccountData)
	{
		if (m_transferAccountData->getDestinationHasAvatars())
		{
			// send a message back to the transfer server
			LOG("CustomerService", ("CharacterTransfer: Cannot complete account transfer from %lu to %lu: destination stationId contains avatars\n", m_transferAccountData->getSourceStationId(), m_transferAccountData->getDestinationStationId()));
			const GenericValueTypeMessage<TransferAccountData> response("TransferAccountFailedDestinationNotEmpty", *m_transferAccountData);
			CentralServerConnection::sendToCentralServer(m_transferAccountData->getStartGalaxy(), response);
		}
		else
			DatabaseConnection::getInstance().onAvatarListRetrievedAccountTransfer(m_avatars, m_transferAccountData);

	}
	else
	{
		DatabaseConnection::getInstance().onAvatarListRetrieved(m_stationId, m_stationIdNumberJediSlot, m_avatars, m_availableCharacterSlots, m_transferCharacterData);
	}
}

// ----------------------------------------------------------------------

AvatarList const & TaskGetAvatarList::getAvatars() const
{
	return m_avatars;
}

// ----------------------------------------------------------------------

StationId TaskGetAvatarList::getStationId() const
{
	return m_stationId;
}

// ----------------------------------------------------------------------

const std::vector<std::pair<uint32, int> > & TaskGetAvatarList::getAvailableCharacterSlots() const
{
	return m_availableCharacterSlots;
}

// ======================================================================

void TaskGetAvatarList::GetCharactersQuery::getSQL(std::string &sql)
{
	sql=std::string("begin :result := ")+DatabaseConnection::getInstance().getSchemaQualifier()+"login.get_avatar_list(:station_id, :cluster_group_id); end;";
 	// DEBUG_REPORT_LOG(true, ("TaskGetAvatarList SQL: %s\n", sql.c_str()));
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::GetCharactersQuery::bindParameters()
{
	if (!bindParameter(station_id)) return false;
	if (!bindParameter(cluster_group_id)) return false;

	return true;	
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::GetCharactersQuery::bindColumns()
{
	if (!bindCol(character_name)) return false;
	if (!bindCol(object_template_id)) return false;
	if (!bindCol(object_id)) return false;
	if (!bindCol(cluster_id)) return false;
	if (!bindCol(character_type)) return false;

	return true;
}

// ----------------------------------------------------------------------

DB::Query::QueryMode TaskGetAvatarList::GetCharactersQuery::getExecutionMode() const
{
	return MODE_PLSQL_REFCURSOR;
}

// ----------------------------------------------------------------------

TaskGetAvatarList::GetCharactersQuery::GetCharactersQuery() :
		Query(),
		station_id(),
		cluster_group_id(),
		character_name(),
		object_template_id(),
		object_id(),
		cluster_id(),
		character_type()
{
}

// ----------------------------------------------------------------------

void TaskGetAvatarList::GetOpenCharacterSlotsQuery::getSQL(std::string &sql)
{
	sql = std::string("begin :rc := ") + DatabaseConnection::getInstance().getSchemaQualifier() + "login.get_open_character_slots(:station_id,:cluster_id); end;";
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::GetOpenCharacterSlotsQuery::bindParameters()
{
	if (!bindParameter(station_id)) return false;
	if (!bindParameter(cluster_id)) return false;
	return true;
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::GetOpenCharacterSlotsQuery::bindColumns()
{
	if (!bindCol(character_type_id)) return false;
	if (!bindCol(num_open_slots)) return false;
	return true;
}

// ----------------------------------------------------------------------

DB::Query::QueryMode TaskGetAvatarList::GetOpenCharacterSlotsQuery::getExecutionMode() const
{
	return MODE_PLSQL_REFCURSOR;
}

// ----------------------------------------------------------------------

TaskGetAvatarList::GetOpenCharacterSlotsQuery::GetOpenCharacterSlotsQuery() :
	Query(),
	station_id(),
	cluster_id(),
	character_type_id(),
	num_open_slots()
{
}

// ----------------------------------------------------------------------

void TaskGetAvatarList::GetOpenSlotCapacityQuery::getSQL(std::string &sql)
{
	sql = std::string("begin select least(account_limit - (select count(*) from ") +
		DatabaseConnection::getInstance().getSchemaQualifier() +
		"swg_characters where (station_id = :station_id or station_id in (select case when child_id = :station_id then parent_id else child_id end from " +
		DatabaseConnection::getInstance().getSchemaQualifier() +
		"account_map where parent_id = :station_id or child_id = :station_id)) and enabled = 'Y'), cluster_limit - num_characters) into :result from " +
		DatabaseConnection::getInstance().getSchemaQualifier() + "default_char_limits, " +
		DatabaseConnection::getInstance().getSchemaQualifier() + "cluster_list where id = :cluster_id; end;";
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::GetOpenSlotCapacityQuery::bindParameters()
{
	if (!bindParameter(station_id)) return false;
	if (!bindParameter(cluster_id)) return false;
	if (!bindParameter(result)) return false;
	return true;
}

// ----------------------------------------------------------------------

bool TaskGetAvatarList::GetOpenSlotCapacityQuery::bindColumns()
{
	return true;
}

// ----------------------------------------------------------------------

DB::Query::QueryMode TaskGetAvatarList::GetOpenSlotCapacityQuery::getExecutionMode() const
{
	return MODE_PROCEXEC;
}

// ----------------------------------------------------------------------

TaskGetAvatarList::GetOpenSlotCapacityQuery::GetOpenSlotCapacityQuery() :
	Query(),
	station_id(),
	cluster_id(),
	result()
{
}

// ======================================================================
