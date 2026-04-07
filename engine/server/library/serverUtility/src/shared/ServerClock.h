// ServerClock.h
// copyright 2001 Verant Interactive
// Author: Justin Randall

//-----------------------------------------------------------------------

class ServerClock
{
public:
	static ServerClock &getInstance();
	
	~ServerClock();
	const unsigned long getGameTimeSeconds () const;
	const unsigned long getServerFrame() const;
	const unsigned long getSubtractInterval() const;
	void                incrementServerFrame();
	void                setSubtractInterval(const unsigned long newSubtractInterval);
	void                setGameTimeSeconds (unsigned long newGameTime);
	bool                isSet              () const;
	std::string         getDebugPrintableTimeframe (const unsigned long timeInSeconds);

	/** Bias added to getGameTimeSeconds() for client sky / environment sync only (cooldowns still use raw game time). */
	long long           getEnvironmentTimeBiasSeconds () const;
	void                adjustEnvironmentTimeBiasSeconds (long long delta);
	long long           getEffectiveEnvironmentTimeSeconds () const;

   static const unsigned long cms_endOfTime;
   
protected:
	ServerClock();

private:
	unsigned long serverFrame;
	unsigned long subtractInterval;
	mutable time_t lastTime;
	long long       m_environmentTimeBiasSeconds;
};

//-----------------------------------------------------------------------

inline const unsigned long ServerClock::getServerFrame() const
{
	return serverFrame;
}

//-----------------------------------------------------------------------

inline const unsigned long ServerClock::getSubtractInterval() const
{
	return subtractInterval;
}

// ----------------------------------------------------------------------

inline bool ServerClock::isSet() const
{
	return subtractInterval!=0;
}

//-----------------------------------------------------------------------

inline long long ServerClock::getEnvironmentTimeBiasSeconds () const
{
	return m_environmentTimeBiasSeconds;
}

//-----------------------------------------------------------------------

inline void ServerClock::adjustEnvironmentTimeBiasSeconds (long long const delta)
{
	m_environmentTimeBiasSeconds += delta;
}

//-----------------------------------------------------------------------

inline long long ServerClock::getEffectiveEnvironmentTimeSeconds () const
{
	return static_cast<long long>(getGameTimeSeconds()) + m_environmentTimeBiasSeconds;
}

//-----------------------------------------------------------------------
