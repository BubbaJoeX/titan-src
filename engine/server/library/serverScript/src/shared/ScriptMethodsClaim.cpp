//========================================================================
//
// ScriptMethodsClaim.cpp - JNI for open-world claim system
//
//========================================================================

#include "serverScript/FirstServerScript.h"
#include "serverScript/JavaLibrary.h"

#include "serverGame/ClaimManager.h"
#include "serverGame/ConfigServerGame.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/PlayerCreatureController.h"
#include "serverGame/PlayerObject.h"
#include "serverGame/ServerObject.h"
#include "serverGame/ServerWorld.h"
#include "sharedFoundation/StationId.h"
#include "sharedLog/Log.h"
#include "sharedObject/NetworkIdManager.h"

using namespace JNIWrappersNamespace;

namespace ScriptMethodsClaimNamespace
{
	bool install();

	jint JNICALL claimFinalizePlacement(JNIEnv *env, jobject self, jlong player, jlong marker, jfloat x, jfloat y, jfloat z, jfloat radius, jlong terminal);
	void JNICALL claimBindObject(JNIEnv *env, jobject self, jlong obj, jint claimId);
	void JNICALL claimUnbindObject(JNIEnv *env, jobject self, jlong obj);
	jint JNICALL claimApplyVisitorResourceTax(JNIEnv *env, jobject self, jlong player, jfloat x, jfloat y, jfloat z, jstring resourceKey, jint amount);
	jboolean JNICALL claimWithdrawTax(JNIEnv *env, jobject self, jlong player, jlong terminal, jstring resourceKey, jint amount);
	jboolean JNICALL claimPayMaintenance(JNIEnv *env, jobject self, jlong player, jlong terminal, jint credits);
	jboolean JNICALL claimAddBan(JNIEnv *env, jobject self, jlong player, jlong terminal, jlong banned);
	jboolean JNICALL claimRemoveBan(JNIEnv *env, jobject self, jlong player, jlong terminal, jlong banned);
	jint JNICALL claimGetTaxBalance(JNIEnv *env, jobject self, jint claimId, jstring resourceKey);
	jboolean JNICALL claimCanManipulateFurniture(JNIEnv *env, jobject self, jlong player, jlong target);
	jboolean JNICALL claimValidateWorldPosition(JNIEnv *env, jobject self, jlong player, jlong target, jfloat x, jfloat y, jfloat z);
	jboolean JNICALL claimAddAllowed(JNIEnv *env, jobject self, jlong player, jlong terminal, jlong allowed);
	jboolean JNICALL claimRemoveAllowed(JNIEnv *env, jobject self, jlong player, jlong terminal, jlong allowed);
	jboolean JNICALL claimRemoveClaim(JNIEnv *env, jobject self, jlong admin, jint claimId, jboolean destroyWorldObjects);
	jint JNICALL claimPurgeClaimsForPlayer(JNIEnv *env, jobject self, jlong admin, jlong targetPlayer, jboolean destroyWorldObjects, jboolean allAccountClaims);
	jint JNICALL claimFindClaimIdByMarker(JNIEnv *env, jobject self, jlong marker);
	jboolean JNICALL claimCanManageTerminal(JNIEnv *env, jobject self, jlong player, jlong terminal);
	jint JNICALL claimGetMaintenancePrepay(JNIEnv *env, jobject self, jlong player, jlong terminal);
	jint JNICALL claimGetMaintenanceDueTime(JNIEnv *env, jobject self, jlong player, jlong terminal);
	jint JNICALL claimGetClaimStatus(JNIEnv *env, jobject self, jlong player, jlong terminal);
}

