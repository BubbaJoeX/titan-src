// ======================================================================
//
// ConsoleCommandParserCraft.cpp
// copyright (c) 2001 Sony Online Entertainment
//
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/ConsoleCommandParserCraft.h"
#include "UnicodeUtils.h"
#include "serverGame/ConsoleCommandParserCraftStation.h"
#include "serverGame/ContainerInterface.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/DraftSchematicObject.h"
#include "serverGame/FactoryObject.h"
#include "serverGame/GameServer.h"
#include "serverGame/ManufactureSchematicObject.h"
#include "serverGame/PlayerCreatureController.h"
#include "serverGame/PlayerObject.h"
#include "serverGame/ServerFactoryObjectTemplate.h"
#include "serverGame/ServerWorld.h"
#include "serverScript/GameScriptObject.h"
#include "serverScript/ScriptParameters.h"
#include "sharedFoundation/FormattedString.h"
#include "sharedFoundation/GameControllerMessage.h"
#include "sharedObject/Container.h"


// ======================================================================

static const CommandParser::CmdInfo cmds[] =
{
	{"draft",                  1, "<number>",                          "Select a draft schematic to craft with"},
	{"fillslot",               3, "<slot #> <slot option #> <ingredient oid>", "Fill a draft schematic slot with an ingredient"},
	{"nextstage",              0, "",                                  "Go to the next stage of crafting"},
	{"experiment",             3, "<attribute #> <experiment points> <core level>", "Experiment to improve an attribute"},
	{"customize",              2, "<custom #> <value>",                "Set a customization property"},
	{"makeprototype",          1, "<name>",                            "Create a prototype object"},
	{"makeschematic",          1, "<name>",                            "Create manufacturing schematic"},
	{"stop",                   0, "",                                  "Stops the current crafting session"},
	{"enableSchematicFilter",  0, "",                                  "Enables schematic filtering (god mode only)"},
	{"disableSchematicFilter", 0, "",                                  "Disables schematic filtering (god mode only)"},
	{"generateFactoryCrate",   2, "<draft schematic path> <quality>",  "Generate a factory crate from a draft schematic (god mode only)"},
    {"", 0, "", ""} // this must be last
};


//-----------------------------------------------------------------

ConsoleCommandParserCraft::ConsoleCommandParserCraft (void) :
CommandParser ("craft", 0, "...", "Crafting related commands.", 0)
{
    createDelegateCommands (cmds);
	addSubCommand(new ConsoleCommandParserCraftStation());
}

//-----------------------------------------------------------------


