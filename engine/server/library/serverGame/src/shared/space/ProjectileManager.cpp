// ======================================================================
//
// ProjectileManager.cpp
//
// Copyright 2003 Sony Online Entertainment
//
// ======================================================================

#include "serverGame/FirstServerGame.h"
#include "serverGame/ProjectileManager.h"

#include "serverGame/ConnectionServerConnection.h"
#include "serverGame/CreatureObject.h"
#include "serverGame/PlayerShipController.h"
#include "serverGame/ServerWorld.h"
#include "serverGame/ShipObject.h"
#include "serverGame/TangibleObject.h"
#include "SwgGameServer/CombatEngine.h"
#include "sharedTerrain/TerrainObject.h"
#include "sharedRandom/Random.h"
#include "serverNetworkMessages/GameConnectionServerMessages.h"
#include "serverScript/GameScriptObject.h"
#include "serverScript/ScriptFunctionTable.h"
#include "serverScript/ScriptParameters.h"
#include "sharedCollision/CollisionProperty.h"
#include "sharedCollision/CollisionWorld.h"
#include "sharedCollision/Extent.h"
#include "sharedCollision/SpatialDatabase.h"
#include "sharedGame/GameObjectTypes.h"
#include "sharedGame/SharedObjectTemplate.h"
#include "sharedGame/ShipChassisSlotType.h"
#include "sharedMath/MultiShape.h"
#include "sharedMathArchive/TransformArchive.h"
#include "sharedNetworkMessages/CreateProjectileMessage.h"
#include "sharedObject/Appearance.h"
#include "sharedObject/AppearanceTemplate.h"
#include "sharedObject/CellProperty.h"

// ======================================================================

namespace ProjectileManagerNamespace
{

	// ======================================================================

	class Projectile
	{
	public:

		// ----------------------------------------------------------------------

		Projectile(ShipObject &owner, int weaponIndex, int projectileIndex, int targetedComponent, float startDeltaTime, Vector const &position_w, Vector const &direction_w, float length, float speed, float duration, bool fromAutoTurret, bool isBeam) :
			m_owner(&owner),
			m_weaponIndex(weaponIndex),
			m_projectileIndex(projectileIndex),
			m_targetedComponent(targetedComponent),
			m_startDeltaTime(startDeltaTime),
			m_position_w(position_w),
			m_direction_w(direction_w),
			m_length(length),
			m_speed(speed),
			m_duration(duration),
			m_fromAutoTurret(fromAutoTurret)
		{

				if (isBeam)
				{
					//-- offset transform used for beam positioning
					m_position_w = m_owner->rotateTranslate_w2o(m_position_w);
					m_direction_w = m_owner->rotate_w2o(m_direction_w);
					m_length = m_speed * m_duration;
				}
		}

		// ----------------------------------------------------------------------

		Projectile(Projectile const &rhs) :
			m_owner(rhs.m_owner),
			m_weaponIndex(rhs.m_weaponIndex),
			m_projectileIndex(rhs.m_projectileIndex),
			m_targetedComponent(rhs.m_targetedComponent),
			m_startDeltaTime(rhs.m_startDeltaTime),
			m_position_w(rhs.m_position_w),
			m_direction_w(rhs.m_direction_w),
			m_length(rhs.m_length),
			m_speed(rhs.m_speed),
			m_duration(rhs.m_duration),
			m_fromAutoTurret(rhs.m_fromAutoTurret)
		{
		}

		//----------------------------------------------------------------------

		Projectile() :
			m_owner(nullptr),
			m_weaponIndex(0),
			m_projectileIndex(0),
			m_targetedComponent(0),
			m_startDeltaTime(0.0f),
			m_position_w(),
			m_direction_w(),
			m_length(0.0f),
			m_speed(0.0f),
			m_duration(0.0f),
			m_fromAutoTurret(false)
		{
		}

		// ----------------------------------------------------------------------

		~Projectile()
		{
		}

		// ----------------------------------------------------------------------