bool ScriptMethodsClaimNamespace::install()
{
	const JNINativeMethod NATIVES[] = {
#define JF(a, b, c) { a, b, (void *)(ScriptMethodsClaimNamespace::c) }
		JF("_claimFinalizePlacement", "(JJFFFFJ)I", claimFinalizePlacement),
		JF("_claimBindObject", "(JI)V", claimBindObject),
		JF("_claimUnbindObject", "(J)V", claimUnbindObject),
		JF("_claimApplyVisitorResourceTax", "(JFFFLjava/lang/String;I)I", claimApplyVisitorResourceTax),
		JF("_claimWithdrawTax", "(JJLjava/lang/String;I)Z", claimWithdrawTax),
		JF("_claimPayMaintenance", "(JJI)Z", claimPayMaintenance),
		JF("_claimAddBan", "(JJJ)Z", claimAddBan),
		JF("_claimRemoveBan", "(JJJ)Z", claimRemoveBan),
		JF("_claimGetTaxBalance", "(ILjava/lang/String;)I", claimGetTaxBalance),
		JF("_claimCanManipulateFurniture", "(JJ)Z", claimCanManipulateFurniture),
		JF("_claimValidateWorldPosition", "(JJFFF)Z", claimValidateWorldPosition),
		JF("_claimAddAllowed", "(JJJ)Z", claimAddAllowed),
		JF("_claimRemoveAllowed", "(JJJ)Z", claimRemoveAllowed),
		JF("_claimRemoveClaim", "(JIZ)Z", claimRemoveClaim),
		JF("_claimPurgeClaimsForPlayer", "(JJZZ)I", claimPurgeClaimsForPlayer),
		JF("_claimFindClaimIdByMarker", "(J)I", claimFindClaimIdByMarker),
		JF("_claimCanManageTerminal", "(JJ)Z", claimCanManageTerminal),
		JF("_claimGetMaintenancePrepay", "(JJ)I", claimGetMaintenancePrepay),
		JF("_claimGetMaintenanceDueTime", "(JJ)I", claimGetMaintenanceDueTime),
		JF("_claimGetClaimStatus", "(JJ)I", claimGetClaimStatus),
#undef JF
	};
	return JavaLibrary::registerNatives(NATIVES, sizeof(NATIVES) / sizeof(NATIVES[0]));
}

static bool validateClaimAdminAccess(JNIEnv *env, jlong admin, CreatureObject const *&adminCreatureOut)
{
	adminCreatureOut = nullptr;
	ServerObject *adminObj = nullptr;
	if (!JavaLibrary::getObject(admin, adminObj) || !adminObj)
		return false;
	adminCreatureOut = adminObj->asCreatureObject();
	if (!adminCreatureOut)
		return false;

	Client *const client = adminCreatureOut->getClient();
	if (client && client->isGod())
		return true;

	int testCenter = 0;
	return adminCreatureOut->getObjVars().getItem("test_center", testCenter) && testCenter != 0;
}

static bool validateClaimTerminalAccess(JNIEnv *env, jlong player, jlong terminal, uint32 &claimIdOut)
{
	claimIdOut = 0;

	ServerObject *playerObj = nullptr;
	if (!JavaLibrary::getObject(player, playerObj) || !playerObj)
		return false;
	CreatureObject *creature = playerObj->asCreatureObject();
	if (!creature)
		return false;
	PlayerObject const *po = PlayerCreatureController::getPlayerObject(creature);
	if (!po)
		return false;

	ServerObject *termObj = nullptr;
	if (!JavaLibrary::getObject(terminal, termObj) || !termObj)
		return false;

	int claimId = 0;
	if (!termObj->getObjVars().getItem("claim.id", claimId) || claimId <= 0)
		return false;

	StationId ownerAccount;
	if (!ClaimManager::getInstance().getClaimOwnerAccount(static_cast<uint32>(claimId), ownerAccount))
		return false;

	if (po->getStationId() != ownerAccount)
		return false;

	claimIdOut = static_cast<uint32>(claimId);
	return true;
}