bool ConsoleCommandParserCraft::performParsing (const NetworkId & userId, const StringVector_t & argv, const String_t & originalCommand, String_t & result, const CommandParser * node)
{
    NOT_NULL (node);
    UNREF(originalCommand);

	CreatureObject * creatureObject = safe_cast<CreatureObject *>(ServerWorld::findObjectByNetworkId(userId));
	PlayerObject * playerObject = const_cast<PlayerObject *>(PlayerCreatureController::getPlayerObject(creatureObject));

    if (!playerObject)
    {
        WARNING_STRICT_FATAL(true, ("Console command executed on invalid player object %s", userId.getValueString().c_str()));
        return false;
    }

    if (!playerObject->getClient()->isGod()) {
        return false;
    }

	//-----------------------------------------------------------------

    if (isAbbrev( argv [0], "draft"))
    {
		int index = atoi(Unicode::wideToNarrow(argv[1]).c_str()) - 1;

		playerObject->selectDraftSchematic(index);
        result += getErrorMessage (argv[0], ERR_SUCCESS);
    }

	//-----------------------------------------------------------------

    else if (isAbbrev( argv [0], "fillslot"))
    {
		int slot(atoi(Unicode::wideToNarrow(argv[1]).c_str()));
		int option(atoi(Unicode::wideToNarrow(argv[2]).c_str())); 
		NetworkId ingredient(Unicode::wideToNarrow(argv[3]));

		if (playerObject->fillSlot(slot - 1, option - 1, ingredient))
			result += getErrorMessage (argv[0], ERR_SUCCESS);
		else
			result += getErrorMessage (argv[0], ERR_FILLSLOT_FAIL);
    }

	//-----------------------------------------------------------------

	else if (isAbbrev( argv [0], "nextstage"))
	{
		if (playerObject->goToNextCraftingStage())
			result += getErrorMessage (argv[0], ERR_SUCCESS);
		else
			result += getErrorMessage (argv[0], ERR_FAIL);
	}

	//-----------------------------------------------------------------

    else if (isAbbrev( argv [0], "experiment"))
    {
		int attribute(atoi(Unicode::wideToNarrow(argv[1]).c_str()));
		int points(atoi(Unicode::wideToNarrow(argv[2]).c_str()));
		int core(atoi(Unicode::wideToNarrow(argv[3]).c_str()));

		std::vector<MessageQueueCraftExperiment::ExperimentInfo> experiment;
		experiment.push_back(MessageQueueCraftExperiment::ExperimentInfo(attribute - 1, points));
		if (playerObject->experiment(experiment, points, core))
			result += getErrorMessage (argv[0], ERR_SUCCESS);
		else
			result += getErrorMessage (argv[0], ERR_FILLSLOT_FAIL);
    }

	//-----------------------------------------------------------------

	else if (isAbbrev( argv [0], "customize"))
	{
		int property(atoi(Unicode::wideToNarrow(argv[1]).c_str()));
		int value(atoi(Unicode::wideToNarrow(argv[2]).c_str()));

		if (playerObject->customize(property - 1, value))
			result += getErrorMessage (argv[0], ERR_SUCCESS);
		else
			result += getErrorMessage (argv[0], ERR_CUSTOMIZE_FAIL);
	}

	//-----------------------------------------------------------------

    else if (isAbbrev( argv [0], "makeprototype"))
    {
		std::vector<Crafting::CustomValue> customizations;
		if (playerObject->setCustomizationData(argv[1], -1, customizations, 1) == 
			Crafting::CE_success && playerObject->createPrototype(true))
		{
			result += getErrorMessage (argv[0], ERR_SUCCESS);
		}
		else
			result += getErrorMessage (argv[0], ERR_FAIL);
    }

	//-----------------------------------------------------------------

    else if (isAbbrev( argv [0], "makeschematic"))
    {
		result += getErrorMessage (argv[0], ERR_FAIL);
    }

	//-----------------------------------------------------------------

    else if (isAbbrev( argv [0], "stop"))
    {
		playerObject->stopCrafting(true);
		result += getErrorMessage (argv[0], ERR_SUCCESS);
    }

	//-----------------------------------------------------------------

	else if (isAbbrev( argv [0], "enableSchematicFilter"))
	{
		if (creatureObject->getClient() == nullptr || !creatureObject->getClient()->isGod())
			result += getErrorMessage (argv[0], ERR_FAIL);
		else
		{
			creatureObject->enableSchematicFiltering();
			result += getErrorMessage (argv[0], ERR_SUCCESS);
		}
	}

	//-----------------------------------------------------------------

	else if (isAbbrev( argv [0], "disableSchematicFilter"))
	{
		if (creatureObject->getClient() == nullptr || !creatureObject->getClient()->isGod())
			result += getErrorMessage (argv[0], ERR_FAIL);
		else
		{
			creatureObject->disableSchematicFiltering();
			result += getErrorMessage (argv[0], ERR_SUCCESS);
		}
	}

	//-----------------------------------------------------------------

	else if (isAbbrev( argv [0], "generateFactoryCrate"))
	{
		if (creatureObject->getClient() == nullptr || !creatureObject->getClient()->isGod())
		{
			result += getErrorMessage (argv[0], ERR_FAIL);
		}
		else
		{
			std::string draftSchematicPath = Unicode::wideToNarrow(argv[1]);
			float quality = static_cast<float>(atof(Unicode::wideToNarrow(argv[2]).c_str()));

			// Get the draft schematic
			const DraftSchematicObject * schematic = DraftSchematicObject::getSchematic(draftSchematicPath);
			if (schematic == nullptr)
			{
				result += Unicode::narrowToWide("Error: Invalid draft schematic path: ") + argv[1];
				result += getErrorMessage (argv[0], ERR_FAIL);
			}
			else
			{
				// Get player's inventory
				ServerObject * inventory = creatureObject->getInventory();
				if (inventory == nullptr)
				{
					result += Unicode::narrowToWide("Error: Cannot find player inventory");
					result += getErrorMessage (argv[0], ERR_FAIL);
				}
				else
				{
					// Clamp quality to 0-100 range
					if (quality < 0.0f)
						quality = 0.0f;
					else if (quality > 100.0f)
						quality = 100.0f;

					// Create position for temporary objects
					Vector createPos(creatureObject->getPosition_w());
					createPos.y = -100000.0f;

					// Create a manufacturing schematic
					ManufactureSchematicObject * manfSchematic = ServerWorld::createNewManufacturingSchematic(
						*schematic, createPos, false);
					if (manfSchematic == nullptr)
					{
						result += Unicode::narrowToWide("Error: Failed to create manufacturing schematic");
						result += getErrorMessage (argv[0], ERR_FAIL);
					}
					else
					{
						// Create a prototype item
						ServerObject * prototype = manfSchematic->manufactureObject(createPos);
						if (prototype == nullptr)
						{
							result += Unicode::narrowToWide("Error: Failed to create prototype object");
							manfSchematic->permanentlyDestroy(DeleteReasons::SetupFailed);
							result += getErrorMessage (argv[0], ERR_FAIL);
						}
						else
						{
							// Apply quality adjustments via script trigger
							ScriptParams params;
							params.addParam(prototype->getNetworkId());
							params.addParam(*manfSchematic);
							params.addParam(quality);
							IGNORE_RETURN(manfSchematic->getScriptObject()->trigAllScripts(
								Scripting::TRIG_MAKE_CRAFTED_ITEM, params));

							// Get the crate template
							const ServerFactoryObjectTemplate * crateTemplate = schematic->getCrateObjectTemplate();
							if (crateTemplate == nullptr)
							{
								result += Unicode::narrowToWide("Error: Draft schematic has no crate template");
								prototype->permanentlyDestroy(DeleteReasons::SetupFailed);
								manfSchematic->permanentlyDestroy(DeleteReasons::Consumed);
								result += getErrorMessage (argv[0], ERR_FAIL);
							}
							else
							{
								// Create the factory crate
								FactoryObject * factoryCrate = safe_cast<FactoryObject *>(ServerWorld::createNewObject(
									*crateTemplate, createPos, false));
								if (factoryCrate == nullptr)
								{
									result += Unicode::narrowToWide("Error: Failed to create factory crate");
									prototype->permanentlyDestroy(DeleteReasons::SetupFailed);
									manfSchematic->permanentlyDestroy(DeleteReasons::Consumed);
									result += getErrorMessage (argv[0], ERR_FAIL);
								}
								else
								{
									// Initialize the factory crate with data from the manf schematic
									factoryCrate->initialize(*manfSchematic);

									// Put the prototype into the factory crate
									Container::ContainerErrorCode error;
									if (!ContainerInterface::transferItemToVolumeContainer(*factoryCrate, *prototype,
										nullptr, error, true))
									{
										result += Unicode::narrowToWide("Error: Failed to add prototype to crate");
										factoryCrate->permanentlyDestroy(DeleteReasons::BadContainerTransfer);
										prototype->permanentlyDestroy(DeleteReasons::BadContainerTransfer);
										manfSchematic->permanentlyDestroy(DeleteReasons::Consumed);
										result += getErrorMessage (argv[0], ERR_FAIL);
									}
									else
									{
										// Set initial count to 1
										factoryCrate->setCount(1);

										// Cleanup the manf schematic
										manfSchematic->permanentlyDestroy(DeleteReasons::Consumed);

										// Move the factory crate to the player's inventory
										if (!ContainerInterface::transferItemToVolumeContainer(*inventory, *factoryCrate,
											nullptr, error, true))
										{
											result += Unicode::narrowToWide("Error: Failed to add crate to inventory");
											factoryCrate->permanentlyDestroy(DeleteReasons::BadContainerTransfer);
											result += getErrorMessage (argv[0], ERR_FAIL);
										}
										else
										{
											result += Unicode::narrowToWide("Successfully generated factory crate with quality ") + 
												Unicode::narrowToWide(FormattedString<32>().sprintf("%.2f", quality));
											result += getErrorMessage (argv[0], ERR_SUCCESS);
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	//-----------------------------------------------------------------


    else
    {
        result += getErrorMessage(argv[0], ERR_NO_HANDLER);
    }

    return true;
}


// ======================================================================