		bool update(float const elapsedTime, bool isBeam)
		{
			// If the object no longer exists, kill the projectile
			if (!m_owner.getPointer())
				return false;
				
			//-- for beam weapons, the m_position_w and m_direction_w vectors are actually in the ship's object space, not world space.
			//-- these values are not changed by this update() method

			Vector projectilePosition_w;
			Vector projectilePath;

			Vector moveAmountThisFrame;

			if (isBeam)
			{
				projectilePosition_w = m_owner->rotateTranslate_o2w(m_position_w);
				projectilePath = m_owner->rotate_o2w(m_direction_w * m_length);
			}
			else
			{
				// Move the projectile appropriately for the amount of time passed.  Return whether the projectile should still exist.
				// If the projectile collides with something or its time expires, it should no longer exist.
				moveAmountThisFrame = m_direction_w * (m_speed * elapsedTime);
				projectilePath = moveAmountThisFrame + (m_direction_w * m_length);
				
				//-- We need to take into consideration that the object has actually been 
				//   advanced forward in time by the time it makes it into the ProjectileManager.
				//   The very first update should be from where the proijectile originated.
				//   To catch potential cheaters, we will not allow this time to be beyond 0.25 seconds.
				projectilePosition_w = m_position_w;
				if (m_startDeltaTime > 0.f)
				{
					float const deltaTime = std::min(m_startDeltaTime, 0.25f);
					m_startDeltaTime = 0.f;
					
					Vector const positionOffset_w(m_direction_w * m_speed * deltaTime);
					projectilePosition_w -= positionOffset_w;
					projectilePath += positionOffset_w;
				}
			}

			// Check terrain collision in atmospheric flight (ship-to-ground combat).
			float terrainHitTime = 2.0f;  // >1 means no hit
			if (ServerWorld::isAtmosphericFlightScene())
			{
				TerrainObject const * const terrain = TerrainObject::getConstInstance();
				if (terrain)
				{
					Vector const pathEnd_w = projectilePosition_w + projectilePath;
					float const pathLength = projectilePath.magnitude();
					int const numSteps = pathLength > 0.f ? std::max(1, static_cast<int>(pathLength / 2.f)) : 1;
					for (int s = 1; s <= numSteps; ++s)
					{
						float const t = static_cast<float>(s) / static_cast<float>(numSteps);
						Vector const sample_w = Vector::linearInterpolate(projectilePosition_w, pathEnd_w, t);
						float terrainHeight = 0.f;
						if (terrain->getHeightForceChunkCreation(Vector(sample_w.x, 0.f, sample_w.z), terrainHeight) && sample_w.y < terrainHeight)
						{
							terrainHitTime = t;
							break;
						}
					}
				}
			}

			// Get all objects that collide with the line segment from m_position_w to endPosition.  Don't collide with the owner.
			float const projectileSize = 1.f;
			Capsule const projectileCapsule_w(Sphere(projectilePosition_w, projectileSize), projectilePath);
			ColliderList collidedWith;
			CollisionWorld::getDatabase()->queryFor(static_cast<int>(SpatialDatabase::Q_Physicals), CellProperty::getWorldCellProperty(), true, projectileCapsule_w, collidedWith);

			Object * closestObject = nullptr;
			float smallestTime = 2.0f;  // >1 means no hit
			Vector collisionPosition_o;

			for (ColliderList::const_iterator i = collidedWith.begin(); i != collidedWith.end(); ++i)
			{
				Object &collider = NON_NULL(*i)->getOwner();
				if (projectileCollidesWith(collider))
				{
					// find which object it collided with first, and when
					BaseExtent const * const extent_l = (*i)->getExtent_l();
					if (extent_l == nullptr)
						WARNING(true, ("ProjectileManager collided with object with nullptr extent. [%s], appearance=[%s]", collider.getDebugName(), collider.getAppearanceTemplateName()));
					else
					{
						Vector const start_o = collider.rotateTranslate_w2o(projectilePosition_w);
						Vector const end_o = collider.rotateTranslate_w2o(projectilePosition_w + projectilePath);
						float time;
						if (extent_l->intersect(start_o, end_o, nullptr, &time))
						{
							if (!closestObject || time < smallestTime)
							{
								closestObject = &collider;
								smallestTime = time;
								collisionPosition_o = Vector::linearInterpolate(start_o, end_o, time);
							}
						}
					}
				}
			}

			// Terrain impact takes precedence if it occurs first (or is the only hit).
			if (terrainHitTime < smallestTime)
			{
				Vector const impactPosition_w = Vector::linearInterpolate(projectilePosition_w, projectilePosition_w + projectilePath, terrainHitTime);
				triggerTerrainImpact(impactPosition_w);
				return false;
			}

			if (closestObject)
			{
				ServerObject * const so = closestObject->asServerObject();
				if (so)
					trigger(*so, collisionPosition_o);
				
				return false;
			}
			
			if (!isBeam)
			{
				// update the position and remaining duration
				m_position_w += moveAmountThisFrame;
				m_duration -= elapsedTime;
			}

			return m_duration > 0.0f;
		}