jint JNICALL ScriptMethodsClaimNamespace::claimFinalizePlacement(JNIEnv *env, jobject self, jlong player, jlong marker, jfloat x, jfloat y, jfloat z, jfloat radius, jlong terminal)
{
	UNREF(env);
	UNREF(self);

	if (!ConfigServerGame::getClaimSystemEnabled())
		return 0;

	ServerObject *playerObj = nullptr;
	if (!JavaLibrary::getObject(player, playerObj) || !playerObj)
		return 0;
	CreatureObject *creature = playerObj->asCreatureObject();
	if (!creature)
		return 0;
	PlayerObject const *po = PlayerCreatureController::getPlayerObject(creature);
	if (!po)
		return 0;

	ServerObject *markerObj = nullptr;
	if (!JavaLibrary::getObject(marker, markerObj) || !markerObj)
		return 0;

	float useRadius = radius;
	if (useRadius <= 0.f)
		useRadius = ConfigServerGame::getClaimDefaultFootprintRadiusMeters();
	if (useRadius <= 0.f)
		useRadius = 32.f;

	Vector const center(x, y, z);
	std::string const scene = markerObj->getSceneId();

	NetworkId terminalId(terminal);
	if (!terminalId.isValid())
		terminalId = NetworkId::cms_invalid;

	uint32 const claimId = ClaimManager::getInstance().tryRegisterClaim(
		po->getStationId(),
		creature->getNetworkId(),
		markerObj->getNetworkId(),
		scene,
		center,
		useRadius,
		terminalId);

	if (claimId == 0)
		return 0;

	markerObj->setObjVarItem("claim.id", static_cast<int>(claimId));
	markerObj->setObjVarItem("claim.is_marker", 1);
	markerObj->setObjVarItem("claim.footprint_radius_m", useRadius);
	ClaimManager::getInstance().bindObjectToClaim(claimId, markerObj->getNetworkId());

	NetworkId const markerId = markerObj->getNetworkId();
	if (terminalId.isValid() && terminalId != markerId)
	{
		ServerObject *tobj = ServerWorld::findObjectByNetworkId(terminalId);
		if (tobj)
		{
			tobj->setObjVarItem("claim.id", static_cast<int>(claimId));
			tobj->setObjVarItem("claim.is_terminal", 1);
			ClaimManager::getInstance().bindObjectToClaim(claimId, terminalId);
		}
	}
	else
	{
		markerObj->setObjVarItem("claim.is_terminal", 1);
	}

	return static_cast<jint>(claimId);
}

void JNICALL ScriptMethodsClaimNamespace::claimBindObject(JNIEnv *env, jobject self, jlong obj, jint claimId)
{
	UNREF(env);
	UNREF(self);
	if (claimId <= 0)
		return;
	ServerObject *o = nullptr;
	if (!JavaLibrary::getObject(obj, o) || !o)
		return;
	o->setObjVarItem("claim.id", static_cast<int>(claimId));
	ClaimManager::getInstance().bindObjectToClaim(static_cast<uint32>(claimId), o->getNetworkId());
}

void JNICALL ScriptMethodsClaimNamespace::claimUnbindObject(JNIEnv *env, jobject self, jlong obj)
{
	UNREF(env);
	UNREF(self);
	ServerObject *o = nullptr;
	if (!JavaLibrary::getObject(obj, o) || !o)
		return;
	ClaimManager::getInstance().unbindObjectFromClaim(o->getNetworkId());
	o->removeObjVarItem("claim.id");
	o->removeObjVarItem("claim.is_marker");
	o->removeObjVarItem("claim.is_terminal");
}

jint JNICALL ScriptMethodsClaimNamespace::claimApplyVisitorResourceTax(JNIEnv *env, jobject self, jlong player, jfloat x, jfloat y, jfloat z, jstring resourceKey, jint amount)
{
	UNREF(self);
	if (!ConfigServerGame::getClaimSystemEnabled() || amount <= 0)
		return amount;

	CreatureObject const *creature = JavaLibrary::getCreatureThrow(env, player, "claimApplyVisitorResourceTax", false);
	if (!creature)
		return amount;

	std::string key = "generic";
	if (resourceKey)
	{
		JavaStringParam jsp(resourceKey);
		IGNORE_RETURN(JavaLibrary::convert(jsp, key));
	}

	Vector const pos(x, y, z);
	return ClaimManager::getInstance().applyVisitorResourceTax(creature, creature->getSceneId(), pos, key, amount);
}

jboolean JNICALL ScriptMethodsClaimNamespace::claimWithdrawTax(JNIEnv *env, jobject self, jlong player, jlong terminal, jstring resourceKey, jint amount)
{
	UNREF(self);
	uint32 claimId = 0;
	if (!validateClaimTerminalAccess(env, player, terminal, claimId))
		return JNI_FALSE;

	std::string key = "generic";
	if (resourceKey)
	{
		JavaStringParam jsp(resourceKey);
		IGNORE_RETURN(JavaLibrary::convert(jsp, key));
	}

	return ClaimManager::getInstance().withdrawTaxBalance(claimId, key, amount) ? JNI_TRUE : JNI_FALSE;
}

jboolean JNICALL ScriptMethodsClaimNamespace::claimPayMaintenance(JNIEnv *env, jobject self, jlong player, jlong terminal, jint credits)
{
	UNREF(self);
	uint32 claimId = 0;
	if (!validateClaimTerminalAccess(env, player, terminal, claimId))
		return JNI_FALSE;

	if (credits <= 0)
		return JNI_FALSE;

	ServerObject *playerObj = nullptr;
	if (!JavaLibrary::getObject(player, playerObj) || !playerObj)
		return JNI_FALSE;

	if (playerObj->getBankBalance() < credits)
		return JNI_FALSE;

	// Same sink account as player structure maintenance (see money.ACCT_STRUCTURE_MAINTENANCE).
	if (!playerObj->transferBankCreditsTo(std::string("structureMaintanence"), credits))
		return JNI_FALSE;

	return ClaimManager::getInstance().payMaintenance(claimId, credits) ? JNI_TRUE : JNI_FALSE;
}

