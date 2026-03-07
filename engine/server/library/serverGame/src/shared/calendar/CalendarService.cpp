// ======================================================================
//
// CalendarService.cpp
// Copyright 2026 Titan Project
//
// Server-side calendar service implementation.
//
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/calendar/CalendarService.h"

#include "serverGame/Client.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/GameServer.h"
#include "serverGame/GuildInterface.h"
#include "serverGame/GuildMemberInfo.h"
#include "serverGame/CityInterface.h"
#include "serverGame/CityInfo.h"
#include "serverGame/ServerWorld.h"
#include "serverGame/Chat.h"
#include "serverNetworkMessages/CalendarEventMessage.h"
#include "sharedFoundation/CrcConstexpr.hpp"
#include "sharedFoundation/NetworkId.h"
#include "sharedNetworkMessages/CalendarMessages.h"
#include "sharedNetworkMessages/GenericValueTypeMessage.h"
#include "sharedLog/Log.h"
#include "sharedMath/Vector.h"

#include <ctime>

// ======================================================================

CalendarService * CalendarService::ms_instance = nullptr;
float const CalendarService::CHECK_INTERVAL = 60.0f;

// ======================================================================

void CalendarService::install()
{
	DEBUG_FATAL(ms_instance != nullptr, ("CalendarService already installed"));
	ms_instance = new CalendarService();
	ms_instance->loadFromClusterData();
}

// ----------------------------------------------------------------------

void CalendarService::remove()
{
	if (ms_instance)
	{
		ms_instance->saveToClusterData();
		delete ms_instance;
		ms_instance = nullptr;
	}
}

// ----------------------------------------------------------------------

CalendarService & CalendarService::getInstance()
{
	DEBUG_FATAL(ms_instance == nullptr, ("CalendarService not installed"));
	return *ms_instance;
}

// ----------------------------------------------------------------------

CalendarService::CalendarService() :
	MessageDispatch::Receiver(),
	m_events(),
	m_nextEventId(1),
	m_bgTexture("ui_calendar_bg.dds"),
	m_srcX(0),
	m_srcY(0),
	m_srcW(512),
	m_srcH(512),
	m_checkTimer(0.0f),
	m_needsLoad(true)
{
	connectToMessage("LoadCalendarEventsMessage");
	connectToMessage("LoadCalendarSettingsMessage");
}

// ----------------------------------------------------------------------

CalendarService::~CalendarService()
{
}

// ----------------------------------------------------------------------

void CalendarService::update(float deltaTime)
{
	if (m_needsLoad)
	{
		m_needsLoad = false;
		RequestLoadCalendarEventsMessage msg;
		GameServer::getInstance().sendToDatabaseServer(msg);
		LOG("CalendarService", ("Requested calendar events from database server"));
	}

	m_checkTimer += deltaTime;
	if (m_checkTimer >= CHECK_INTERVAL)
	{
		m_checkTimer = 0.0f;
		checkAndTriggerEvents();
	}
}

// ======================================================================
// Event Management
// ======================================================================

std::string CalendarService::createEvent(NetworkId const & creatorId, CalendarEventData const & eventData)
{
	std::string eventId = generateEventId();

	CalendarEventData newEvent = eventData;
	newEvent.eventId = eventId;
	newEvent.creatorId = creatorId;
	newEvent.active = false;

	m_events[eventId] = newEvent;

	LOG("CalendarService", ("Event created: %s by %s", eventId.c_str(), creatorId.getValueString().c_str()));

	// Notify relevant players
	notifyEventCreated(newEvent);

	// Save to database
	saveEventToDatabase(newEvent);

	return eventId;
}

// ----------------------------------------------------------------------

bool CalendarService::updateEvent(std::string const & eventId, CalendarEventData const & eventData)
{
	EventMap::iterator it = m_events.find(eventId);
	if (it == m_events.end())
		return false;

	// Preserve the original ID and creator
	CalendarEventData updated = eventData;
	updated.eventId = eventId;
	updated.creatorId = it->second.creatorId;

	it->second = updated;

	LOG("CalendarService", ("Event updated: %s", eventId.c_str()));

	notifyEventUpdated(updated);
	saveEventToDatabase(updated);

	return true;
}

// ----------------------------------------------------------------------

