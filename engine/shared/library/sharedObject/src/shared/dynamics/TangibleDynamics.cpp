//===================================================================
//
// TangibleDynamics.cpp
// Copyright 2025, 2026 Titan SWG, All Rights Reserved
//
// Full implementation of TangibleDynamics providing push, spin,
// breathing, bounce, wobble, orbit, drag, and easing effects.
//
//===================================================================

#include "sharedObject/FirstSharedObject.h"
#include "sharedObject/TangibleDynamics.h"

#include "sharedMath/Vector.h"
#include "sharedObject/Object.h"
#include "sharedObject/AlterResult.h"
#include "sharedObject/NetworkIdManager.h"
#include "sharedFoundation/NetworkId.h"
#include "sharedMath/Transform.h"
#include "sharedTerrain/TerrainObject.h"

#include <cmath>

//===================================================================
// STATIC HELPERS
//===================================================================

namespace TangibleDynamicsNamespace
{
	static float const s_minimumElapsedTime = 0.001f;
	static float const s_bounceMinVelocity  = 0.05f;

	inline bool isExpired(float duration, float elapsed)
	{
		return (duration >= 0.0f && elapsed >= duration);
	}
}

using namespace TangibleDynamicsNamespace;

// Use global PI constants from FloatMath.h (included via FirstSharedObject.h)

//===================================================================
// CONSTRUCTOR / DESTRUCTOR
//===================================================================

TangibleDynamics::TangibleDynamics(Object* owner) :
	SimpleDynamics(owner),
	// Push
	m_pushVelocity(Vector::zero),
	m_pushDuration(-1.0f),
	m_pushElapsed(0.0f),
	m_pushDrag(0.0f),
	m_pushSpace(MS_world),
	m_pushForceActive(false),
	// Spin
	m_spinAngular(Vector::zero),
	m_spinDuration(-1.0f),
	m_spinElapsed(0.0f),
	m_spinForceActive(false),
	m_spinAroundAppearanceCenter(false),
	// Breathing
	m_baseScale(owner ? owner->getScale() : Vector::xyz111),
	m_breathingMin(1.0f),
	m_breathingMax(1.0f),
	m_breathingSpeed(1.0f),
	m_breathingDuration(-1.0f),
	m_breathingElapsed(0.0f),
	m_breathingPhase(0.0f),
	m_breathingEffectActive(false),
	// Bounce
	m_bounceGravity(9.8f),
	m_bounceElasticity(0.6f),
	m_bounceVerticalVelocity(0.0f),
	m_bounceFloorY(0.0f),
	m_bounceDuration(-1.0f),
	m_bounceElapsed(0.0f),
	m_bounceEffectActive(false),
	// Wobble
	m_wobbleAmplitude(Vector::zero),
	m_wobbleFrequency(Vector::zero),
	m_wobbleDuration(-1.0f),
	m_wobbleElapsed(0.0f),
	m_wobblePhase(0.0f),
	m_wobbleOrigin(Vector::zero),
	m_wobbleEffectActive(false),
	// Orbit
	m_orbitCenter(Vector::zero),
	m_orbitRadius(1.0f),
	m_orbitSpeed(1.0f),
	m_orbitAngle(0.0f),
	m_orbitDuration(-1.0f),
	m_orbitElapsed(0.0f),
	m_orbitEffectActive(false),
	// Hover
	m_hoverHeight(1.0f),
	m_hoverBobAmplitude(0.1f),
	m_hoverBobSpeed(1.0f),
	m_hoverBobPhase(0.0f),
	m_hoverDuration(-1.0f),
	m_hoverElapsed(0.0f),
	m_hoverUpdateAccumulator(0.0f),
	m_hoverEffectActive(false),
	// Follow Target
	m_followTargetId(0),
	m_followDistance(2.0f),
	m_followSpeed(3.0f),
	m_followHoverHeight(1.0f),
	m_followBobAmplitude(0.05f),
	m_followBobPhase(0.0f),
	m_followDuration(-1.0f),
	m_followElapsed(0.0f),
	m_followUpdateAccumulator(0.0f),
	m_followTargetEffectActive(false),
	// Easing
	m_easeType(ET_none),
	m_easeDuration(0.5f),
	// Overall
	m_activeForceMask(FM_none)
{
}

