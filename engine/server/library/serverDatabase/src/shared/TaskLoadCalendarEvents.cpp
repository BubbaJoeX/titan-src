// ======================================================================
//
// TaskLoadCalendarEvents.cpp
// Copyright 2026 Titan Project
//
// ======================================================================

#include "serverDatabase/FirstServerDatabase.h"
#include "serverDatabase/TaskLoadCalendarEvents.h"

#include "serverDatabase/CalendarEventQueries.h"
#include "serverDatabase/DatabaseProcess.h"
#include "serverDatabase/GameServerConnection.h"
#include "serverNetworkMessages/CalendarEventMessage.h"
#include "sharedDatabaseInterface/DbSession.h"
#include "sharedLog/Log.h"

// ======================================================================

TaskLoadCalendarEvents::TaskLoadCalendarEvents(uint32 requestingProcess) :
	m_requestingProcess(requestingProcess),
	m_eventsMsg(nullptr),
	m_settingsMsg(nullptr)
{
}

// ----------------------------------------------------------------------

TaskLoadCalendarEvents::~TaskLoadCalendarEvents()
{
	delete m_eventsMsg;
	m_eventsMsg = nullptr;
	delete m_settingsMsg;
	m_settingsMsg = nullptr;
}

// ----------------------------------------------------------------------

bool TaskLoadCalendarEvents::process(DB::Session *session)
{
	int rowsFetched;

	// Load events
	{
		std::vector<CalendarEventRow> eventRows;

		DBQuery::LoadCalendarEventsQuery qry;
		if (!(session->exec(&qry)))
			return false;

		while ((rowsFetched = qry.fetch()) > 0)
		{
			std::string eventId, title, description, serverEventKey;
			std::string recurring, broadcastStart, active;
			long eventType, year, month, day, hour, minute, duration;
			long guildId, cityId, recurrenceType;
			NetworkId creatorId;

			qry.getData(
				eventId, title, description,
				eventType, year, month, day, hour, minute,
				duration, guildId, cityId,
				serverEventKey, recurring, recurrenceType,
				broadcastStart, active, creatorId
			);

			CalendarEventRow row;
			row.eventId = eventId;
			row.title = title;
			row.description = description;
			row.eventType = static_cast<int32>(eventType);
			row.year = static_cast<int32>(year);
			row.month = static_cast<int32>(month);
			row.day = static_cast<int32>(day);
			row.hour = static_cast<int32>(hour);
			row.minute = static_cast<int32>(minute);
			row.duration = static_cast<int32>(duration);
			row.guildId = static_cast<int32>(guildId);
			row.cityId = static_cast<int32>(cityId);
			row.serverEventKey = serverEventKey;
			row.recurring = (recurring == "Y");
			row.recurrenceType = static_cast<int32>(recurrenceType);
			row.broadcastStart = (broadcastStart == "Y");
			row.active = (active == "Y");
			row.creatorId = creatorId;

			eventRows.push_back(row);
		}
		qry.done();

		if (rowsFetched < 0)
			return false;

		m_eventsMsg = new LoadCalendarEventsMessage(eventRows);
	}

	// Load settings
	{
		DBQuery::LoadCalendarSettingsQuery qry;
		if (!(session->exec(&qry)))
			return false;

		if ((rowsFetched = qry.fetch()) > 0)
		{
			std::string bgTexture;
			long srcX, srcY, srcW, srcH;
			qry.getData(bgTexture, srcX, srcY, srcW, srcH);
			m_settingsMsg = new LoadCalendarSettingsMessage(
				bgTexture,
				static_cast<int32>(srcX),
				static_cast<int32>(srcY),
				static_cast<int32>(srcW),
				static_cast<int32>(srcH)
			);
		}
		qry.done();
	}

	return true;
}

// ----------------------------------------------------------------------

void TaskLoadCalendarEvents::onComplete()
{
	GameServerConnection *conn = DatabaseProcess::getInstance().getConnectionByProcess(m_requestingProcess);
	if (!conn)
	{
		LOG("CalendarService", ("Discarded loaded calendar events because Game Server has disconnected."));
		return;
	}

	if (m_eventsMsg)
	{
		LOG("CalendarService", ("Sending %d calendar events to game server.", static_cast<int>(m_eventsMsg->getEvents().size())));
		conn->send(*m_eventsMsg, true);
	}

	if (m_settingsMsg)
	{
		conn->send(*m_settingsMsg, true);
	}
}

// ======================================================================