		// ----------------------------------------------------------------------

		bool projectileCollidesWith(Object const &target) const
		{
			// Projectiles collide with anything that isn't a friendly, visible, non-capital ship
			ShipObject const * const actorShip = m_owner.getPointer();
			ShipObject const * const targetShip = target.asServerObject() ? target.asServerObject()->asShipObject() : 0;
			if (   actorShip != &target
			    && !target.getKill()
			    && (   !targetShip
			        || (   targetShip->isVisible()
			            && (   targetShip->isCapitalShip()
			                || Pvp::canAttack(*actorShip, *targetShip)))))
				return true;
			return false;
		}

		// ----------------------------------------------------------------------
		// Splash damage constants for ship-to-ground combat.
		static float const s_splashRadius;
		static float const s_splashDamageFraction;

		void applySplashDamage(Vector const &center_w, ServerObject const *excludeDirectTarget, bool inAtmospheric) const
		{
			if (!inAtmospheric)
				return;
			ShipObject * const actorShip = m_owner.getPointer();
			if (!actorShip)
				return;
			int const weaponSlot = m_weaponIndex + static_cast<int>(ShipChassisSlotType::SCST_weapon_0);
			float const damageMin = actorShip->getWeaponDamageMinimum(weaponSlot);
			float const damageMax = actorShip->getWeaponDamageMaximum(weaponSlot);
			int const splashAmount = static_cast<int>(Random::randomReal(damageMin, damageMax) * s_splashDamageFraction);
			if (splashAmount <= 0)
				return;
			std::vector<ServerObject *> targetList;
			ServerWorld::findObjectsInRange(center_w, s_splashRadius, targetList);
			for (std::vector<ServerObject *>::const_iterator it = targetList.begin(); it != targetList.end(); ++it)
			{
				ServerObject * const obj = *it;
				if (!obj || obj == actorShip || obj == excludeDirectTarget)
					continue;
				TangibleObject * const defender = obj->asTangibleObject();
				if (!defender || defender->getKill())
					continue;
				if (defender->isAuthoritative())
					CombatEngine::damage(*defender, ServerWeaponObjectTemplate::DT_kinetic, 0, splashAmount);
			}
		}

		// ----------------------------------------------------------------------

		void triggerTerrainImpact(Vector const &impactPosition_w) const
		{
			applySplashDamage(impactPosition_w, nullptr, true);
		}

		// ----------------------------------------------------------------------