//-------------------------------------------------------------------

TangibleDynamics::~TangibleDynamics()
{
	Object* const owner = getOwner();
	if (owner != NULL && m_breathingEffectActive)
	{
		owner->setRecursiveScale(m_baseScale);
	}
}

//===================================================================
// EASING
//===================================================================

float TangibleDynamics::computeEaseFactor(EaseType easeType, float elapsed, float duration, float easeDuration)
{
	if (easeType == ET_none || easeDuration <= 0.0f)
		return 1.0f;

	float factor = 1.0f;

	switch (easeType)
	{
	case ET_easeIn:
		if (elapsed < easeDuration)
			factor = elapsed / easeDuration;
		break;

	case ET_easeOut:
		if (duration > 0.0f && elapsed > (duration - easeDuration))
		{
			float remaining = duration - elapsed;
			factor = (remaining > 0.0f) ? (remaining / easeDuration) : 0.0f;
		}
		break;

	case ET_easeInOut:
		if (elapsed < easeDuration)
		{
			factor = elapsed / easeDuration;
		}
		else if (duration > 0.0f && elapsed > (duration - easeDuration))
		{
			float remaining = duration - elapsed;
			factor = (remaining > 0.0f) ? (remaining / easeDuration) : 0.0f;
		}
		break;

	default:
		break;
	}

	// Smooth step (hermite interpolation)
	return factor * factor * (3.0f - 2.0f * factor);
}

//===================================================================
// PUSH / SHOVE
//===================================================================

