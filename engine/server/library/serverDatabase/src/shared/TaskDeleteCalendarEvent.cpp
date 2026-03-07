// ======================================================================
//
// TaskDeleteCalendarEvent.cpp
// Copyright 2026 Titan Project
//
// ======================================================================

#include "serverDatabase/FirstServerDatabase.h"
#include "serverDatabase/TaskDeleteCalendarEvent.h"

#include "serverDatabase/CalendarEventQueries.h"
#include "sharedDatabaseInterface/DbSession.h"

// ======================================================================

TaskDeleteCalendarEvent::TaskDeleteCalendarEvent(std::string const & eventId) :
	m_eventId(eventId)
{
}

// ----------------------------------------------------------------------

TaskDeleteCalendarEvent::~TaskDeleteCalendarEvent()
{
}

// ----------------------------------------------------------------------

bool TaskDeleteCalendarEvent::process(DB::Session *session)
{
	DBQuery::DeleteCalendarEventQuery qry(m_eventId);
	bool rval = session->exec(&qry);
	qry.done();
	return rval;
}

// ----------------------------------------------------------------------

void TaskDeleteCalendarEvent::onComplete()
{
}

// ======================================================================