		void trigger(ServerObject &target, Vector const & collisionPosition_o) const
		{
			// Our projectile has hit a server object. Fire off a OnShipHit Trigger for that object.
			// If our object is a ship, then we check PvP rules before damaging the target.
			ShipObject * const actorShip = m_owner.getPointer();
			ShipObject * const targetShip = target.asShipObject();

/*
			DEBUG_REPORT_LOG(actorShip && targetShip, ("%s shooting %s (%s).\n",
				actorShip->getNetworkId().getValueString().c_str(),
				targetShip->getNetworkId().getValueString().c_str(),
				Pvp::canAttack(*actorShip, *targetShip) ? "allowed" : "denied"));
*/

			if ( actorShip )
			{
				// For small ships, a hit anywhere on the ship counts as hitting the selected target.  For capital ships,
				// it detects what component was actually hit
				int actualComponentHit = m_targetedComponent;

				if (targetShip && targetShip->isCapitalShip())
				{
					actualComponentHit = targetShip->findTargetChassisSlotByPosition(collisionPosition_o);
				}
				
				// trigger target that it was hit
				{
					ScriptParams params;
					params.addParam(actorShip->getNetworkId());
					params.addParam(m_weaponIndex);
					params.addParam(false);
					params.addParam(static_cast<int>(0));
					params.addParam(actualComponentHit);
					params.addParam(m_fromAutoTurret);
					params.addParam(collisionPosition_o.x);
					params.addParam(collisionPosition_o.y);
					params.addParam(collisionPosition_o.z);
					CreatureObject * const targetCreature = target.asCreatureObject();
					if (targetCreature && ServerWorld::isAtmosphericFlightScene())
					{
						int const weaponSlot = m_weaponIndex + static_cast<int>(ShipChassisSlotType::SCST_weapon_0);
						float const damageMin = actorShip->getWeaponDamageMinimum(weaponSlot);
						float const damageMax = actorShip->getWeaponDamageMaximum(weaponSlot);
						int const damageAmount = static_cast<int>(Random::randomReal(damageMin, damageMax));
						if (damageAmount > 0)
						{
							CombatEngine::damage(*targetCreature, ServerWeaponObjectTemplate::DT_kinetic, 0, damageAmount);
						}
					}
					if(!targetShip) // For Non ship objects. Just trigger the event.
						IGNORE_RETURN(target.getScriptObject()->trigAllScripts(Scripting::TRIG_SHIP_HIT, params));
					if(targetShip  && Pvp::canAttack(*actorShip, *targetShip)) // For ship objects AND PvP rule is OK
					{
						// Trigger event and decrease ship HP.
						IGNORE_RETURN(target.getScriptObject()->trigAllScripts(Scripting::TRIG_SHIP_HIT, params));
						targetShip->setNumberOfHits(targetShip->getNumberOfHits() + 1);	
					}
					// Splash damage in atmospheric flight (ship-to-ground combat).
					Vector const splashCenter_w = target.rotateTranslate_o2w(collisionPosition_o);
					applySplashDamage(splashCenter_w, &target, ServerWorld::isAtmosphericFlightScene());
				}

				// trigger attacker that they hit
				{
					ScriptParams params;
					params.addParam(target.getNetworkId());
					params.addParam(m_weaponIndex);
					params.addParam(actualComponentHit);
					IGNORE_RETURN(actorShip->getScriptObject()->trigAllScripts(Scripting::TRIG_SHIP_HITTING, params));
				}
			}
		}

		// ----------------------------------------------------------------------

		ShipObject * getOwner()
		{
			return m_owner.getPointer();
		}

	private:

		Watcher<ShipObject> m_owner;
		int m_weaponIndex;
		int m_projectileIndex;
		int m_targetedComponent;
		float m_startDeltaTime;
		Vector m_position_w;
		Vector m_direction_w;
		float m_length;
		float m_speed;
		float m_duration;
		bool m_fromAutoTurret;
	};

	// ======================================================================

	float const Projectile::s_splashRadius = 10.0f;
	float const Projectile::s_splashDamageFraction = 0.5f;

	std::vector<Projectile> s_projectiles;

	//----------------------------------------------------------------------

	typedef std::pair<NetworkId /* ship id */, int /*weaponIndex */> ShipWeaponPair;
	typedef std::map<ShipWeaponPair, Projectile> ProjectileMap;
	ProjectileMap s_projectileBeamMap;


	// ======================================================================
}
using namespace ProjectileManagerNamespace;

// ======================================================================

