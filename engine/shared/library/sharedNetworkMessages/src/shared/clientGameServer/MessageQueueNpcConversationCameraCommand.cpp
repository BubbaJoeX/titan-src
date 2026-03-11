//========================================================================
//
// MessageQueueNpcConversationCameraCommand.cpp
//
// Sends camera control commands to the client during NPC conversations.
//
//========================================================================

#include "sharedNetworkMessages/FirstSharedNetworkMessages.h"
#include "sharedNetworkMessages/MessageQueueNpcConversationCameraCommand.h"
#include "sharedFoundation/MemoryBlockManager.h"
#include "sharedFoundation/ExitChain.h"
#include "sharedFoundation/GameControllerMessage.h"
#include "sharedNetworkMessages/ControllerMessageFactory.h"

//===================================================================

CONTROLLER_MESSAGE_IMPLEMENTATION(MessageQueueNpcConversationCameraCommand, CM_npcConversationCameraCommand);

//===================================================================

MessageQueueNpcConversationCameraCommand::~MessageQueueNpcConversationCameraCommand()
{
}

//-----------------------------------------------------------------------

void MessageQueueNpcConversationCameraCommand::pack(const MessageQueue::Data* const data, Archive::ByteStream & target)
{
	const MessageQueueNpcConversationCameraCommand* const msg = safe_cast<const MessageQueueNpcConversationCameraCommand*>(data);

	if (msg)
	{
		Archive::put(target, static_cast<int8>(msg->getCommandType()));
		Archive::put(target, msg->getTargetId());
		Archive::put(target, msg->getPositionX());
		Archive::put(target, msg->getPositionY());
		Archive::put(target, msg->getPositionZ());
		Archive::put(target, msg->getHoldTime());
		Archive::put(target, msg->getTransitionDuration());
	}
}

//-----------------------------------------------------------------------

MessageQueue::Data* MessageQueueNpcConversationCameraCommand::unpack(Archive::ReadIterator & source)
{
	MessageQueueNpcConversationCameraCommand * msg = new MessageQueueNpcConversationCameraCommand;

	int8 commandType;
	NetworkId targetId;
	float posX, posY, posZ;
	float holdTime;
	float transitionDuration;

	Archive::get(source, commandType);
	Archive::get(source, targetId);
	Archive::get(source, posX);
	Archive::get(source, posY);
	Archive::get(source, posZ);
	Archive::get(source, holdTime);
	Archive::get(source, transitionDuration);

	msg->setCommandType(static_cast<MessageQueueNpcConversationCameraCommand::CommandType>(commandType));
	msg->setTargetId(targetId);
	msg->setPosition(posX, posY, posZ);
	msg->setHoldTime(holdTime);
	msg->setTransitionDuration(transitionDuration);

	return msg;
}

//-----------------------------------------------------------------------

