// ======================================================================
//
// TaskSaveCalendarEvent.cpp
// Copyright 2026 Titan Project
//
// ======================================================================

#include "serverDatabase/FirstServerDatabase.h"
#include "serverDatabase/TaskSaveCalendarEvent.h"

#include "serverDatabase/CalendarEventQueries.h"
#include "sharedDatabaseInterface/DbSession.h"

// ======================================================================

TaskSaveCalendarEvent::TaskSaveCalendarEvent(
	std::string const & eventId,
	std::string const & title,
	std::string const & description,
	int eventType,
	int year, int month, int day,
	int hour, int minute,
	int duration,
	int guildId, int cityId,
	std::string const & serverEventKey,
	bool recurring, int recurrenceType,
	bool broadcastStart, bool active,
	NetworkId const & creatorId
) :
	m_eventId(eventId),
	m_title(title),
	m_description(description),
	m_eventType(eventType),
	m_year(year), m_month(month), m_day(day),
	m_hour(hour), m_minute(minute),
	m_duration(duration),
	m_guildId(guildId), m_cityId(cityId),
	m_serverEventKey(serverEventKey),
	m_recurring(recurring), m_recurrenceType(recurrenceType),
	m_broadcastStart(broadcastStart), m_active(active),
	m_creatorId(creatorId)
{
}

// ----------------------------------------------------------------------

TaskSaveCalendarEvent::~TaskSaveCalendarEvent()
{
}

// ----------------------------------------------------------------------

bool TaskSaveCalendarEvent::process(DB::Session *session)
{
	DBQuery::SaveCalendarEventQuery qry(
		m_eventId, m_title, m_description, m_eventType,
		m_year, m_month, m_day, m_hour, m_minute,
		m_duration, m_guildId, m_cityId,
		m_serverEventKey, m_recurring, m_recurrenceType,
		m_broadcastStart, m_active, m_creatorId
	);
	bool rval = session->exec(&qry);
	qry.done();
	return rval;
}

// ----------------------------------------------------------------------

void TaskSaveCalendarEvent::onComplete()
{
}

// ======================================================================

