// ======================================================================
//
// FarmRanchController.cpp
//
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/FarmRanchController.h"

#include "serverGame/Chat.h"
#include "serverGame/ContainerInterface.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/ObjectIdManager.h"
#include "serverGame/ServerObject.h"
#include "serverGame/ServerWorld.h"
#include "serverGame/TangibleObject.h"

#include "serverUtility/ServerClock.h"

#include "sharedFoundation/Crc.h"
#include "sharedFoundation/Unicode.h"
#include "sharedLog/Log.h"
#include "sharedMath/Transform.h"
#include "sharedMath/Vector.h"
#include "sharedObject/ContainedByProperty.h"
#include "sharedObject/NetworkIdManager.h"
#include "sharedObject/ObjectTemplateList.h"
#include "sharedObject/CellProperty.h"
#include "sharedUtility/DataTable.h"
#include "sharedUtility/DataTableManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

namespace FarmRanchControllerNamespace
{
	static char const * const cs_cropTable = "datatables/farming/crop_definitions.iff";
	static char const * const cs_ranchRulesTable = "datatables/farming/ranch_rules.iff";
	static char const * const cs_economyTable = "datatables/farming/economy_constants.iff";
	static char const * const cs_breedingTable = "datatables/farming/ranch_breeding.iff";
	static char const * const cs_soilWeatherTable = "datatables/farming/soil_weather.iff";

	static NetworkId parseFirstOid(Unicode::String const & params)
	{
		std::string const narrow = Unicode::wideToNarrow(params);
		std::istringstream iss(narrow);
		std::string token;
		iss >> token;
		while (!token.empty() && std::isspace(static_cast<unsigned char>(token[0])))
			token.erase(token.begin());
		if (token.empty())
			return NetworkId::cms_invalid;
		return NetworkId(token);
	}

	static bool templateNameContainsBedSubstring(std::string const & templateName, DataTable const * rules)
	{
		if (!rules || rules->getNumRows() <= 0)
			return false;
		int const col = rules->findColumnNumber("bed_template_substring");
		if (col < 0)
			return false;
		std::string const sub = rules->getStringValue(col, 0);
		if (sub.empty())
			return true;
		return templateName.find(sub) != std::string::npos;
	}

	static bool isRanchBedObject(TangibleObject const * t)
	{
		if (!t)
			return false;
		std::string bedFlag;
		if (t->getObjVars().getItem("ranch.bed", bedFlag))
		{
			if (!bedFlag.empty() && bedFlag != "0")
				return true;
		}
		DataTable const * const rules = DataTableManager::getTable(cs_ranchRulesTable, true);
		std::string const tn = ObjectTemplateList::lookUp(t->getTemplateCrc()).getString();
		return templateNameContainsBedSubstring(tn, rules);
	}

	static bool objectIsInInventory(ServerObject const * item, CreatureObject const * player)
	{
		if (!item || !player)
			return false;
		ServerObject const * const inv = player->getInventory();
		if (!inv)
			return false;
		ContainedByProperty const * const cb = item->getContainedByProperty();
		if (!cb)
			return false;
		return cb->getContainedByNetworkId() == inv->getNetworkId();
	}

	static std::string getCreatureTemplateName(ServerObject const * o)
	{
		if (!o)
			return std::string();
		return ObjectTemplateList::lookUp(o->getTemplateCrc()).getString();
	}

	static float phaseDGrowthMultiplier(char const * planetRow)
	{
		DataTable const * const sw = DataTableManager::getTable(cs_soilWeatherTable, true);
		if (!sw || sw->getNumRows() <= 0)
			return 1.f;
		int const planetCol = sw->findColumnNumber("planet");
		int const droughtCol = sw->findColumnNumber("drought_modifier");
		int const rainCol = sw->findColumnNumber("rain_bonus");
		if (planetCol < 0 || droughtCol < 0 || rainCol < 0)
			return 1.f;
		int row = sw->searchColumnString(planetCol, planetRow ? planetRow : "default");
		if (row < 0)
			row = sw->searchColumnString(planetCol, "default");
		if (row < 0)
			row = 0;
		float const drought = sw->getFloatValue(droughtCol, row);
		float const rain = sw->getFloatValue(rainCol, row);
		// Stub weather blend (0.5 neutral); scales effective grow time denominator.
		float const weatherBlend = 0.5f;
		float const factor = 1.f + rain * weatherBlend - drought * weatherBlend;
		return std::max(0.25f, std::min(2.f, factor));
	}

	static float moistureBonusForCrop(DataTable const * crop, int row)
	{
		if (!crop || row < 0)
			return 0.f;
		int const col = crop->findColumnNumber("moisture_bonus");
		if (col < 0)
			return 0.f;
		return crop->getFloatValue(col, row);
	}