jboolean JNICALL ScriptMethodsClaimNamespace::claimAddBan(JNIEnv *env, jobject self, jlong player, jlong terminal, jlong banned)
{
	UNREF(self);
	uint32 claimId = 0;
	if (!validateClaimTerminalAccess(env, player, terminal, claimId))
		return JNI_FALSE;

	NetworkId const bannedId(banned);
	if (!bannedId.isValid())
		return JNI_FALSE;

	ClaimManager::getInstance().addBan(claimId, bannedId);
	return JNI_TRUE;
}

jboolean JNICALL ScriptMethodsClaimNamespace::claimRemoveBan(JNIEnv *env, jobject self, jlong player, jlong terminal, jlong banned)
{
	UNREF(self);
	uint32 claimId = 0;
	if (!validateClaimTerminalAccess(env, player, terminal, claimId))
		return JNI_FALSE;

	NetworkId const bannedId(banned);
	if (!bannedId.isValid())
		return JNI_FALSE;

	ClaimManager::getInstance().removeBan(claimId, bannedId);
	return JNI_TRUE;
}

jint JNICALL ScriptMethodsClaimNamespace::claimGetTaxBalance(JNIEnv *env, jobject self, jint claimId, jstring resourceKey)
{
	UNREF(env);
	UNREF(self);
	if (claimId <= 0)
		return 0;
	std::string key = "generic";
	if (resourceKey)
	{
		JavaStringParam jsp(resourceKey);
		IGNORE_RETURN(JavaLibrary::convert(jsp, key));
	}
	return ClaimManager::getInstance().getTaxBalance(static_cast<uint32>(claimId), key);
}

jboolean JNICALL ScriptMethodsClaimNamespace::claimCanManipulateFurniture(JNIEnv *env, jobject self, jlong player, jlong target)
{
	UNREF(self);
	if (!ConfigServerGame::getClaimSystemEnabled())
		return JNI_TRUE;

	CreatureObject const *creature = JavaLibrary::getCreatureThrow(env, player, "claimCanManipulateFurniture", false);
	if (!creature)
		return JNI_FALSE;

	ServerObject *tobj = nullptr;
	if (!JavaLibrary::getObject(target, tobj) || !tobj)
		return JNI_FALSE;

	return ClaimManager::getInstance().allowWorldManipulation(creature, *tobj) ? JNI_TRUE : JNI_FALSE;
}

jboolean JNICALL ScriptMethodsClaimNamespace::claimValidateWorldPosition(JNIEnv *env, jobject self, jlong player, jlong target, jfloat x, jfloat y, jfloat z)
{
	UNREF(self);
	if (!ConfigServerGame::getClaimSystemEnabled())
		return JNI_TRUE;

	CreatureObject const *creature = JavaLibrary::getCreatureThrow(env, player, "claimValidateWorldPosition", false);
	if (!creature)
		return JNI_FALSE;

	ServerObject *tobj = nullptr;
	if (!JavaLibrary::getObject(target, tobj) || !tobj)
		return JNI_FALSE;

	Vector const newPos(x, y, z);
	return ClaimManager::getInstance().validateManipulateWorldPosition(creature, *tobj, newPos) ? JNI_TRUE : JNI_FALSE;
}

jboolean JNICALL ScriptMethodsClaimNamespace::claimAddAllowed(JNIEnv *env, jobject self, jlong player, jlong terminal, jlong allowed)
{
	UNREF(self);
	uint32 claimId = 0;
	if (!validateClaimTerminalAccess(env, player, terminal, claimId))
		return JNI_FALSE;

	NetworkId const allowedId(allowed);
	if (!allowedId.isValid())
		return JNI_FALSE;

	ClaimManager::getInstance().addAllowed(claimId, allowedId);
	return JNI_TRUE;
}

