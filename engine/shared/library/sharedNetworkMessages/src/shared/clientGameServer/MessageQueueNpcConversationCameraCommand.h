//========================================================================
//
// MessageQueueNpcConversationCameraCommand.h
//
// Sends camera control commands to the client during NPC conversations.
// Allows server scripts to control where the cinematic camera looks.
//
//========================================================================

#ifndef INCLUDED_MessageQueueNpcConversationCameraCommand_H
#define INCLUDED_MessageQueueNpcConversationCameraCommand_H

#include "sharedFoundation/MessageQueue.h"
#include "sharedFoundation/NetworkId.h"
#include "sharedNetworkMessages/ControllerMessageMacros.h"

class MemoryBlockManager;

class MessageQueueNpcConversationCameraCommand : public MessageQueue::Data
{
	CONTROLLER_MESSAGE_INTERFACE;

public:
	// Camera command types
	enum CommandType
	{
		CT_None,
		CT_LookAtTarget,     // Look at a specific object
		CT_LookAtPosition,   // Look at world coordinates
		CT_ReturnToSpeaker   // Return camera to current NPC speaker
	};

public:
	MessageQueueNpcConversationCameraCommand();
	virtual ~MessageQueueNpcConversationCameraCommand();

	// Setters
	void setCommandType(CommandType type);
	void setTargetId(NetworkId const & targetId);
	void setPosition(float x, float y, float z);
	void setHoldTime(float holdTime);
	void setTransitionDuration(float duration);

	// Getters
	CommandType getCommandType() const;
	NetworkId const & getTargetId() const;
	float getPositionX() const;
	float getPositionY() const;
	float getPositionZ() const;
	float getHoldTime() const;
	float getTransitionDuration() const;

private:
	CommandType m_commandType;
	NetworkId m_targetId;
	float m_posX;
	float m_posY;
	float m_posZ;
	float m_holdTime;
	float m_transitionDuration;
};

//----------------------------------------------------------------------

inline MessageQueueNpcConversationCameraCommand::MessageQueueNpcConversationCameraCommand() :
	m_commandType(CT_None),
	m_targetId(NetworkId::cms_invalid),
	m_posX(0.0f),
	m_posY(0.0f),
	m_posZ(0.0f),
	m_holdTime(0.0f),
	m_transitionDuration(1.2f)
{
}

inline void MessageQueueNpcConversationCameraCommand::setCommandType(CommandType type)
{
	m_commandType = type;
}

inline void MessageQueueNpcConversationCameraCommand::setTargetId(NetworkId const & targetId)
{
	m_targetId = targetId;
}

inline void MessageQueueNpcConversationCameraCommand::setPosition(float x, float y, float z)
{
	m_posX = x;
	m_posY = y;
	m_posZ = z;
}

inline void MessageQueueNpcConversationCameraCommand::setHoldTime(float holdTime)
{
	m_holdTime = holdTime;
}

inline void MessageQueueNpcConversationCameraCommand::setTransitionDuration(float duration)
{
	m_transitionDuration = duration;
}

inline MessageQueueNpcConversationCameraCommand::CommandType MessageQueueNpcConversationCameraCommand::getCommandType() const
{
	return m_commandType;
}

inline NetworkId const & MessageQueueNpcConversationCameraCommand::getTargetId() const
{
	return m_targetId;
}

inline float MessageQueueNpcConversationCameraCommand::getPositionX() const
{
	return m_posX;
}

inline float MessageQueueNpcConversationCameraCommand::getPositionY() const
{
	return m_posY;
}

inline float MessageQueueNpcConversationCameraCommand::getPositionZ() const
{
	return m_posZ;
}

inline float MessageQueueNpcConversationCameraCommand::getHoldTime() const
{
	return m_holdTime;
}

inline float MessageQueueNpcConversationCameraCommand::getTransitionDuration() const
{
	return m_transitionDuration;
}

#endif // INCLUDED_MessageQueueNpcConversationCameraCommand_H

