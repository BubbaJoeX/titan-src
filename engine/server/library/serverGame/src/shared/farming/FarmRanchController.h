// ======================================================================
//
// FarmRanchController.h
//
// Farming / ranching: inventory seeds, house-scoped garden beds, plant growth
// scale, optional Phase D datatable modifiers, ranch breeding hook.
//
// ======================================================================

#ifndef INCLUDED_FarmRanchController_H
#define INCLUDED_FarmRanchController_H

#include "sharedFoundation/NetworkId.h"
#include "Unicode.h"
#include "UnicodeUtils.h"

class Command;

namespace FarmRanchController
{
	void commandFarmPlant(Command const & c, NetworkId const & actor, NetworkId const & target, Unicode::String const & params);
	void commandFarmHarvest(Command const & c, NetworkId const & actor, NetworkId const & target, Unicode::String const & params);
	void commandRanchBreed(Command const & c, NetworkId const & actor, NetworkId const & target, Unicode::String const & params);
}

// ======================================================================

#endif
