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
	// Lock To Parent
	m_lockToParentId(0),
	m_lockToParentPositionOffset(Vector::zero),
	m_lockToParentRotationOffset(Vector::zero),
	m_lockToParentMatchRotation(true),
	m_lockToParentDuration(-1.0f),
	m_lockToParentElapsed(0.0f),
	m_lockToParentEffectActive(false),
	// Sway
	m_swayAngle(0.1f),
	m_swaySpeed(1.0f),
	m_swayDamping(0.0f),
	m_swayPhase(0.0f),
	m_swayCurrentAngle(0.0f),
	m_swayDuration(-1.0f),
	m_swayElapsed(0.0f),
	m_swayEffectActive(false),
	// Shake
	m_shakeIntensity(0.1f),
	m_shakeFrequency(10.0f),
	m_shakeOrigin(Vector::zero),
	m_shakeDuration(-1.0f),
	m_shakeElapsed(0.0f),
	m_shakePhase(0.0f),
	m_shakeEffectActive(false),
	// Float
	m_floatHeight(0.5f),
	m_floatDriftSpeed(0.5f),
	m_floatRandomStrength(0.1f),
	m_floatOrigin(Vector::zero),
	m_floatDuration(-1.0f),
	m_floatElapsed(0.0f),
	m_floatPhase(0.0f),
	m_floatRandomOffset(0.0f),
	m_floatEffectActive(false),
	// Conveyor
	m_conveyorDirection(Vector::unitZ),
	m_conveyorSpeed(1.0f),
	m_conveyorWrapDistance(0.0f),
	m_conveyorOrigin(Vector::zero),
	m_conveyorTravelDistance(0.0f),
	m_conveyorDuration(-1.0f),
	m_conveyorElapsed(0.0f),
	m_conveyorEffectActive(false),
	// Carousel
	m_carouselCenter(Vector::zero),
	m_carouselRadius(3.0f),
	m_carouselRotationSpeed(1.0f),
	m_carouselAngle(0.0f),
	m_carouselVerticalAmplitude(0.0f),
	m_carouselVerticalSpeed(1.0f),
	m_carouselVerticalPhase(0.0f),
	m_carouselDuration(-1.0f),
	m_carouselElapsed(0.0f),
	m_carouselEffectActive(false),
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
// LOCK TO PARENT (rigid attachment with fixed offset)
//===================================================================