bool CalendarService::deleteEvent(std::string const & eventId)
{
	EventMap::iterator it = m_events.find(eventId);
	if (it == m_events.end())
		return false;

	CalendarEventData deletedEvent = it->second;
	m_events.erase(it);

	LOG("CalendarService", ("Event deleted: %s", eventId.c_str()));

	notifyEventDeleted(deletedEvent);
	deleteEventFromDatabase(eventId);

	return true;
}

// ----------------------------------------------------------------------

CalendarEventData const * CalendarService::getEvent(std::string const & eventId) const
{
	EventMap::const_iterator it = m_events.find(eventId);
	if (it == m_events.end())
		return nullptr;
	return &(it->second);
}

// ======================================================================

// Query Functions
// ======================================================================

void CalendarService::getEventsForPlayer(NetworkId const & playerId, int32 year, int32 month, std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	if (!playerId.isValid())
		return;

	// Get player's guild and city
	int32 playerGuildId = 0;
	int32 playerCityId = 0;

	ServerObject * playerObj = ServerWorld::findObjectByNetworkId(playerId);
	if (playerObj != nullptr)
	{
		CreatureObject * creature = playerObj->asCreatureObject();
		if (creature != nullptr)
		{
			playerGuildId = GuildInterface::getGuildId(creature->getNetworkId());
			std::vector<int> const & cityIds = CityInterface::getCitizenOfCityId(creature->getNetworkId());
			if (!cityIds.empty())
				playerCityId = cityIds.front();
		}
	}

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;

		// Filter by month if specified
		if (year > 0 && month > 0)
		{
			if (evt.year != year || evt.month != month)
				continue;
		}

		// Check visibility based on event type
		switch (evt.eventType)
		{
			case ET_Staff:
			case ET_Server:
				// Everyone can see staff and server events
				outEvents.push_back(evt);
				break;

			case ET_Guild:
				// Only guild members can see guild events
				if (evt.guildId == playerGuildId && playerGuildId > 0)
					outEvents.push_back(evt);
				break;

			case ET_City:
				// Only city citizens can see city events
				if (evt.cityId == playerCityId && playerCityId > 0)
					outEvents.push_back(evt);
				break;
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::getEventsForDay(int32 year, int32 month, int32 day, std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;
		if (evt.year == year && evt.month == month && evt.day == day)
		{
			outEvents.push_back(evt);
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::getEventsForGuild(int32 guildId, std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;
		if (evt.eventType == ET_Guild && evt.guildId == guildId)
		{
			outEvents.push_back(evt);
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::getEventsForCity(int32 cityId, std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;
		if (evt.eventType == ET_City && evt.cityId == cityId)
		{
			outEvents.push_back(evt);
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::getStaffEvents(std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;
		if (evt.eventType == ET_Staff)
		{
			outEvents.push_back(evt);
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::getServerEvents(std::vector<CalendarEventData> & outEvents) const
{
	outEvents.clear();

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;
		if (evt.eventType == ET_Server)
		{
			outEvents.push_back(evt);
		}
	}
}

// ======================================================================
// Permission Checks
// ======================================================================

bool CalendarService::canCreateEvent(CreatureObject const * player, int32 eventType) const
{
	if (!player)
		return false;

	// Staff can create any event
	Client const * client = player->getClient();
	if (client && client->isGod())
		return true;

	switch (eventType)
	{
		case ET_Staff:
		case ET_Server:
			// Only staff can create these
			return false;

		case ET_Guild:
		{
			// Guild leader or officer
			int32 guildId = GuildInterface::getGuildId(player->getNetworkId());
			if (guildId <= 0)
				return false;
			// Check if player is guild leader
			NetworkId const & leaderId = GuildInterface::getGuildLeaderId(guildId);
			if (leaderId == player->getNetworkId())
				return true;
			// Check if player has guild permissions
			GuildMemberInfo const * memberInfo = GuildInterface::getGuildMemberInfo(guildId, player->getNetworkId());
			return memberInfo && (memberInfo->m_permissions & GuildInterface::Mail) != 0;
		}

		case ET_City:
		{
			// City mayor only
			std::vector<int> const & cityIds = CityInterface::getCitizenOfCityId(player->getNetworkId());
			if (cityIds.empty())
				return false;
			int32 cityId = cityIds.front();
			CityInfo const & cityInfo = CityInterface::getCityInfo(cityId);
			return cityInfo.getLeaderId() == player->getNetworkId();
		}
	}

	return false;
}

// ----------------------------------------------------------------------

bool CalendarService::canEditEvent(CreatureObject const * player, std::string const & eventId) const
{
	if (!player)
		return false;

	CalendarEventData const * evt = getEvent(eventId);
	if (!evt)
		return false;

	// Staff can edit any event
	Client const * client = player->getClient();
	if (client && client->isGod())
		return true;

	// Creator can edit their own event
	if (evt->creatorId == player->getNetworkId())
		return true;

	return false;
}

// ----------------------------------------------------------------------

bool CalendarService::canDeleteEvent(CreatureObject const * player, std::string const & eventId) const
{
	return canEditEvent(player, eventId);
}

// ----------------------------------------------------------------------

bool CalendarService::canViewEvent(CreatureObject const * player, CalendarEventData const & eventData) const
{
	if (!player)
		return false;

	// Staff can view all events
	Client const * client = player->getClient();
	if (client && client->isGod())
		return true;

	switch (eventData.eventType)
	{
		case ET_Staff:
		case ET_Server:
			return true;

		case ET_Guild:
		{
			int32 playerGuildId = GuildInterface::getGuildId(player->getNetworkId());
			return eventData.guildId == playerGuildId && playerGuildId > 0;
		}

		case ET_City:
		{
			std::vector<int> const & cityIds = CityInterface::getCitizenOfCityId(player->getNetworkId());
			if (cityIds.empty())
				return false;
			int32 playerCityId = cityIds.front();
			return eventData.cityId == playerCityId && playerCityId > 0;
		}
	}

	return false;
}

// ======================================================================
// Settings
// ======================================================================

void CalendarService::setSettings(std::string const & bgTexture, int32 srcX, int32 srcY, int32 srcW, int32 srcH)
{
	m_bgTexture = bgTexture;
	m_srcX = srcX;
	m_srcY = srcY;
	m_srcW = srcW;
	m_srcH = srcH;

	saveToClusterData();
}

// ----------------------------------------------------------------------

void CalendarService::getSettings(std::string & outBgTexture, int32 & outSrcX, int32 & outSrcY, int32 & outSrcW, int32 & outSrcH) const
{
	outBgTexture = m_bgTexture;
	outSrcX = m_srcX;
	outSrcY = m_srcY;
	outSrcW = m_srcW;
	outSrcH = m_srcH;
}

// ======================================================================
// Notifications
// ======================================================================

void CalendarService::notifyEventCreated(CalendarEventData const & eventData)
{
	CalendarEventNotificationMessage msg(CalendarEventNotificationMessage::NT_Created, eventData);

	switch (eventData.eventType)
	{
		case ET_Staff:
		case ET_Server:
			broadcastToAll(msg);
			break;

		case ET_Guild:
			broadcastToGuild(eventData.guildId, msg);
			break;

		case ET_City:
			broadcastToCity(eventData.cityId, msg);
			break;
	}
}

// ----------------------------------------------------------------------

void CalendarService::notifyEventUpdated(CalendarEventData const & eventData)
{
	CalendarEventNotificationMessage msg(CalendarEventNotificationMessage::NT_Updated, eventData);

	switch (eventData.eventType)
	{
		case ET_Staff:
		case ET_Server:
			broadcastToAll(msg);
			break;

		case ET_Guild:
			broadcastToGuild(eventData.guildId, msg);
			break;

		case ET_City:
			broadcastToCity(eventData.cityId, msg);
			break;
	}
}

// ----------------------------------------------------------------------

void CalendarService::notifyEventDeleted(CalendarEventData const & eventData)
{
	CalendarEventNotificationMessage msg(CalendarEventNotificationMessage::NT_Deleted, eventData);

	switch (eventData.eventType)
	{
		case ET_Staff:
		case ET_Server:
			broadcastToAll(msg);
			break;

		case ET_Guild:
			broadcastToGuild(eventData.guildId, msg);
			break;

		case ET_City:
			broadcastToCity(eventData.cityId, msg);
			break;
	}
}

// ======================================================================
// Message Handlers
// ======================================================================

void CalendarService::handleGetEventsRequest(Client const & client, int32 year, int32 month)
{
	std::vector<CalendarEventData> events;
	getEventsForPlayer(client.getCharacterObjectId(), year, month, events);

	CalendarEventsResponseMessage response(events);
	sendToClient(client, response);
}

// ----------------------------------------------------------------------

void CalendarService::handleCreateEventRequest(Client const & client, CalendarEventData const & eventData)
{
	ServerObject * playerObj = ServerWorld::findObjectByNetworkId(client.getCharacterObjectId());
	CreatureObject * player = playerObj ? playerObj->asCreatureObject() : nullptr;

	if (!player)
	{
		CalendarCreateEventResponseMessage response(false, "", "Player not found");
		sendToClient(client, response);
		return;
	}

	if (!canCreateEvent(player, eventData.eventType))
	{
		CalendarCreateEventResponseMessage response(false, "", "Permission denied");
		sendToClient(client, response);
		return;
	}

	// Fill in guild/city IDs if needed
	CalendarEventData finalEventData = eventData;
	if (eventData.eventType == ET_Guild)
	{
		finalEventData.guildId = GuildInterface::getGuildId(player->getNetworkId());
	}
	else if (eventData.eventType == ET_City)
	{
		std::vector<int> const & cityIds = CityInterface::getCitizenOfCityId(player->getNetworkId());
		if (!cityIds.empty())
			finalEventData.cityId = cityIds.front();
	}

	std::string eventId = createEvent(client.getCharacterObjectId(), finalEventData);

	if (!eventId.empty())
	{
		CalendarCreateEventResponseMessage response(true, eventId, "");
		sendToClient(client, response);
	}
	else
	{
		CalendarCreateEventResponseMessage response(false, "", "Failed to create event");
		sendToClient(client, response);
	}
}

// ----------------------------------------------------------------------

void CalendarService::handleDeleteEventRequest(Client const & client, std::string const & eventId)
{
	ServerObject * playerObj = ServerWorld::findObjectByNetworkId(client.getCharacterObjectId());
	CreatureObject * player = playerObj ? playerObj->asCreatureObject() : nullptr;

	if (!player)
	{
		CalendarDeleteEventResponseMessage response(false, eventId);
		sendToClient(client, response);
		return;
	}

	if (!canDeleteEvent(player, eventId))
	{
		CalendarDeleteEventResponseMessage response(false, eventId);
		sendToClient(client, response);
		return;
	}

	bool success = deleteEvent(eventId);
	CalendarDeleteEventResponseMessage response(success, eventId);
	sendToClient(client, response);
}

// ----------------------------------------------------------------------

void CalendarService::handleGetSettingsRequest(Client const & client)
{
	std::string bgTexture;
	int32 srcX, srcY, srcW, srcH;
	getSettings(bgTexture, srcX, srcY, srcW, srcH);

	CalendarSettingsResponseMessage response(bgTexture, srcX, srcY, srcW, srcH);
	sendToClient(client, response);
}

// ----------------------------------------------------------------------

void CalendarService::handleApplySettingsRequest(Client const & client, std::string const & bgTexture, int32 srcX, int32 srcY, int32 srcW, int32 srcH)
{
	ServerObject * playerObj = ServerWorld::findObjectByNetworkId(client.getCharacterObjectId());
	CreatureObject * player = playerObj ? playerObj->asCreatureObject() : nullptr;

	if (!player)
		return;

	Client const * playerClient = player->getClient();
	if (!playerClient || !playerClient->isGod())
	{
		// Permission denied - just ignore
		return;
	}

	setSettings(bgTexture, srcX, srcY, srcW, srcH);

	// Send confirmation
	CalendarSettingsResponseMessage response(bgTexture, srcX, srcY, srcW, srcH);
	sendToClient(client, response);
}

// ======================================================================
// Helper Functions
// ======================================================================

void CalendarService::checkAndTriggerEvents()
{
	time_t now = time(nullptr);
	struct tm * timeinfo = localtime(&now);

	int32 currentYear = timeinfo->tm_year + 1900;
	int32 currentMonth = timeinfo->tm_mon + 1;
	int32 currentDay = timeinfo->tm_mday;
	int32 currentHour = timeinfo->tm_hour;
	int32 currentMinute = timeinfo->tm_min;

	for (EventMap::iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData & evt = it->second;

		// Check if event should start
		if (!evt.active)
		{
			bool shouldStart = false;

			if (evt.year < currentYear) shouldStart = true;
			else if (evt.year == currentYear)
			{
				if (evt.month < currentMonth) shouldStart = true;
				else if (evt.month == currentMonth)
				{
					if (evt.day < currentDay) shouldStart = true;
					else if (evt.day == currentDay)
					{
						if (evt.hour < currentHour) shouldStart = true;
						else if (evt.hour == currentHour && evt.minute <= currentMinute) shouldStart = true;
					}
				}
			}

			if (shouldStart)
			{
				evt.active = true;

				if (evt.broadcastStart)
				{
					// Send start notification
					CalendarEventNotificationMessage msg(CalendarEventNotificationMessage::NT_Started, evt);

					switch (evt.eventType)
					{
						case ET_Staff:
						case ET_Server:
							broadcastToAll(msg);
							break;
						case ET_Guild:
							broadcastToGuild(evt.guildId, msg);
							break;
						case ET_City:
							broadcastToCity(evt.cityId, msg);
							break;
					}
				}

				LOG("CalendarService", ("Event started: %s", evt.eventId.c_str()));
			}
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::broadcastToGuild(int32 guildId, CalendarEventNotificationMessage const & msg)
{
	if (guildId <= 0)
		return;

	std::vector<NetworkId> members;
	GuildInterface::getGuildMemberIds(guildId, members);

	for (std::vector<NetworkId>::const_iterator it = members.begin(); it != members.end(); ++it)
	{
		ServerObject * obj = ServerWorld::findObjectByNetworkId(*it);
		if (obj && obj->isPlayerControlled())
		{
			Client * client = obj->getClient();
			if (client)
			{
				sendToClient(*client, msg);
			}
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::broadcastToCity(int32 cityId, CalendarEventNotificationMessage const & msg)
{
	if (cityId <= 0)
		return;

	std::vector<NetworkId> citizens;
	CityInterface::getCitizenIds(cityId, citizens);

	for (std::vector<NetworkId>::const_iterator it = citizens.begin(); it != citizens.end(); ++it)
	{
		ServerObject * obj = ServerWorld::findObjectByNetworkId(*it);
		if (obj && obj->isPlayerControlled())
		{
			Client * client = obj->getClient();
			if (client)
			{
				sendToClient(*client, msg);
			}
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::broadcastToAll(CalendarEventNotificationMessage const & msg)
{
	// Find all player creatures on this server within a large radius from origin
	// This will get all players in range - for a true galaxy-wide broadcast,
	// this would need to be sent through the ConnectionServer
	std::vector<ServerObject *> players;
	ServerWorld::findPlayerCreaturesInRange(Vector::zero, 100000.0f, players);

	for (std::vector<ServerObject *>::const_iterator it = players.begin(); it != players.end(); ++it)
	{
		if (*it && (*it)->isPlayerControlled())
		{
			Client * client = (*it)->getClient();
			if (client)
			{
				sendToClient(*client, msg);
			}
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::sendToClient(Client const & client, GameNetworkMessage const & msg)
{
	client.send(msg, true);
}

// ----------------------------------------------------------------------

std::string CalendarService::generateEventId()
{
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "evt_%u_%ld", m_nextEventId++, static_cast<long>(time(nullptr)));
	return std::string(buffer);
}

// ----------------------------------------------------------------------

void CalendarService::saveToClusterData()
{
	// Save all events to database via network message to database server
	LOG("CalendarService", ("Saving %d events to database", static_cast<int>(m_events.size())));

	for (EventMap::const_iterator it = m_events.begin(); it != m_events.end(); ++it)
	{
		CalendarEventData const & evt = it->second;
		saveEventToDatabase(evt);
	}

	// Save settings
	saveSettingsToDatabase();
}

// ----------------------------------------------------------------------

void CalendarService::loadFromClusterData()
{
	m_needsLoad = true;
	LOG("CalendarService", ("Calendar load deferred until GameServer is ready"));
}

// ----------------------------------------------------------------------

void CalendarService::receiveMessage(MessageDispatch::Emitter const & source, MessageDispatch::MessageBase const & message)
{
	UNREF(source);

	uint32 const messageType = message.getType();

	switch (messageType)
	{
		case constcrc("LoadCalendarEventsMessage") :
		{
			Archive::ReadIterator ri = static_cast<GameNetworkMessage const &>(message).getByteStream().begin();
			LoadCalendarEventsMessage msg(ri);
			handleLoadedEvents(msg);
			break;
		}
		case constcrc("LoadCalendarSettingsMessage") :
		{
			Archive::ReadIterator ri = static_cast<GameNetworkMessage const &>(message).getByteStream().begin();
			LoadCalendarSettingsMessage msg(ri);
			handleLoadedSettings(msg);
			break;
		}
	}
}

// ----------------------------------------------------------------------

void CalendarService::handleLoadedEvents(LoadCalendarEventsMessage const & msg)
{
	std::vector<CalendarEventRow> const & rows = msg.getEvents();

	m_events.clear();

	uint32 maxId = 0;
	for (std::vector<CalendarEventRow>::const_iterator it = rows.begin(); it != rows.end(); ++it)
	{
		CalendarEventData evt;
		evt.eventId        = it->eventId;
		evt.title          = it->title;
		evt.description    = it->description;
		evt.eventType      = it->eventType;
		evt.year           = it->year;
		evt.month          = it->month;
		evt.day            = it->day;
		evt.hour           = it->hour;
		evt.minute         = it->minute;
		evt.duration       = it->duration;
		evt.guildId        = it->guildId;
		evt.cityId         = it->cityId;
		evt.serverEventKey = it->serverEventKey;
		evt.recurring      = it->recurring;
		evt.recurrenceType = it->recurrenceType;
		evt.broadcastStart = it->broadcastStart;
		evt.active         = it->active;
		evt.creatorId      = it->creatorId;

		m_events[evt.eventId] = evt;

		// Track the highest numeric event ID to avoid collisions
		if (evt.eventId.substr(0, 4) == "evt_")
		{
			std::string numPart = evt.eventId.substr(4);
			size_t underscorePos = numPart.find('_');
			if (underscorePos != std::string::npos)
			{
				uint32 idNum = static_cast<uint32>(atoi(numPart.substr(0, underscorePos).c_str()));
				if (idNum > maxId)
					maxId = idNum;
			}
		}
	}

	if (maxId >= m_nextEventId)
		m_nextEventId = maxId + 1;

	LOG("CalendarService", ("Loaded %d calendar events from database", static_cast<int>(m_events.size())));
}

// ----------------------------------------------------------------------

void CalendarService::handleLoadedSettings(LoadCalendarSettingsMessage const & msg)
{
	m_bgTexture = msg.getBgTexture();
	m_srcX = msg.getSrcX();
	m_srcY = msg.getSrcY();
	m_srcW = msg.getSrcW();
	m_srcH = msg.getSrcH();

	LOG("CalendarService", ("Loaded calendar settings from database (texture: %s)", m_bgTexture.c_str()));
}

// ----------------------------------------------------------------------

void CalendarService::saveEventToDatabase(CalendarEventData const & evt)
{
	SaveCalendarEventMessage const msg(
		evt.eventId,
		evt.title,
		evt.description,
		evt.eventType,
		evt.year,
		evt.month,
		evt.day,
		evt.hour,
		evt.minute,
		evt.duration,
		evt.guildId,
		evt.cityId,
		evt.serverEventKey,
		evt.recurring,
		evt.recurrenceType,
		evt.broadcastStart,
		evt.active,
		evt.creatorId
	);
	GameServer::getInstance().sendToDatabaseServer(msg);
}

// ----------------------------------------------------------------------

void CalendarService::deleteEventFromDatabase(std::string const & eventId)
{
	DeleteCalendarEventMessage const msg(eventId);
	GameServer::getInstance().sendToDatabaseServer(msg);
}

// ----------------------------------------------------------------------

void CalendarService::saveSettingsToDatabase()
{
	SaveCalendarSettingsMessage const msg(
		m_bgTexture,
		m_srcX,
		m_srcY,
		m_srcW,
		m_srcH,
		NetworkId::cms_invalid
	);
	GameServer::getInstance().sendToDatabaseServer(msg);
}

// ======================================================================

