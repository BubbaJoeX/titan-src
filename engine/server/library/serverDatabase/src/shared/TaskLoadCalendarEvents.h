// ======================================================================
//
// TaskLoadCalendarEvents.h
// Copyright 2026 Titan Project
//
// ======================================================================

#ifndef INCLUDED_TaskLoadCalendarEvents_H
#define INCLUDED_TaskLoadCalendarEvents_H

// ======================================================================

#include "sharedDatabaseInterface/DbTaskRequest.h"
#include "sharedFoundation/NetworkId.h"

#include <string>
#include <vector>

class LoadCalendarEventsMessage;
class LoadCalendarSettingsMessage;

// ======================================================================

class TaskLoadCalendarEvents : public DB::TaskRequest
{
public:
	explicit TaskLoadCalendarEvents(uint32 requestingProcess);
	virtual ~TaskLoadCalendarEvents();

	virtual bool process(DB::Session *session);
	virtual void onComplete();

private:
	uint32 m_requestingProcess;
	LoadCalendarEventsMessage * m_eventsMsg;
	LoadCalendarSettingsMessage * m_settingsMsg;
};

// ======================================================================

#endif