void ProjectileManager::create(Client const *gunnerClient, ShipObject &owner, int weaponIndex, int const projectileIndex, int const targetedComponent, float const startDeltaTime, Transform const & transform_p, float const length, float const speed, float const duration, bool const fromAutoTurret)
{
	UNREF(gunnerClient);
	bool const isBeam = owner.isBeamWeapon(weaponIndex);

	Projectile const projectile(owner, weaponIndex, projectileIndex, targetedComponent, startDeltaTime, transform_p.getPosition_p(), transform_p.getLocalFrameK_p(), length, speed, duration, fromAutoTurret, isBeam);

	if (isBeam)
	{
		s_projectileBeamMap[ShipWeaponPair(owner.getNetworkId(), weaponIndex)] = projectile;
	}
	else
	{
		// add the projectile to the active projectiles vector
		s_projectiles.push_back(projectile);
	}

	// send the projectile to all clients observing the owner except the owner
	// time is in the context of the connection server's sync stamp, so we need a unique message per connection server
	typedef std::map<ConnectionServerConnection *, std::vector<NetworkId> > DistributionList;
	DistributionList distributionList;

	//-- Build distribution list of clients observing ship, but don't include the client doing the firing
	{
		typedef std::set<Client *> ObserverList;
		ObserverList const & observers = owner.getObservers();
		for (ObserverList::const_iterator iter = observers.begin(); iter != observers.end(); ++iter)
		{
			if (*iter != gunnerClient)
			{
				ServerObject const * const characterObject = (*iter)->getCharacterObject();
				if (characterObject)
				{
					// Don't send projectiles to players in ships that are teleporting
					ShipObject const * const clientShip = ShipObject::getContainingShipObject(characterObject);
					if (!clientShip || !safe_cast<PlayerShipController const *>(clientShip->getController())->isTeleporting() || !clientShip->getPilot())
						distributionList[(*iter)->getConnection()].push_back(characterObject->getNetworkId());
				}
			}
		}
	}

	//-- Send CreateProjectileMessage to distribution list
	{
		for (DistributionList::const_iterator iter = distributionList.begin(); iter != distributionList.end(); ++iter)
		{
			CreateProjectileMessage const createProjectileMessage(owner.getShipId(), weaponIndex, projectileIndex, targetedComponent, transform_p, (*iter).first->getSyncStampLong());
			GameClientMessage const gameClientMessage(iter->second, false, createProjectileMessage);
			iter->first->send(gameClientMessage, true);
		}
	}
}

//----------------------------------------------------------------------

void ProjectileManager::stopBeam(ShipObject & shipOwner, int weaponIndex)
{
	ShipWeaponPair const key(shipOwner.getNetworkId(), weaponIndex);
	s_projectileBeamMap.erase(key);
}

// ----------------------------------------------------------------------

void ProjectileManager::update(float timePassed) // static
{
	{
		// Call Projectile::update all projectiles, removing from the vector if Projectile::update() returns false
		unsigned int i = 0;
		while (i < s_projectiles.size())
		{
			if (s_projectiles[i].update(timePassed, false))
				++i;
			else
			{
				s_projectiles[i] = s_projectiles[s_projectiles.size()-1];
				s_projectiles.pop_back();
			}
		}
	}

	//--
	//-- Update the beam weapons
	//--

	{
		for (ProjectileMap::iterator it = s_projectileBeamMap.begin(); it != s_projectileBeamMap.end();)
		{
			ShipWeaponPair const & key = (*it).first;
			NetworkId const & shipId = key.first;
			int weaponIndex = key.second;
			Projectile & projectile = (*it).second;

			ShipObject * const shipObject = projectile.getOwner();

			if (nullptr == shipObject)
			{
				WARNING(true, ("ProjectileManager beam weapon [%d] for ship id [%s], ship object no longer exists.", weaponIndex, shipId.getValueString().c_str()));
			}
			else
			{
				if (shipObject->isBeamWeapon(weaponIndex))
				{
					if (!shipObject->isComponentFunctional(static_cast<int>(ShipChassisSlotType::SCST_weapon_first) + weaponIndex))
					{
						WARNING(true, ("ProjectileManager beam weapon [%d] for ship [%s] is no longer functional.", weaponIndex, shipId.getValueString().c_str()));
					}
					else
					{
						projectile.update(timePassed, true);
						++it;
						continue;
					}
				}
			}

			//-- this beam is no longer valid, remove it
			s_projectileBeamMap.erase(it++);
		}
	}
}

// ======================================================================