void TangibleDynamics::setPushForce(const Vector& velocity, float duration, MovementSpace space)
{
	m_pushVelocity = velocity;
	m_pushDuration = duration;
	m_pushElapsed = 0.0f;
	m_pushDrag = 0.0f;
	m_pushSpace = space;
	m_pushForceActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::setPushForceWithDrag(const Vector& velocity, float drag, float duration, MovementSpace space)
{
	m_pushVelocity = velocity;
	m_pushDuration = duration;
	m_pushElapsed = 0.0f;
	m_pushDrag = drag;
	m_pushSpace = space;
	m_pushForceActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearPushForce()
{
	m_pushVelocity = Vector::zero;
	m_pushDuration = -1.0f;
	m_pushElapsed = 0.0f;
	m_pushDrag = 0.0f;
	m_pushForceActive = false;
	setCurrentVelocity_w(Vector::zero);
	recalculateMode();
}

//-------------------------------------------------------------------

Vector TangibleDynamics::getPushForce() const
{
	return m_pushVelocity;
}

//-------------------------------------------------------------------

float TangibleDynamics::getPushForceDuration() const
{
	if (m_pushDuration < 0.0f)
		return -1.0f;
	return m_pushDuration - m_pushElapsed;
}

//-------------------------------------------------------------------

void TangibleDynamics::setPushDrag(float drag)
{
	m_pushDrag = drag;
}

//-------------------------------------------------------------------

float TangibleDynamics::getPushDrag() const
{
	return m_pushDrag;
}

//===================================================================
// SPIN / ROTATION
//===================================================================

void TangibleDynamics::setSpinForce(const Vector& rotationRadiansPerSecond, float duration)
{
	m_spinAngular = rotationRadiansPerSecond;
	m_spinDuration = duration;
	m_spinElapsed = 0.0f;
	m_spinForceActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearSpinForce()
{
	m_spinAngular = Vector::zero;
	m_spinDuration = -1.0f;
	m_spinElapsed = 0.0f;
	m_spinForceActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

Vector TangibleDynamics::getSpinForce() const
{
	return m_spinAngular;
}

//-------------------------------------------------------------------

float TangibleDynamics::getSpinForceDuration() const
{
	if (m_spinDuration < 0.0f)
		return -1.0f;
	return m_spinDuration - m_spinElapsed;
}

//-------------------------------------------------------------------

bool TangibleDynamics::getSpinAroundAppearanceCenter() const
{
	return m_spinAroundAppearanceCenter;
}

//-------------------------------------------------------------------

void TangibleDynamics::setSpinAroundAppearanceCenter(bool spinAroundAppearanceCenter)
{
	m_spinAroundAppearanceCenter = spinAroundAppearanceCenter;
}

//===================================================================
// BREATHING (sinusoidal)
//===================================================================

void TangibleDynamics::setBreathingEffect(float minimumScale, float maximumScale, float speed, float duration)
{
	m_breathingMin = minimumScale;
	m_breathingMax = maximumScale;
	m_breathingSpeed = speed;
	m_breathingDuration = duration;
	m_breathingElapsed = 0.0f;
	m_breathingPhase = 0.0f;
	m_breathingEffectActive = true;

	Object* const owner = getOwner();
	if (owner != NULL)
	{
		m_baseScale = owner->getScale();
	}

	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearBreathingEffect()
{
	Object* const owner = getOwner();
	if (owner != NULL && m_breathingEffectActive)
	{
		owner->setRecursiveScale(m_baseScale);
	}

	m_breathingMin = 1.0f;
	m_breathingMax = 1.0f;
	m_breathingSpeed = 1.0f;
	m_breathingDuration = -1.0f;
	m_breathingElapsed = 0.0f;
	m_breathingPhase = 0.0f;
	m_breathingEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

float TangibleDynamics::getBreathingMinScale() const   { return m_breathingMin; }
float TangibleDynamics::getBreathingMaxScale() const   { return m_breathingMax; }
float TangibleDynamics::getBreathingSpeed() const      { return m_breathingSpeed; }

float TangibleDynamics::getBreathingDuration() const
{
	if (m_breathingDuration < 0.0f)
		return -1.0f;
	return m_breathingDuration - m_breathingElapsed;
}

//===================================================================
// BOUNCE (gravity + elasticity)
//===================================================================

void TangibleDynamics::setBounceEffect(float gravity, float elasticity, float initialUpVelocity, float duration)
{
	Object* const owner = getOwner();

	m_bounceGravity = gravity;
	m_bounceElasticity = clamp(0.0f, elasticity, 1.0f);
	m_bounceVerticalVelocity = initialUpVelocity;
	m_bounceFloorY = owner ? owner->getPosition_w().y : 0.0f;
	m_bounceDuration = duration;
	m_bounceElapsed = 0.0f;
	m_bounceEffectActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearBounceEffect()
{
	m_bounceGravity = 9.8f;
	m_bounceElasticity = 0.6f;
	m_bounceVerticalVelocity = 0.0f;
	m_bounceDuration = -1.0f;
	m_bounceElapsed = 0.0f;
	m_bounceEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

float TangibleDynamics::getBounceGravity() const     { return m_bounceGravity; }
float TangibleDynamics::getBounceElasticity() const  { return m_bounceElasticity; }
float TangibleDynamics::getBounceFloorY() const      { return m_bounceFloorY; }
float TangibleDynamics::getBounceVerticalVelocity() const { return m_bounceVerticalVelocity; }

//===================================================================
// WOBBLE (sinusoidal position oscillation)
//===================================================================

void TangibleDynamics::setWobbleEffect(const Vector& amplitude, const Vector& frequency, float duration)
{
	Object* const owner = getOwner();

	m_wobbleAmplitude = amplitude;
	m_wobbleFrequency = frequency;
	m_wobbleDuration = duration;
	m_wobbleElapsed = 0.0f;
	m_wobblePhase = 0.0f;
	m_wobbleOrigin = owner ? owner->getPosition_w() : Vector::zero;
	m_wobbleEffectActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearWobbleEffect()
{
	Object* const owner = getOwner();
	if (owner != NULL && m_wobbleEffectActive)
	{
		owner->setPosition_w(m_wobbleOrigin);
	}

	m_wobbleAmplitude = Vector::zero;
	m_wobbleFrequency = Vector::zero;
	m_wobbleDuration = -1.0f;
	m_wobbleElapsed = 0.0f;
	m_wobblePhase = 0.0f;
	m_wobbleEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

Vector TangibleDynamics::getWobbleAmplitude() const   { return m_wobbleAmplitude; }
Vector TangibleDynamics::getWobbleFrequency() const   { return m_wobbleFrequency; }
float  TangibleDynamics::getWobblePhase() const       { return m_wobblePhase; }
Vector TangibleDynamics::getWobbleOrigin() const      { return m_wobbleOrigin; }

//===================================================================
// ORBIT (circular motion around point)
//===================================================================

void TangibleDynamics::setOrbitEffect(const Vector& center, float radius, float radiansPerSecond, float duration)
{
	Object* const owner = getOwner();

	m_orbitCenter = center;
	m_orbitRadius = radius;
	m_orbitSpeed = radiansPerSecond;
	m_orbitDuration = duration;
	m_orbitElapsed = 0.0f;
	m_orbitEffectActive = true;

	// Compute initial angle from current position
	if (owner != NULL)
	{
		Vector delta = owner->getPosition_w() - center;
		m_orbitAngle = atan2(delta.x, delta.z);
	}
	else
	{
		m_orbitAngle = 0.0f;
	}

	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearOrbitEffect()
{
	m_orbitCenter = Vector::zero;
	m_orbitRadius = 1.0f;
	m_orbitSpeed = 1.0f;
	m_orbitAngle = 0.0f;
	m_orbitDuration = -1.0f;
	m_orbitElapsed = 0.0f;
	m_orbitEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

Vector TangibleDynamics::getOrbitCenter() const  { return m_orbitCenter; }
float  TangibleDynamics::getOrbitRadius() const  { return m_orbitRadius; }
float  TangibleDynamics::getOrbitAngle() const   { return m_orbitAngle; }

//===================================================================
// HOVER (terrain-following with bob)
//===================================================================

void TangibleDynamics::setHoverEffect(float hoverHeight, float bobAmplitude, float bobSpeed, float duration)
{
	m_hoverHeight = hoverHeight;
	m_hoverBobAmplitude = bobAmplitude;
	m_hoverBobSpeed = bobSpeed;
	m_hoverBobPhase = 0.0f;
	m_hoverDuration = duration;
	m_hoverElapsed = 0.0f;
	m_hoverEffectActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearHoverEffect()
{
	m_hoverHeight = 1.0f;
	m_hoverBobAmplitude = 0.1f;
	m_hoverBobSpeed = 1.0f;
	m_hoverBobPhase = 0.0f;
	m_hoverDuration = -1.0f;
	m_hoverElapsed = 0.0f;
	m_hoverEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

float TangibleDynamics::getHoverHeight() const       { return m_hoverHeight; }
float TangibleDynamics::getHoverBobAmplitude() const { return m_hoverBobAmplitude; }
float TangibleDynamics::getHoverBobSpeed() const     { return m_hoverBobSpeed; }

//===================================================================
// FOLLOW TARGET (hover + follow object + match rotation)
//===================================================================

void TangibleDynamics::setFollowTargetEffect(uint64 targetNetworkId, float followDistance, float followSpeed,
	float hoverHeight, float bobAmplitude, float duration)
{
	m_followTargetId = targetNetworkId;
	m_followDistance = followDistance;
	m_followSpeed = followSpeed;
	m_followHoverHeight = hoverHeight;
	m_followBobAmplitude = bobAmplitude;
	m_followBobPhase = 0.0f;
	m_followDuration = duration;
	m_followElapsed = 0.0f;
	m_followTargetEffectActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearFollowTargetEffect()
{
	m_followTargetId = 0;
	m_followDistance = 2.0f;
	m_followSpeed = 3.0f;
	m_followHoverHeight = 1.0f;
	m_followBobAmplitude = 0.05f;
	m_followBobPhase = 0.0f;
	m_followDuration = -1.0f;
	m_followElapsed = 0.0f;
	m_followTargetEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

uint64 TangibleDynamics::getFollowTargetId() const  { return m_followTargetId; }
float  TangibleDynamics::getFollowDistance() const  { return m_followDistance; }
float  TangibleDynamics::getFollowSpeed() const     { return m_followSpeed; }
float  TangibleDynamics::getFollowHoverHeight() const    { return m_followHoverHeight; }
float  TangibleDynamics::getFollowBobAmplitude() const   { return m_followBobAmplitude; }

//===================================================================
// EASING
//===================================================================

void TangibleDynamics::setEasing(EaseType easeType, float easeDuration)
{
	m_easeType = easeType;
	m_easeDuration = easeDuration;
}

//===================================================================
// COMBINED
//===================================================================

void TangibleDynamics::setCombinedForces(const Vector& pushVelocity, const Vector& spinAngular,
	float minScale, float maxScale, float breatheSpeed, float duration)
{
	setPushForce(pushVelocity, duration, MS_world);
	setSpinForce(spinAngular, duration);
	setBreathingEffect(minScale, maxScale, breatheSpeed, duration);
}

//-------------------------------------------------------------------

void TangibleDynamics::clearAllForces()
{
	clearPushForce();
	clearSpinForce();
	clearBreathingEffect();
	clearBounceEffect();
	clearWobbleEffect();
	clearOrbitEffect();
	clearHoverEffect();
	clearFollowTargetEffect();
}

//===================================================================
// QUERY
//===================================================================

int TangibleDynamics::getActiveForceMask() const
{
	return m_activeForceMask;
}

//-------------------------------------------------------------------

bool TangibleDynamics::isActive() const
{
	return m_activeForceMask != FM_none;
}

//-------------------------------------------------------------------

bool TangibleDynamics::isForceActive(ForceMode mode) const
{
	return (m_activeForceMask & static_cast<int>(mode)) != 0;
}

//===================================================================
// ALTER
//===================================================================

float TangibleDynamics::alter(float elapsedTime)
{
	float const baseAlterResult = SimpleDynamics::alter(elapsedTime);

	if (m_activeForceMask != FM_none && elapsedTime > s_minimumElapsedTime)
	{
		realAlter(elapsedTime);
		return AlterResult::cms_alterNextFrame;
	}

	if (m_activeForceMask != FM_none)
		return AlterResult::cms_alterNextFrame;

	return baseAlterResult;
}

//-------------------------------------------------------------------

void TangibleDynamics::realAlter(float elapsedTime)
{
	if (m_pushForceActive)
		updatePushForce(elapsedTime);

	if (m_spinForceActive)
		updateSpinForce(elapsedTime);

	if (m_breathingEffectActive)
		updateBreathingEffect(elapsedTime);

	if (m_bounceEffectActive)
		updateBounceEffect(elapsedTime);

	if (m_wobbleEffectActive)
		updateWobbleEffect(elapsedTime);

	if (m_orbitEffectActive)
		updateOrbitEffect(elapsedTime);

	if (m_hoverEffectActive)
		updateHoverEffect(elapsedTime);

	if (m_followTargetEffectActive)
		updateFollowTargetEffect(elapsedTime);
}

//===================================================================
// UPDATE HELPERS
//===================================================================

void TangibleDynamics::updatePushForce(float elapsedTime)
{
	Object* const owner = getOwner();
	if (owner == NULL)
	{
		clearPushForce();
		return;
	}

	// Duration check
	if (m_pushDuration >= 0.0f)
	{
		m_pushElapsed += elapsedTime;
		if (m_pushElapsed >= m_pushDuration)
		{
			clearPushForce();
			return;
		}
	}

	// Apply drag (exponential decay) - this slows down the velocity over time
	if (m_pushDrag > 0.0f)
	{
		float const dragFactor = exp(-m_pushDrag * elapsedTime);
		m_pushVelocity *= dragFactor;

		// Stop if velocity is negligible (soft termination)
		float const speedSquared = m_pushVelocity.magnitudeSquared();
		if (speedSquared < 0.01f)  // ~0.1 m/s threshold
		{
			clearPushForce();
			return;
		}
	}

	// Note: Position updates are handled by TangibleObject::updateHockeyPuckPhysics()
	// which applies terrain following and collision detection.
	// We don't use setCurrentVelocity here because that would conflict with
	// direct position manipulation for hockey puck mode.
}

//-------------------------------------------------------------------

void TangibleDynamics::updateSpinForce(float elapsedTime)
{
	Object* const owner = getOwner();
	if (owner == NULL)
	{
		clearSpinForce();
		return;
	}

	if (m_spinDuration >= 0.0f)
	{
		m_spinElapsed += elapsedTime;
		if (m_spinElapsed >= m_spinDuration)
		{
			clearSpinForce();
			return;
		}
	}

	// Server-side rotation update - keep rotation in sync with client
	Transform ownerTransform = owner->getTransform_o2w();

	// Apply yaw rotation (most common)
	if (fabs(m_spinAngular.y) > 0.001f)
	{
		ownerTransform.yaw_l(m_spinAngular.y * elapsedTime);
	}
	// Apply pitch rotation
	if (fabs(m_spinAngular.x) > 0.001f)
	{
		ownerTransform.pitch_l(m_spinAngular.x * elapsedTime);
	}
	// Apply roll rotation
	if (fabs(m_spinAngular.z) > 0.001f)
	{
		ownerTransform.roll_l(m_spinAngular.z * elapsedTime);
	}

	owner->setTransform_o2w(ownerTransform);
}

//-------------------------------------------------------------------

void TangibleDynamics::updateBreathingEffect(float elapsedTime)
{
	// Server-side: only track timing for duration expiration
	// Actual scale animation is handled client-side for smooth visuals

	if (m_breathingDuration >= 0.0f)
	{
		m_breathingElapsed += elapsedTime;
		if (m_breathingElapsed >= m_breathingDuration)
		{
			clearBreathingEffect();
			return;
		}
	}

	// Update phase for sync tracking (but don't apply scale here)
	m_breathingPhase += elapsedTime * m_breathingSpeed;

	// Note: Scale changes are intentionally NOT applied server-side
	// to prevent choppy updates. Client receives breathing params via
	// CM_tangibleDynamicsData and renders smoothly at frame rate.
}

//-------------------------------------------------------------------

void TangibleDynamics::updateBounceEffect(float elapsedTime)
{
	Object* const owner = getOwner();
	if (owner == NULL)
	{
		clearBounceEffect();
		return;
	}

	if (m_bounceDuration >= 0.0f)
	{
		m_bounceElapsed += elapsedTime;
		if (m_bounceElapsed >= m_bounceDuration)
		{
			clearBounceEffect();
			return;
		}
	}

	// Apply gravity to velocity
	m_bounceVerticalVelocity -= m_bounceGravity * elapsedTime;

	// Get current position
	Vector pos = owner->getPosition_w();
	pos.y += m_bounceVerticalVelocity * elapsedTime;

	// Floor collision
	if (pos.y <= m_bounceFloorY)
	{
		pos.y = m_bounceFloorY;
		m_bounceVerticalVelocity = -m_bounceVerticalVelocity * m_bounceElasticity;

		if (fabs(m_bounceVerticalVelocity) < s_bounceMinVelocity)
		{
			clearBounceEffect();
			return;
		}
	}

	// Set the position - this keeps the server in sync
	owner->setPosition_w(pos);
}

//-------------------------------------------------------------------

void TangibleDynamics::updateWobbleEffect(float elapsedTime)
{
	Object* const owner = getOwner();
	if (owner == NULL)
	{
		clearWobbleEffect();
		return;
	}

	if (m_wobbleDuration >= 0.0f)
	{
		m_wobbleElapsed += elapsedTime;
		if (m_wobbleElapsed >= m_wobbleDuration)
		{
			clearWobbleEffect();
			return;
		}
	}

	// Update phase
	m_wobblePhase += elapsedTime;

	// Server-side position update - keep position in sync with client
	float const ease = computeEaseFactor(m_easeType, m_wobbleElapsed, m_wobbleDuration, m_easeDuration);

	float const ox = m_wobbleAmplitude.x * sin(m_wobbleFrequency.x * m_wobblePhase * PI_TIMES_2) * ease;
	float const oy = m_wobbleAmplitude.y * sin(m_wobbleFrequency.y * m_wobblePhase * PI_TIMES_2) * ease;
	float const oz = m_wobbleAmplitude.z * sin(m_wobbleFrequency.z * m_wobblePhase * PI_TIMES_2) * ease;

	Vector const basePos = m_wobbleOrigin;
	owner->setPosition_w(Vector(basePos.x + ox, basePos.y + oy, basePos.z + oz));
}

//-------------------------------------------------------------------

void TangibleDynamics::updateOrbitEffect(float elapsedTime)
{
	Object* const owner = getOwner();
	if (owner == NULL)
	{
		clearOrbitEffect();
		return;
	}

	if (m_orbitDuration >= 0.0f)
	{
		m_orbitElapsed += elapsedTime;
		if (m_orbitElapsed >= m_orbitDuration)
		{
			clearOrbitEffect();
			return;
		}
	}

	// Update angle
	float const ease = computeEaseFactor(m_easeType, m_orbitElapsed, m_orbitDuration, m_easeDuration);
	m_orbitAngle += m_orbitSpeed * elapsedTime * ease;

	// Server-side position update - keep position in sync with client
	// Calculate position on circular orbit around center
	float const newX = m_orbitCenter.x + m_orbitRadius * cos(m_orbitAngle);
	float const newZ = m_orbitCenter.z + m_orbitRadius * sin(m_orbitAngle);

	// Set the position - Y stays at orbit center height
	owner->setPosition_w(Vector(newX, m_orbitCenter.y, newZ));
}

//-------------------------------------------------------------------

void TangibleDynamics::updateHoverEffect(float elapsedTime)
{
	Object* const owner = getOwner();
	if (owner == NULL)
	{
		clearHoverEffect();
		return;
	}

	if (m_hoverDuration >= 0.0f)
	{
		m_hoverElapsed += elapsedTime;
		if (m_hoverElapsed >= m_hoverDuration)
		{
			clearHoverEffect();
			return;
		}
	}

	// Update phase for bob calculation
	m_hoverBobPhase += elapsedTime * m_hoverBobSpeed;

	// Server-side position update - keep position in sync with client
	Vector const ownerPos = owner->getPosition_w();

	// Get terrain height at current position
	float terrainHeight = ownerPos.y;
	TerrainObject const * const terrain = TerrainObject::getConstInstance();
	if (terrain)
	{
		Vector queryPos(ownerPos.x, 0.0f, ownerPos.z);
		terrain->getHeight(queryPos, terrainHeight);
	}

	// Calculate hover position (no bob on server - let client do smooth visual bob)
	float const desiredY = terrainHeight + m_hoverHeight;

	// Smooth vertical interpolation
	float const smoothFactor = 5.0f;
	float const lerpFactor = 1.0f - exp(-smoothFactor * elapsedTime);
	float const newY = ownerPos.y + (desiredY - ownerPos.y) * lerpFactor;

	// Set the position - this keeps the server in sync
	owner->setPosition_w(Vector(ownerPos.x, newY, ownerPos.z));
}

//-------------------------------------------------------------------

void TangibleDynamics::updateFollowTargetEffect(float elapsedTime)
{
	Object* const owner = getOwner();
	if (owner == NULL)
	{
		clearFollowTargetEffect();
		return;
	}

	// Duration check
	if (m_followDuration >= 0.0f)
	{
		m_followElapsed += elapsedTime;
		if (m_followElapsed >= m_followDuration)
		{
			clearFollowTargetEffect();
			return;
		}
	}

	// Validate target still exists
	if (m_followTargetId == 0)
	{
		clearFollowTargetEffect();
		return;
	}

	Object const * const target = NetworkIdManager::getObjectById(NetworkId(static_cast<NetworkId::NetworkIdType>(m_followTargetId)));
	if (!target)
	{
		clearFollowTargetEffect();
		return;
	}

	// Update phase for bob calculation
	m_followBobPhase += elapsedTime * 1.0f;

	// Server-side position calculation - keep position in sync with client
	// This ensures getLocation() returns the correct position for Java scripts
	Vector const targetPos = target->getPosition_w();
	Vector const ownerPos = owner->getPosition_w();

	// Get target's facing direction
	Transform const & targetTransform = target->getTransform_o2w();
	Vector const targetFacing = targetTransform.getLocalFrameK_p();

	// Calculate desired position behind the target at follow distance
	Vector desiredPos;
	desiredPos.x = targetPos.x - targetFacing.x * m_followDistance;
	desiredPos.z = targetPos.z - targetFacing.z * m_followDistance;

	// Apply hover height (no bob on server - let client do the visual smoothing)
	desiredPos.y = targetPos.y + m_followHoverHeight;

	// Use smooth exponential interpolation matching client
	float const positionSmoothFactor = 5.0f;
	float const posLerpFactor = 1.0f - exp(-positionSmoothFactor * elapsedTime);

	// Smooth position interpolation
	Vector newPos;
	newPos.x = ownerPos.x + (desiredPos.x - ownerPos.x) * posLerpFactor;
	newPos.y = ownerPos.y + (desiredPos.y - ownerPos.y) * posLerpFactor;
	newPos.z = ownerPos.z + (desiredPos.z - ownerPos.z) * posLerpFactor;

	// Set the position - this keeps the server in sync
	owner->setPosition_w(newPos);

	// Update rotation to face same direction as target (simpler than client - no lerp needed server-side)
	float const rotationSmoothFactor = 8.0f;
	float const rotLerpFactor = 1.0f - exp(-rotationSmoothFactor * elapsedTime);

	Transform ownerTransform = owner->getTransform_o2w();
	Vector const currentFacing = ownerTransform.getLocalFrameK_p();

	Vector newFacing;
	newFacing.x = currentFacing.x + (targetFacing.x - currentFacing.x) * rotLerpFactor;
	newFacing.y = 0.0f;
	newFacing.z = currentFacing.z + (targetFacing.z - currentFacing.z) * rotLerpFactor;

	float const facingMag = sqrt(newFacing.x * newFacing.x + newFacing.z * newFacing.z);
	if (facingMag > 0.001f)
	{
		newFacing.x /= facingMag;
		newFacing.z /= facingMag;
		ownerTransform.setLocalFrameKJ_p(newFacing, Vector::unitY);
		owner->setTransform_o2w(ownerTransform);
	}
}

//===================================================================
// RECALCULATE
//===================================================================

void TangibleDynamics::recalculateMode()
{
	m_activeForceMask = FM_none;

	if (m_pushForceActive)         m_activeForceMask |= FM_push;
	if (m_spinForceActive)         m_activeForceMask |= FM_spin;
	if (m_breathingEffectActive)   m_activeForceMask |= FM_breathing;
	if (m_bounceEffectActive)      m_activeForceMask |= FM_bounce;
	if (m_wobbleEffectActive)      m_activeForceMask |= FM_wobble;
	if (m_orbitEffectActive)       m_activeForceMask |= FM_orbit;
	if (m_hoverEffectActive)       m_activeForceMask |= FM_hover;
	if (m_followTargetEffectActive) m_activeForceMask |= FM_followTarget;
}

//===================================================================