void TangibleDynamics::setLockToParentEffect(uint64 parentNetworkId, const Vector& positionOffset,
                                             const Vector& rotationOffset, bool matchRotation, float duration)
{
	m_lockToParentId = parentNetworkId;
	m_lockToParentPositionOffset = positionOffset;
	m_lockToParentRotationOffset = rotationOffset;
	m_lockToParentMatchRotation = matchRotation;
	m_lockToParentDuration = duration;
	m_lockToParentElapsed = 0.0f;
	m_lockToParentEffectActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearLockToParentEffect()
{
	m_lockToParentId = 0;
	m_lockToParentPositionOffset = Vector::zero;
	m_lockToParentRotationOffset = Vector::zero;
	m_lockToParentMatchRotation = true;
	m_lockToParentDuration = -1.0f;
	m_lockToParentElapsed = 0.0f;
	m_lockToParentEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

uint64 TangibleDynamics::getLockToParentId() const               { return m_lockToParentId; }
Vector TangibleDynamics::getLockToParentPositionOffset() const   { return m_lockToParentPositionOffset; }
Vector TangibleDynamics::getLockToParentRotationOffset() const   { return m_lockToParentRotationOffset; }
bool   TangibleDynamics::getLockToParentMatchRotation() const    { return m_lockToParentMatchRotation; }
bool   TangibleDynamics::isLockToParentActive() const            { return m_lockToParentEffectActive; }

//===================================================================
// SWAY/PENDULUM (swinging back and forth)
//===================================================================

void TangibleDynamics::setSwayEffect(float swingAngle, float swingSpeed, float damping, float duration)
{
	m_swayAngle = swingAngle;
	m_swaySpeed = swingSpeed;
	m_swayDamping = damping;
	m_swayPhase = 0.0f;
	m_swayCurrentAngle = 0.0f;
	m_swayDuration = duration;
	m_swayElapsed = 0.0f;
	m_swayEffectActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearSwayEffect()
{
	m_swayAngle = 0.1f;
	m_swaySpeed = 1.0f;
	m_swayDamping = 0.0f;
	m_swayPhase = 0.0f;
	m_swayCurrentAngle = 0.0f;
	m_swayDuration = -1.0f;
	m_swayElapsed = 0.0f;
	m_swayEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

float TangibleDynamics::getSwayAngle() const    { return m_swayAngle; }
float TangibleDynamics::getSwaySpeed() const    { return m_swaySpeed; }
float TangibleDynamics::getSwayDamping() const  { return m_swayDamping; }
float TangibleDynamics::getSwayPhase() const    { return m_swayPhase; }

//===================================================================
// SHAKE/VIBRATE (rapid small position offsets)
//===================================================================

void TangibleDynamics::setShakeEffect(float intensity, float frequency, float duration)
{
	Object* const owner = getOwner();

	m_shakeIntensity = intensity;
	m_shakeFrequency = frequency;
	m_shakeOrigin = owner ? owner->getPosition_w() : Vector::zero;
	m_shakeDuration = duration;
	m_shakeElapsed = 0.0f;
	m_shakePhase = 0.0f;
	m_shakeEffectActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearShakeEffect()
{
	Object* const owner = getOwner();
	if (owner != NULL && m_shakeEffectActive)
	{
		owner->setPosition_w(m_shakeOrigin);
	}

	m_shakeIntensity = 0.1f;
	m_shakeFrequency = 10.0f;
	m_shakeOrigin = Vector::zero;
	m_shakeDuration = -1.0f;
	m_shakeElapsed = 0.0f;
	m_shakePhase = 0.0f;
	m_shakeEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

float  TangibleDynamics::getShakeIntensity() const  { return m_shakeIntensity; }
float  TangibleDynamics::getShakeFrequency() const  { return m_shakeFrequency; }
Vector TangibleDynamics::getShakeOrigin() const     { return m_shakeOrigin; }

//===================================================================
// FLOAT/LEVITATE (slow drift up and down with randomness)
//===================================================================

void TangibleDynamics::setFloatEffect(float floatHeight, float driftSpeed, float randomStrength, float duration)
{
	Object* const owner = getOwner();

	m_floatHeight = floatHeight;
	m_floatDriftSpeed = driftSpeed;
	m_floatRandomStrength = randomStrength;
	m_floatOrigin = owner ? owner->getPosition_w() : Vector::zero;
	m_floatDuration = duration;
	m_floatElapsed = 0.0f;
	m_floatPhase = 0.0f;
	m_floatRandomOffset = 0.0f;
	m_floatEffectActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearFloatEffect()
{
	Object* const owner = getOwner();
	if (owner != NULL && m_floatEffectActive)
	{
		owner->setPosition_w(m_floatOrigin);
	}

	m_floatHeight = 0.5f;
	m_floatDriftSpeed = 0.5f;
	m_floatRandomStrength = 0.1f;
	m_floatOrigin = Vector::zero;
	m_floatDuration = -1.0f;
	m_floatElapsed = 0.0f;
	m_floatPhase = 0.0f;
	m_floatRandomOffset = 0.0f;
	m_floatEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

float  TangibleDynamics::getFloatHeight() const          { return m_floatHeight; }
float  TangibleDynamics::getFloatDriftSpeed() const      { return m_floatDriftSpeed; }
float  TangibleDynamics::getFloatRandomStrength() const  { return m_floatRandomStrength; }
Vector TangibleDynamics::getFloatOrigin() const          { return m_floatOrigin; }

//===================================================================
// CONVEYOR (continuous linear movement with optional wrap)
//===================================================================

void TangibleDynamics::setConveyorEffect(const Vector& direction, float speed, float wrapDistance, float duration)
{
	Object* const owner = getOwner();

	// Normalize direction
	Vector normalizedDir = direction;
	if (normalizedDir.normalize())
	{
		m_conveyorDirection = normalizedDir;
	}
	else
	{
		m_conveyorDirection = Vector::unitZ;
	}

	m_conveyorSpeed = speed;
	m_conveyorWrapDistance = wrapDistance;
	m_conveyorOrigin = owner ? owner->getPosition_w() : Vector::zero;
	m_conveyorTravelDistance = 0.0f;
	m_conveyorDuration = duration;
	m_conveyorElapsed = 0.0f;
	m_conveyorEffectActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearConveyorEffect()
{
	m_conveyorDirection = Vector::unitZ;
	m_conveyorSpeed = 1.0f;
	m_conveyorWrapDistance = 0.0f;
	m_conveyorOrigin = Vector::zero;
	m_conveyorTravelDistance = 0.0f;
	m_conveyorDuration = -1.0f;
	m_conveyorElapsed = 0.0f;
	m_conveyorEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

Vector TangibleDynamics::getConveyorDirection() const     { return m_conveyorDirection; }
float  TangibleDynamics::getConveyorSpeed() const         { return m_conveyorSpeed; }
float  TangibleDynamics::getConveyorWrapDistance() const  { return m_conveyorWrapDistance; }
Vector TangibleDynamics::getConveyorOrigin() const        { return m_conveyorOrigin; }
float  TangibleDynamics::getConveyorTravelDistance() const { return m_conveyorTravelDistance; }

//===================================================================
// CAROUSEL (rotating platform with vertical oscillation like ferris wheel)
//===================================================================

void TangibleDynamics::setCarouselEffect(const Vector& center, float radius, float rotationSpeed, float verticalAmplitude, float verticalSpeed, float duration)
{
	Object* const owner = getOwner();

	m_carouselCenter = center;
	m_carouselRadius = radius;
	m_carouselRotationSpeed = rotationSpeed;
	m_carouselVerticalAmplitude = verticalAmplitude;
	m_carouselVerticalSpeed = verticalSpeed;

	// Calculate initial angle from object's current position
	if (owner)
	{
		Vector const pos = owner->getPosition_w();
		float const dx = pos.x - center.x;
		float const dz = pos.z - center.z;
		m_carouselAngle = atan2(dz, dx);
	}
	else
	{
		m_carouselAngle = 0.0f;
	}

	m_carouselVerticalPhase = 0.0f;
	m_carouselDuration = duration;
	m_carouselElapsed = 0.0f;
	m_carouselEffectActive = true;
	recalculateMode();
}

//-------------------------------------------------------------------

void TangibleDynamics::clearCarouselEffect()
{
	m_carouselCenter = Vector::zero;
	m_carouselRadius = 3.0f;
	m_carouselRotationSpeed = 1.0f;
	m_carouselAngle = 0.0f;
	m_carouselVerticalAmplitude = 0.0f;
	m_carouselVerticalSpeed = 1.0f;
	m_carouselVerticalPhase = 0.0f;
	m_carouselDuration = -1.0f;
	m_carouselElapsed = 0.0f;
	m_carouselEffectActive = false;
	recalculateMode();
}

//-------------------------------------------------------------------

Vector TangibleDynamics::getCarouselCenter() const            { return m_carouselCenter; }
float  TangibleDynamics::getCarouselRadius() const            { return m_carouselRadius; }
float  TangibleDynamics::getCarouselRotationSpeed() const     { return m_carouselRotationSpeed; }
float  TangibleDynamics::getCarouselAngle() const             { return m_carouselAngle; }
float  TangibleDynamics::getCarouselVerticalAmplitude() const { return m_carouselVerticalAmplitude; }
float  TangibleDynamics::getCarouselVerticalSpeed() const     { return m_carouselVerticalSpeed; }

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
	clearSwayEffect();
	clearShakeEffect();
	clearFloatEffect();
	clearConveyorEffect();
	clearCarouselEffect();
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

	if (m_lockToParentEffectActive)
		updateLockToParentEffect(elapsedTime);

	if (m_swayEffectActive)
		updateSwayEffect(elapsedTime);

	if (m_shakeEffectActive)
		updateShakeEffect(elapsedTime);

	if (m_floatEffectActive)
		updateFloatEffect(elapsedTime);

	if (m_conveyorEffectActive)
		updateConveyorEffect(elapsedTime);

	if (m_carouselEffectActive)
		updateCarouselEffect(elapsedTime);
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
	// Server-side: only track timing for duration expiration
	// Note: We DON'T call setTransform_o2w here
	// Client handles smooth visual rotation at frame rate
	// Server's TangibleObject::updateTangibleDynamicsPosition handles authoritative sync

	if (m_spinDuration >= 0.0f)
	{
		m_spinElapsed += elapsedTime;
		if (m_spinElapsed >= m_spinDuration)
		{
			clearSpinForce();
			return;
		}
	}
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
	if (m_bounceDuration >= 0.0f)
	{
		m_bounceElapsed += elapsedTime;
		if (m_bounceElapsed >= m_bounceDuration)
		{
			clearBounceEffect();
			return;
		}
	}

	// Apply gravity to velocity (track state for server sync)
	m_bounceVerticalVelocity -= m_bounceGravity * elapsedTime;

	// Simulate floor collision for state tracking
	// Note: We track the physics state but DON'T call setPosition_w here
	// Client handles smooth visual updates at frame rate
	// Server's TangibleObject::updateTangibleDynamicsPosition handles authoritative sync
	if (m_bounceVerticalVelocity < -m_bounceGravity * 2.0f)
	{
		// Approximate floor hit - reverse velocity with elasticity
		m_bounceVerticalVelocity = -m_bounceVerticalVelocity * m_bounceElasticity;

		if (fabs(m_bounceVerticalVelocity) < s_bounceMinVelocity)
		{
			clearBounceEffect();
			return;
		}
	}
}

//-------------------------------------------------------------------

void TangibleDynamics::updateWobbleEffect(float elapsedTime)
{
	if (m_wobbleDuration >= 0.0f)
	{
		m_wobbleElapsed += elapsedTime;
		if (m_wobbleElapsed >= m_wobbleDuration)
		{
			clearWobbleEffect();
			return;
		}
	}

	// Update phase for state tracking
	// Note: We DON'T call setPosition_w here
	// Client handles smooth visual updates at frame rate
	// Server's TangibleObject::updateTangibleDynamicsPosition handles authoritative sync
	m_wobblePhase += elapsedTime;
}

//-------------------------------------------------------------------

void TangibleDynamics::updateOrbitEffect(float elapsedTime)
{
	if (m_orbitDuration >= 0.0f)
	{
		m_orbitElapsed += elapsedTime;
		if (m_orbitElapsed >= m_orbitDuration)
		{
			clearOrbitEffect();
			return;
		}
	}

	// Update angle for state tracking
	// Note: We DON'T call setPosition_w here
	// Client handles smooth visual updates at frame rate
	// Server's TangibleObject::updateTangibleDynamicsPosition handles authoritative sync
	float const ease = computeEaseFactor(m_easeType, m_orbitElapsed, m_orbitDuration, m_easeDuration);
	m_orbitAngle += m_orbitSpeed * elapsedTime * ease;
}

//-------------------------------------------------------------------

void TangibleDynamics::updateHoverEffect(float elapsedTime)
{
	if (m_hoverDuration >= 0.0f)
	{
		m_hoverElapsed += elapsedTime;
		if (m_hoverElapsed >= m_hoverDuration)
		{
			clearHoverEffect();
			return;
		}
	}

	// Update phase for bob calculation (state tracking only)
	// Note: We DON'T call setPosition_w here
	// Client handles smooth visual updates at frame rate
	// Server's TangibleObject::updateTangibleDynamicsPosition handles authoritative sync
	m_hoverBobPhase += elapsedTime * m_hoverBobSpeed;
}

//-------------------------------------------------------------------

void TangibleDynamics::updateFollowTargetEffect(float elapsedTime)
{
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

	// Update phase for bob calculation (state tracking only)
	// Note: We DON'T call setPosition_w here
	// Client handles smooth visual updates at frame rate
	// Server's TangibleObject::updateTangibleDynamicsPosition handles authoritative sync
	m_followBobPhase += elapsedTime * 1.0f;
}

//-------------------------------------------------------------------

void TangibleDynamics::updateLockToParentEffect(float elapsedTime)
{
	// Duration check
	if (m_lockToParentDuration >= 0.0f)
	{
		m_lockToParentElapsed += elapsedTime;
		if (m_lockToParentElapsed >= m_lockToParentDuration)
		{
			clearLockToParentEffect();
			return;
		}
	}

	// Validate parent still exists
	if (m_lockToParentId == 0)
	{
		clearLockToParentEffect();
		return;
	}

	Object const * const parent = NetworkIdManager::getObjectById(NetworkId(static_cast<NetworkId::NetworkIdType>(m_lockToParentId)));
	if (!parent)
	{
		clearLockToParentEffect();
		return;
	}

	// Note: Actual position/rotation update is handled in TangibleObject::updateTangibleDynamicsPosition
	// This keeps the logic server-authoritative while allowing client-side smoothing
}

//-------------------------------------------------------------------

void TangibleDynamics::updateSwayEffect(float elapsedTime)
{
	if (m_swayDuration >= 0.0f)
	{
		m_swayElapsed += elapsedTime;
		if (m_swayElapsed >= m_swayDuration)
		{
			clearSwayEffect();
			return;
		}
	}

	// Update phase for pendulum motion (state tracking only)
	// Note: We DON'T call setTransform here
	// Client handles smooth visual updates at frame rate
	m_swayPhase += elapsedTime * m_swaySpeed;

	// Apply damping if set
	if (m_swayDamping > 0.0f)
	{
		float dampingFactor = exp(-m_swayDamping * m_swayElapsed);
		m_swayCurrentAngle = m_swayAngle * dampingFactor;
	}
	else
	{
		m_swayCurrentAngle = m_swayAngle;
	}
}

//-------------------------------------------------------------------

void TangibleDynamics::updateShakeEffect(float elapsedTime)
{
	if (m_shakeDuration >= 0.0f)
	{
		m_shakeElapsed += elapsedTime;
		if (m_shakeElapsed >= m_shakeDuration)
		{
			clearShakeEffect();
			return;
		}
	}

	// Update phase for shake calculation (state tracking only)
	// Note: We DON'T call setPosition_w here
	// Client handles smooth visual updates at frame rate
	m_shakePhase += elapsedTime * m_shakeFrequency;
}

//-------------------------------------------------------------------

void TangibleDynamics::updateFloatEffect(float elapsedTime)
{
	if (m_floatDuration >= 0.0f)
	{
		m_floatElapsed += elapsedTime;
		if (m_floatElapsed >= m_floatDuration)
		{
			clearFloatEffect();
			return;
		}
	}

	// Update phase for float calculation (state tracking only)
	// Note: We DON'T call setPosition_w here
	// Client handles smooth visual updates at frame rate
	m_floatPhase += elapsedTime * m_floatDriftSpeed;

	// Update random offset periodically for organic feel
	// This creates subtle horizontal drift
	if (m_floatRandomStrength > 0.0f)
	{
		// Simple pseudo-random based on phase
		m_floatRandomOffset = sin(m_floatPhase * 3.7f) * m_floatRandomStrength;
	}
}

//-------------------------------------------------------------------

void TangibleDynamics::updateConveyorEffect(float elapsedTime)
{
	if (m_conveyorDuration >= 0.0f)
	{
		m_conveyorElapsed += elapsedTime;
		if (m_conveyorElapsed >= m_conveyorDuration)
		{
			clearConveyorEffect();
			return;
		}
	}

	// Update travel distance (state tracking only)
	// Note: We DON'T call setPosition_w here
	// Client handles smooth visual updates at frame rate
	// Server's TangibleObject::updateTangibleDynamicsPosition handles authoritative sync
	float const distanceDelta = m_conveyorSpeed * elapsedTime;
	m_conveyorTravelDistance += distanceDelta;

	// Handle wrap-around if wrapDistance is set
	if (m_conveyorWrapDistance > 0.0f && m_conveyorTravelDistance >= m_conveyorWrapDistance)
	{
		// Reset travel distance (wraps back to origin)
		m_conveyorTravelDistance = fmod(m_conveyorTravelDistance, m_conveyorWrapDistance);
	}
}

//-------------------------------------------------------------------

void TangibleDynamics::updateCarouselEffect(float elapsedTime)
{
	if (m_carouselDuration >= 0.0f)
	{
		m_carouselElapsed += elapsedTime;
		if (m_carouselElapsed >= m_carouselDuration)
		{
			clearCarouselEffect();
			return;
		}
	}

	// Update rotation angle (state tracking)
	m_carouselAngle += m_carouselRotationSpeed * elapsedTime;

	// Update vertical phase for ferris wheel oscillation
	if (m_carouselVerticalAmplitude > 0.0f)
	{
		m_carouselVerticalPhase += m_carouselVerticalSpeed * elapsedTime;
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
	if (m_lockToParentEffectActive) m_activeForceMask |= FM_lockToParent;
	if (m_swayEffectActive)        m_activeForceMask |= FM_sway;
	if (m_shakeEffectActive)       m_activeForceMask |= FM_shake;
	if (m_floatEffectActive)       m_activeForceMask |= FM_float;
	if (m_conveyorEffectActive)    m_activeForceMask |= FM_conveyor;
	if (m_carouselEffectActive)    m_activeForceMask |= FM_carousel;
}

//===================================================================