jboolean JNICALL ScriptMethodsClaimNamespace::claimRemoveAllowed(JNIEnv *env, jobject self, jlong player, jlong terminal, jlong allowed)
{
	UNREF(self);
	uint32 claimId = 0;
	if (!validateClaimTerminalAccess(env, player, terminal, claimId))
		return JNI_FALSE;

	NetworkId const allowedId(allowed);
	if (!allowedId.isValid())
		return JNI_FALSE;

	ClaimManager::getInstance().removeAllowed(claimId, allowedId);
	return JNI_TRUE;
}

jboolean JNICALL ScriptMethodsClaimNamespace::claimRemoveClaim(JNIEnv *env, jobject self, jlong admin, jint claimId, jboolean destroyWorldObjects)
{
	UNREF(self);
	UNREF(env);

	if (!ConfigServerGame::getClaimSystemEnabled() || claimId <= 0)
		return JNI_FALSE;

	CreatureObject const *adminCreature = nullptr;
	if (!validateClaimAdminAccess(env, admin, adminCreature))
		return JNI_FALSE;
	UNREF(adminCreature);

	return ClaimManager::getInstance().removeClaim(static_cast<uint32>(claimId), destroyWorldObjects == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

jint JNICALL ScriptMethodsClaimNamespace::claimPurgeClaimsForPlayer(JNIEnv *env, jobject self, jlong admin, jlong targetPlayer, jboolean destroyWorldObjects, jboolean allAccountClaims)
{
	UNREF(self);
	UNREF(env);

	if (!ConfigServerGame::getClaimSystemEnabled())
		return 0;

	CreatureObject const *adminCreature = nullptr;
	if (!validateClaimAdminAccess(env, admin, adminCreature))
		return 0;

	ServerObject *targetObj = nullptr;
	if (!JavaLibrary::getObject(targetPlayer, targetObj) || !targetObj)
		return 0;

	CreatureObject *const targetCreature = targetObj->asCreatureObject();
	if (!targetCreature)
		return 0;

	bool const destroyContent = destroyWorldObjects == JNI_TRUE;
	if (allAccountClaims == JNI_TRUE)
	{
		PlayerObject const *const po = PlayerCreatureController::getPlayerObject(targetCreature);
		if (!po)
			return 0;
		return ClaimManager::getInstance().purgeClaimsForAccount(po->getStationId(), destroyContent);
	}

	return ClaimManager::getInstance().purgeClaimsForCharacter(targetCreature->getNetworkId(), destroyContent);
}

jint JNICALL ScriptMethodsClaimNamespace::claimFindClaimIdByMarker(JNIEnv *env, jobject self, jlong marker)
{
	UNREF(self);
	UNREF(env);

	if (!ConfigServerGame::getClaimSystemEnabled())
		return 0;

	NetworkId const markerId(marker);
	uint32 const claimId = ClaimManager::getInstance().findClaimIdByMarker(markerId);
	return static_cast<jint>(claimId);
}

jboolean JNICALL ScriptMethodsClaimNamespace::claimCanManageTerminal(JNIEnv *env, jobject self, jlong player, jlong terminal)
{
	UNREF(self);
	UNREF(env);
	uint32 claimId = 0;
	return validateClaimTerminalAccess(env, player, terminal, claimId) ? JNI_TRUE : JNI_FALSE;
}

jint JNICALL ScriptMethodsClaimNamespace::claimGetMaintenancePrepay(JNIEnv *env, jobject self, jlong player, jlong terminal)
{
	UNREF(self);
	uint32 claimId = 0;
	if (!validateClaimTerminalAccess(env, player, terminal, claimId))
		return 0;
	return ClaimManager::getInstance().getMaintenancePrepayCredits(claimId);
}

jint JNICALL ScriptMethodsClaimNamespace::claimGetMaintenanceDueTime(JNIEnv *env, jobject self, jlong player, jlong terminal)
{
	UNREF(self);
	uint32 claimId = 0;
	if (!validateClaimTerminalAccess(env, player, terminal, claimId))
		return 0;
	unsigned long const due = ClaimManager::getInstance().getNextMaintenanceDueGameSeconds(claimId);
	if (due > static_cast<unsigned long>(0x7FFFFFFF))
		return 0x7FFFFFFF;
	return static_cast<jint>(due);
}

jint JNICALL ScriptMethodsClaimNamespace::claimGetClaimStatus(JNIEnv *env, jobject self, jlong player, jlong terminal)
{
	UNREF(self);
	uint32 claimId = 0;
	if (!validateClaimTerminalAccess(env, player, terminal, claimId))
		return -1;
	return ClaimManager::getInstance().getClaimStatus(claimId);
}