	static real computeGrowthScale(int64_t plantedSeconds, int growSeconds, float scaleMin, float scaleMax, float phaseDMult)
	{
		if (growSeconds <= 0)
			return static_cast<real>(scaleMax);
		double const elapsed = static_cast<double>(ServerClock::getInstance().getGameTimeSeconds() - plantedSeconds);
		double const dur = static_cast<double>(growSeconds) / static_cast<double>(phaseDMult > 0.f ? phaseDMult : 1.f);
		double t = elapsed / dur;
		if (t < 0.0)
			t = 0.0;
		if (t > 1.0)
			t = 1.0;
		float const s = scaleMin + static_cast<float>(t) * (scaleMax - scaleMin);
		return static_cast<real>(s);
	}

	static bool findCropRowForSeed(std::string const & seedTemplatePath, int & rowOut, DataTable const * crop)
	{
		if (!crop)
			return false;
		int const seedCol = crop->findColumnNumber("seed_template");
		if (seedCol < 0)
			return false;
		int const r = crop->searchColumnString(seedCol, seedTemplatePath);
		if (r < 0)
			return false;
		rowOut = r;
		return true;
	}

	static bool findCropRowForCropId(std::string const & cropId, int & rowOut, DataTable const * crop)
	{
		if (!crop)
			return false;
		int const idCol = crop->findColumnNumber("crop_id");
		if (idCol < 0)
			return false;
		int const r = crop->searchColumnString(idCol, cropId);
		if (r < 0)
			return false;
		rowOut = r;
		return true;
	}

	static int matchBreedingRuleRow(CreatureObject const * a, CreatureObject const * b)
	{
		DataTable const * const bt = DataTableManager::getTable(cs_breedingTable, true);
		if (!bt || bt->getNumRows() <= 0)
			return -1;
		int const ka = bt->findColumnNumber("parent_a_contains");
		int const kb = bt->findColumnNumber("parent_b_contains");
		std::string const na = getCreatureTemplateName(a);
		std::string const nb = getCreatureTemplateName(b);
		for (int i = 0; i < bt->getNumRows(); ++i)
		{
			std::string const sa = ka >= 0 ? bt->getStringValue(ka, i) : std::string();
			std::string const sb = kb >= 0 ? bt->getStringValue(kb, i) : std::string();
			bool okA = sa.empty() || na.find(sa) != std::string::npos;
			bool okB = sb.empty() || nb.find(sb) != std::string::npos;
			if (okA && okB)
				return i;
		}
		return -1;
	}
}

using namespace FarmRanchControllerNamespace;

