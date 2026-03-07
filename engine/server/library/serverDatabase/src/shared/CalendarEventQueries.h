// ======================================================================
//
// CalendarEventQueries.h
// Copyright 2026 Titan Project
//
// Database queries for the calendar system.
//
// ======================================================================

#ifndef INCLUDED_CalendarEventQueries_H
#define INCLUDED_CalendarEventQueries_H

// ======================================================================

#include "sharedDatabaseInterface/Bindable.h"
#include "sharedDatabaseInterface/BindableNetworkId.h"
#include "sharedDatabaseInterface/DbQuery.h"

// ======================================================================

namespace DBQuery {

// ======================================================================

class SaveCalendarEventQuery : public DB::Query
{
public:
	SaveCalendarEventQuery(
		std::string const & eventId,
		std::string const & title,
		std::string const & description,
		int eventType,
		int year,
		int month,
		int day,
		int hour,
		int minute,
		int duration,
		int guildId,
		int cityId,
		std::string const & serverEventKey,
		bool recurring,
		int recurrenceType,
		bool broadcastStart,
		bool active,
		NetworkId const & creatorId
	);

	virtual void getSQL(std::string &sql);
	virtual bool bindParameters();
	virtual bool bindColumns();

protected:
	virtual QueryMode getExecutionMode() const;

private:
	DB::BindableString<64>    m_eventId;
	DB::BindableString<256>   m_title;
	DB::BindableString<2000>  m_description;
	DB::BindableLong          m_eventType;
	DB::BindableLong          m_year;
	DB::BindableLong          m_month;
	DB::BindableLong          m_day;
	DB::BindableLong          m_hour;
	DB::BindableLong          m_minute;
	DB::BindableLong          m_duration;
	DB::BindableLong          m_guildId;
	DB::BindableLong          m_cityId;
	DB::BindableString<64>    m_serverEventKey;
	DB::BindableString<1>     m_recurring;
	DB::BindableLong          m_recurrenceType;
	DB::BindableString<1>     m_broadcastStart;
	DB::BindableString<1>     m_active;
	DB::BindableNetworkId     m_creatorId;

	SaveCalendarEventQuery(SaveCalendarEventQuery const &);
	SaveCalendarEventQuery & operator=(SaveCalendarEventQuery const &);
};

// ======================================================================

class DeleteCalendarEventQuery : public DB::Query
{
public:
	explicit DeleteCalendarEventQuery(std::string const & eventId);

	virtual void getSQL(std::string &sql);
	virtual bool bindParameters();
	virtual bool bindColumns();

protected:
	virtual QueryMode getExecutionMode() const;

private:
	DB::BindableString<64> m_eventId;

	DeleteCalendarEventQuery(DeleteCalendarEventQuery const &);
	DeleteCalendarEventQuery & operator=(DeleteCalendarEventQuery const &);
};

// ======================================================================

class LoadCalendarEventsQuery : public DB::Query
{
public:
	LoadCalendarEventsQuery();

	virtual void getSQL(std::string &sql);
	virtual bool bindParameters();
	virtual bool bindColumns();

	void getData(
		std::string & eventId,
		std::string & title,
		std::string & description,
		long & eventType,
		long & year,
		long & month,
		long & day,
		long & hour,
		long & minute,
		long & duration,
		long & guildId,
		long & cityId,
		std::string & serverEventKey,
		std::string & recurring,
		long & recurrenceType,
		std::string & broadcastStart,
		std::string & active,
		NetworkId & creatorId
	) const;

protected:
	virtual QueryMode getExecutionMode() const;

private:
	DB::BindableString<64>    m_eventId;
	DB::BindableString<256>   m_title;
	DB::BindableString<2000>  m_description;
	DB::BindableLong          m_eventType;
	DB::BindableLong          m_year;
	DB::BindableLong          m_month;
	DB::BindableLong          m_day;
	DB::BindableLong          m_hour;
	DB::BindableLong          m_minute;
	DB::BindableLong          m_duration;
	DB::BindableLong          m_guildId;
	DB::BindableLong          m_cityId;
	DB::BindableString<64>    m_serverEventKey;
	DB::BindableString<1>     m_recurring;
	DB::BindableLong          m_recurrenceType;
	DB::BindableString<1>     m_broadcastStart;
	DB::BindableString<1>     m_active;
	DB::BindableNetworkId     m_creatorId;

	LoadCalendarEventsQuery(LoadCalendarEventsQuery const &);
	LoadCalendarEventsQuery & operator=(LoadCalendarEventsQuery const &);
};

// ======================================================================

class SaveCalendarSettingsQuery : public DB::Query
{
public:
	SaveCalendarSettingsQuery(
		std::string const & bgTexture,
		int srcX, int srcY, int srcW, int srcH,
		NetworkId const & modifiedBy
	);

	virtual void getSQL(std::string &sql);
	virtual bool bindParameters();
	virtual bool bindColumns();

protected:
	virtual QueryMode getExecutionMode() const;

private:
	DB::BindableString<256>   m_bgTexture;
	DB::BindableLong          m_srcX;
	DB::BindableLong          m_srcY;
	DB::BindableLong          m_srcW;
	DB::BindableLong          m_srcH;
	DB::BindableNetworkId     m_modifiedBy;

	SaveCalendarSettingsQuery(SaveCalendarSettingsQuery const &);
	SaveCalendarSettingsQuery & operator=(SaveCalendarSettingsQuery const &);
};

// ======================================================================

class LoadCalendarSettingsQuery : public DB::Query
{
public:
	LoadCalendarSettingsQuery();

	virtual void getSQL(std::string &sql);
	virtual bool bindParameters();
	virtual bool bindColumns();

	void getData(
		std::string & bgTexture,
		long & srcX, long & srcY, long & srcW, long & srcH
	) const;

protected:
	virtual QueryMode getExecutionMode() const;

private:
	DB::BindableString<256>   m_bgTexture;
	DB::BindableLong          m_srcX;
	DB::BindableLong          m_srcY;
	DB::BindableLong          m_srcW;
	DB::BindableLong          m_srcH;

	LoadCalendarSettingsQuery(LoadCalendarSettingsQuery const &);
	LoadCalendarSettingsQuery & operator=(LoadCalendarSettingsQuery const &);
};

// ======================================================================

} // namespace DBQuery

// ======================================================================

#endif // INCLUDED_CalendarEventQueries_H