void FarmRanchController::commandFarmPlant(Command const &, NetworkId const & actor, NetworkId const & target, Unicode::String const & params)
{
	CreatureObject * const player = dynamic_cast<CreatureObject *>(NetworkIdManager::getObjectById(actor));
	TangibleObject * const bed = dynamic_cast<TangibleObject *>(NetworkIdManager::getObjectById(target));
	NetworkId const seedId = parseFirstOid(params);
	ServerObject * const seedObj = NetworkIdManager::getObjectById(seedId);

	if (!player || !bed || !seedObj || seedId == NetworkId::cms_invalid)
	{
		if (player)
			Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: invalid bed or seed."), Unicode::String());
		return;
	}

	if (!isRanchBedObject(bed))
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: target is not a ranch garden bed."), Unicode::String());
		return;
	}

	if (!objectIsInInventory(seedObj, player))
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: seed must be in your inventory."), Unicode::String());
		return;
	}

	DataTable const * const crop = DataTableManager::getTable(cs_cropTable, true);
	if (!crop)
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: crop datatable missing."), Unicode::String());
		return;
	}

	std::string seedTemplatePath = ObjectTemplateList::lookUp(seedObj->getTemplateCrc()).getString();
	int cropRow = -1;
	if (!findCropRowForSeed(seedTemplatePath, cropRow, crop))
	{
		std::string cropId;
		if (seedObj->getObjVars().getItem("farm.cropId", cropId) && findCropRowForCropId(cropId, cropRow, crop))
		{
			seedTemplatePath = crop->getStringValue(crop->findColumnNumber("seed_template"), cropRow);
		}
		else
		{
			Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: unknown seed template."), Unicode::String());
			return;
		}
	}

	int const growCol = crop->findColumnNumber("grow_seconds");
	int const plantCol = crop->findColumnNumber("plant_template");
	int const sminCol = crop->findColumnNumber("scale_min");
	int const smaxCol = crop->findColumnNumber("scale_max");
	if (growCol < 0 || plantCol < 0 || sminCol < 0 || smaxCol < 0)
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: crop row incomplete."), Unicode::String());
		return;
	}

	int const growSeconds = crop->getIntValue(growCol, cropRow);
	std::string const plantTemplate = crop->getStringValue(plantCol, cropRow);
	float const scaleMin = crop->getFloatValue(sminCol, cropRow);
	float const scaleMax = crop->getFloatValue(smaxCol, cropRow);

	CellProperty * const cellProp = bed->getParentCell();
	if (!cellProp)
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: bed has no cell parent (place indoors / on structure)."), Unicode::String());
		return;
	}
	Object & cellOwnerObj = cellProp->getOwner();
	ServerObject * const cellObject = cellOwnerObj.asServerObject();
	if (!cellObject)
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: invalid cell."), Unicode::String());
		return;
	}

	TangibleObject * const plant = dynamic_cast<TangibleObject *>(ServerWorld::createObjectFromTemplate(plantTemplate, NetworkId::cms_invalid));
	if (!plant)
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: could not create plant (missing template?)."), Unicode::String());
		return;
	}

	float const phaseD = phaseDGrowthMultiplier("default") + moistureBonusForCrop(crop, cropRow) * 0.01f;
	int64_t const planted = static_cast<int64_t>(ServerClock::getInstance().getGameTimeSeconds());
	std::string const cropIdStr = crop->getStringValue(crop->findColumnNumber("crop_id"), cropRow);
	IGNORE_RETURN(plant->setObjVarItem("farm.cropId", cropIdStr));
	IGNORE_RETURN(plant->setObjVarItem("farm.plantedAt", static_cast<int>(planted)));

	real const s = computeGrowthScale(planted, growSeconds, scaleMin, scaleMax, phaseD);
	plant->setScale(Vector(s, s, s));

	Transform tr(bed->getTransform_o2w());
	Vector p = tr.getPosition_p();
	p.y += static_cast<real>(0.05);
	tr.setPosition_p(p);
	plant->setTransform_o2w(tr);

	Container::ContainerErrorCode cerr = Container::CEC_Success;
	if (!ContainerInterface::transferItemToCell(*cellObject, *plant, tr, player, cerr))
	{
		IGNORE_RETURN(plant->permanentlyDestroy(DeleteReasons::SetupFailed));
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: could not place plant in cell."), Unicode::String());
		return;
	}

	IGNORE_RETURN(seedObj->permanentlyDestroy(DeleteReasons::Consumed));
	Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: planted."), Unicode::String());
}

void FarmRanchController::commandFarmHarvest(Command const &, NetworkId const & actor, NetworkId const & target, Unicode::String const &)
{
	CreatureObject * const player = dynamic_cast<CreatureObject *>(NetworkIdManager::getObjectById(actor));
	TangibleObject * const plant = dynamic_cast<TangibleObject *>(NetworkIdManager::getObjectById(target));

	if (!player || !plant)
		return;

	std::string cropId;
	if (!plant->getObjVars().getItem("farm.cropId", cropId) || cropId.empty())
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: not a farm plant."), Unicode::String());
		return;
	}

	DataTable const * const crop = DataTableManager::getTable(cs_cropTable, true);
	if (!crop)
		return;

	int cropRow = -1;
	if (!findCropRowForCropId(cropId, cropRow, crop))
		return;

	int planted = 0;
	if (!plant->getObjVars().getItem("farm.plantedAt", planted))
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: plant has no growth state."), Unicode::String());
		return;
	}

	int const growCol = crop->findColumnNumber("grow_seconds");
	int const harvestCol = crop->findColumnNumber("harvest_template");
	int const sminCol = crop->findColumnNumber("scale_min");
	int const smaxCol = crop->findColumnNumber("scale_max");
	if (growCol < 0 || harvestCol < 0 || sminCol < 0 || smaxCol < 0)
		return;

	int growSeconds = crop->getIntValue(growCol, cropRow);
	std::string const harvestTemplate = crop->getStringValue(harvestCol, cropRow);
	float const scaleMin = crop->getFloatValue(sminCol, cropRow);
	float const scaleMax = crop->getFloatValue(smaxCol, cropRow);

	float const phaseD = phaseDGrowthMultiplier("default") + moistureBonusForCrop(crop, cropRow) * 0.01f;
	real const scaleNow = computeGrowthScale(static_cast<int64_t>(planted), growSeconds, scaleMin, scaleMax, phaseD);
	plant->setScale(Vector(scaleNow, scaleNow, scaleNow));

	if (scaleNow + static_cast<real>(0.001) < static_cast<real>(scaleMax))
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: crop not ready."), Unicode::String());
		return;
	}

	DataTable const * const econ = DataTableManager::getTable(cs_economyTable, true);
	int yieldPct = 100;
	if (econ)
	{
		int const keyCol = econ->findColumnNumber("key");
		int const rowIncome = econ->searchColumnString(keyCol, "income_mode_yield_multiplier_percent");
		if (rowIncome >= 0)
			yieldPct = econ->getIntValue(econ->findColumnNumber("value_int"), rowIncome);
	}
	UNREF(yieldPct);

	ServerObject * const reward = ServerWorld::createObjectFromTemplate(harvestTemplate, NetworkId::cms_invalid);
	if (!reward)
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: harvest template failed."), Unicode::String());
		return;
	}

	ServerObject * const inv = player->getInventory();
	Container::ContainerErrorCode cerr = Container::CEC_Success;
	if (!inv || !ContainerInterface::transferItemToVolumeContainer(*inv, *reward, player, cerr, false))
	{
		IGNORE_RETURN(reward->permanentlyDestroy(DeleteReasons::SetupFailed));
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: inventory full."), Unicode::String());
		return;
	}

	IGNORE_RETURN(plant->permanentlyDestroy(DeleteReasons::Consumed));
	Chat::sendSystemMessage(*player, Unicode::narrowToWide("Farm: harvested."), Unicode::String());
}

void FarmRanchController::commandRanchBreed(Command const &, NetworkId const & actor, NetworkId const & target, Unicode::String const & params)
{
	CreatureObject * const player = dynamic_cast<CreatureObject *>(NetworkIdManager::getObjectById(actor));
	CreatureObject * const parentA = dynamic_cast<CreatureObject *>(NetworkIdManager::getObjectById(target));
	NetworkId const parentBId = parseFirstOid(params);
	CreatureObject * const parentB = dynamic_cast<CreatureObject *>(NetworkIdManager::getObjectById(parentBId));

	if (!player || !parentA || !parentB || parentBId == NetworkId::cms_invalid)
	{
		if (player)
			Chat::sendSystemMessage(*player, Unicode::narrowToWide("Ranch: select two creatures."), Unicode::String());
		return;
	}

	DataTable const * const rules = DataTableManager::getTable(cs_ranchRulesTable, true);
	float maxDist = 64.f;
	if (rules && rules->getNumRows() > 0)
	{
		int const dcol = rules->findColumnNumber("max_breed_distance");
		if (dcol >= 0)
			maxDist = rules->getFloatValue(dcol, 0);
	}

	Vector const pw = player->getPosition_w();
	if (parentA->getPosition_w().magnitudeBetween(pw) > static_cast<real>(maxDist) || parentB->getPosition_w().magnitudeBetween(pw) > static_cast<real>(maxDist))
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Ranch: bring both animals near you (ranch cell)."), Unicode::String());
		return;
	}

	if (parentA->getPosition_w().magnitudeBetween(parentB->getPosition_w()) > static_cast<real>(maxDist * 2.f))
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Ranch: animals too far apart."), Unicode::String());
		return;
	}

	int const breedRow = matchBreedingRuleRow(parentA, parentB);
	if (breedRow < 0)
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Ranch: no breeding rule for these templates."), Unicode::String());
		return;
	}

	DataTable const * const bt = DataTableManager::getTable(cs_breedingTable, true);
	if (!bt)
		return;

	std::string const offspringTemplate = bt->getStringValue(bt->findColumnNumber("offspring_template"), breedRow);
	int const cd = bt->getIntValue(bt->findColumnNumber("cooldown_seconds"), breedRow);

	int lastA = 0;
	int lastB = 0;
	int const now = static_cast<int>(ServerClock::getInstance().getGameTimeSeconds());
	parentA->getObjVars().getItem("ranch.lastBreedAt", lastA);
	parentB->getObjVars().getItem("ranch.lastBreedAt", lastB);
	if ((now - lastA) < cd || (now - lastB) < cd)
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Ranch: breeding cooldown."), Unicode::String());
		return;
	}

	ServerObject * const offspring = ServerWorld::createObjectFromTemplate(offspringTemplate, NetworkId::cms_invalid);
	if (!offspring)
	{
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Ranch: offspring template failed."), Unicode::String());
		return;
	}

	ServerObject * const inv = player->getInventory();
	Container::ContainerErrorCode cerr = Container::CEC_Success;
	if (!inv || !ContainerInterface::transferItemToVolumeContainer(*inv, *offspring, player, cerr, false))
	{
		IGNORE_RETURN(offspring->permanentlyDestroy(DeleteReasons::SetupFailed));
		Chat::sendSystemMessage(*player, Unicode::narrowToWide("Ranch: inventory full."), Unicode::String());
		return;
	}

	IGNORE_RETURN(parentA->setObjVarItem("ranch.lastBreedAt", now));
	IGNORE_RETURN(parentB->setObjVarItem("ranch.lastBreedAt", now));

	Chat::sendSystemMessage(*player, Unicode::narrowToWide("Ranch: offspring produced (placeholder pipeline)."), Unicode::String());
}
