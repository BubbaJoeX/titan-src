//===================================================================
//
// TangibleDynamics.h
// Copyright 2025, 2026 Titan SWG, All Rights Reserved
//
// Full physics simulation for tangible objects:
// - Push/Shove forces (linear velocity with optional drag)
// - Spinning/Rotation (angular velocity)
// - Breathing effects (sinusoidal scale pulsing)
// - Bounce (gravity + elasticity)
// - Wobble (sinusoidal oscillation on position axes)
// - Orbit (circular motion around a point)
// Forces can be applied individually or in combination,
// each with independent duration, easing, and control.
// Activated via condition MAGIC_TANGIBLE_DYNAMIC.
//
//===================================================================

#ifndef INCLUDED_TangibleDynamics_H
#define INCLUDED_TangibleDynamics_H

//===================================================================

#include "sharedObject/SimpleDynamics.h"
#include "sharedMath/Vector.h"
#include "sharedFoundation/Timer.h"

//===================================================================

class TangibleDynamics : public SimpleDynamics
{
public:

	// --- Enums ---

	enum ForceMode
	{
		FM_none         = 0,
		FM_push         = (1 << 0),
		FM_spin         = (1 << 1),
		FM_breathing    = (1 << 2),
		FM_bounce       = (1 << 3),
		FM_wobble       = (1 << 4),
		FM_orbit        = (1 << 5),
		FM_hover        = (1 << 6),
		FM_followTarget = (1 << 7),
		FM_sway         = (1 << 8),
		FM_shake        = (1 << 9),
		FM_float        = (1 << 10),
		FM_conveyor     = (1 << 11),
		FM_carousel     = (1 << 12),
		FM_lockToParent = (1 << 13)
	};

	enum MovementSpace
	{
		MS_world,
		MS_parent,
		MS_object
	};

	enum EaseType
	{
		ET_none,
		ET_easeIn,
		ET_easeOut,
		ET_easeInOut
	};

	// --- Constructor / Destructor ---

	explicit TangibleDynamics(Object* owner);
	virtual ~TangibleDynamics();

	// --- Push/Shove ---

	void   setPushForce(const Vector& velocity, float duration = -1.0f, MovementSpace space = MS_world);
	void   setPushForceWithDrag(const Vector& velocity, float drag, float duration = -1.0f, MovementSpace space = MS_world);
	void   clearPushForce();
	Vector getPushForce() const;
	float  getPushForceDuration() const;
	void   setPushDrag(float drag);
	float  getPushDrag() const;

	// --- Spin/Rotation ---

	void   setSpinForce(const Vector& rotationRadiansPerSecond, float duration = -1.0f);
	void   clearSpinForce();
	Vector getSpinForce() const;
	float  getSpinForceDuration() const;
	bool   getSpinAroundAppearanceCenter() const;
	void   setSpinAroundAppearanceCenter(bool spinAroundAppearanceCenter);

	// --- Breathing/Pulsing (sinusoidal) ---

	void  setBreathingEffect(float minimumScale, float maximumScale, float speed, float duration = -1.0f);
	void  clearBreathingEffect();
	float getBreathingMinScale() const;
	float getBreathingMaxScale() const;
	float getBreathingSpeed() const;
	float getBreathingDuration() const;

	// --- Bounce (gravity + elasticity) ---

	void  setBounceEffect(float gravity, float elasticity, float initialUpVelocity, float duration = -1.0f);
	void  clearBounceEffect();
	float getBounceGravity() const;
	float getBounceElasticity() const;
	float getBounceFloorY() const;
	float getBounceVerticalVelocity() const;

	// --- Wobble (sinusoidal position oscillation) ---

	void   setWobbleEffect(const Vector& amplitude, const Vector& frequency, float duration = -1.0f);
	void   clearWobbleEffect();
	Vector getWobbleAmplitude() const;
	Vector getWobbleFrequency() const;
	float  getWobblePhase() const;
	Vector getWobbleOrigin() const;

	// --- Orbit (circular motion) ---

	void   setOrbitEffect(const Vector& center, float radius, float radiansPerSecond, float duration = -1.0f);
	void   clearOrbitEffect();
	Vector getOrbitCenter() const;
	float  getOrbitRadius() const;
	float  getOrbitAngle() const;

	// --- Hover (terrain-following with slight bob) ---

	void  setHoverEffect(float hoverHeight, float bobAmplitude = 0.1f, float bobSpeed = 1.0f, float duration = -1.0f);
	void  clearHoverEffect();
	float getHoverHeight() const;
	float getHoverBobAmplitude() const;
	float getHoverBobSpeed() const;

	// --- Follow Target (hover + follow another object, matching rotation) ---

	void   setFollowTargetEffect(uint64 targetNetworkId, float followDistance, float followSpeed,
	                            float hoverHeight = 1.0f, float bobAmplitude = 0.05f, float duration = -1.0f);
	void   clearFollowTargetEffect();
	uint64 getFollowTargetId() const;
	float  getFollowDistance() const;
	float  getFollowSpeed() const;
	float  getFollowHoverHeight() const;
	float  getFollowBobAmplitude() const;

	// --- Lock To Parent (rigid attachment to parent object with fixed offset) ---

	void   setLockToParentEffect(uint64 parentNetworkId, const Vector& positionOffset,
	                             const Vector& rotationOffset, bool matchRotation = true, float duration = -1.0f);
	void   clearLockToParentEffect();
	uint64 getLockToParentId() const;
	Vector getLockToParentPositionOffset() const;
	Vector getLockToParentRotationOffset() const;
	bool   getLockToParentMatchRotation() const;
	bool   isLockToParentActive() const;

	// --- Sway/Pendulum (swinging back and forth like a hanging sign) ---

	void   setSwayEffect(float swingAngle, float swingSpeed, float damping = 0.0f, float duration = -1.0f);
	void   clearSwayEffect();
	float  getSwayAngle() const;
	float  getSwaySpeed() const;
	float  getSwayDamping() const;
	float  getSwayPhase() const;

	// --- Shake/Vibrate (rapid small position offsets for emphasis) ---

	void   setShakeEffect(float intensity, float frequency, float duration = -1.0f);
	void   clearShakeEffect();
	float  getShakeIntensity() const;
	float  getShakeFrequency() const;
	Vector getShakeOrigin() const;

	// --- Float/Levitate (slow drift up and down with slight random movement) ---

	void   setFloatEffect(float floatHeight, float driftSpeed, float randomStrength = 0.1f, float duration = -1.0f);
	void   clearFloatEffect();
	float  getFloatHeight() const;
	float  getFloatDriftSpeed() const;
	float  getFloatRandomStrength() const;
	Vector getFloatOrigin() const;

	// --- Conveyor (continuous linear movement with optional wrap) ---

	void   setConveyorEffect(const Vector& direction, float speed, float wrapDistance = 0.0f, float duration = -1.0f);
	void   clearConveyorEffect();
	Vector getConveyorDirection() const;
	float  getConveyorSpeed() const;
	float  getConveyorWrapDistance() const;
	Vector getConveyorOrigin() const;
	float  getConveyorTravelDistance() const;

	// --- Carousel (rotating platform with vertical oscillation) ---

	void   setCarouselEffect(const Vector& center, float radius, float rotationSpeed, float verticalAmplitude = 0.0f, float verticalSpeed = 1.0f, float duration = -1.0f);
	void   clearCarouselEffect();
	Vector getCarouselCenter() const;
	float  getCarouselRadius() const;
	float  getCarouselRotationSpeed() const;
	float  getCarouselAngle() const;
	float  getCarouselVerticalAmplitude() const;
	float  getCarouselVerticalSpeed() const;

	// --- Easing ---

	void setEasing(EaseType easeType, float easeDuration);

	// --- Combined forces ---

	void setCombinedForces(const Vector& pushVelocity, const Vector& spinAngular,
		float minScale, float maxScale, float breatheSpeed, float duration = -1.0f);
	void clearAllForces();

	// --- Query ---

	int  getActiveForceMask() const;
	bool isActive() const;
	bool isForceActive(ForceMode mode) const;

	// --- Alter ---

	virtual float alter(float elapsedTime);

protected:

	virtual void realAlter(float elapsedTime);

private:

	// disable
	TangibleDynamics();
	TangibleDynamics(const TangibleDynamics&);
	TangibleDynamics& operator=(const TangibleDynamics&);

	// --- Static helpers ---
	static float computeEaseFactor(EaseType easeType, float elapsed, float duration, float easeDuration);

	// --- Push state ---
	Vector        m_pushVelocity;
	float         m_pushDuration;
	float         m_pushElapsed;
	float         m_pushDrag;
	MovementSpace m_pushSpace;
	bool          m_pushForceActive;

	// --- Spin state ---
	Vector m_spinAngular;
	float  m_spinDuration;
	float  m_spinElapsed;
	bool   m_spinForceActive;
	bool   m_spinAroundAppearanceCenter;

	// --- Breathing state ---
	Vector m_baseScale;
	float  m_breathingMin;
	float  m_breathingMax;
	float  m_breathingSpeed;
	float  m_breathingDuration;
	float  m_breathingElapsed;
	float  m_breathingPhase;
	bool   m_breathingEffectActive;

	// --- Bounce state ---
	float  m_bounceGravity;
	float  m_bounceElasticity;
	float  m_bounceVerticalVelocity;
	float  m_bounceFloorY;
	float  m_bounceDuration;
	float  m_bounceElapsed;
	bool   m_bounceEffectActive;

	// --- Wobble state ---
	Vector m_wobbleAmplitude;
	Vector m_wobbleFrequency;
	float  m_wobbleDuration;
	float  m_wobbleElapsed;
	float  m_wobblePhase;
	Vector m_wobbleOrigin;
	bool   m_wobbleEffectActive;

	// --- Orbit state ---
	Vector m_orbitCenter;
	float  m_orbitRadius;
	float  m_orbitSpeed;
	float  m_orbitAngle;
	float  m_orbitDuration;
	float  m_orbitElapsed;
	bool   m_orbitEffectActive;

	// --- Hover state ---
	float  m_hoverHeight;
	float  m_hoverBobAmplitude;
	float  m_hoverBobSpeed;
	float  m_hoverBobPhase;
	float  m_hoverDuration;
	float  m_hoverElapsed;
	float  m_hoverUpdateAccumulator;
	bool   m_hoverEffectActive;

	// --- Follow Target state ---
	uint64 m_followTargetId;
	float  m_followDistance;
	float  m_followSpeed;
	float  m_followHoverHeight;
	float  m_followBobAmplitude;
	float  m_followBobPhase;
	float  m_followDuration;
	float  m_followElapsed;
	float  m_followUpdateAccumulator;
	bool   m_followTargetEffectActive;

	// --- Lock To Parent state ---
	uint64 m_lockToParentId;
	Vector m_lockToParentPositionOffset;
	Vector m_lockToParentRotationOffset;
	bool   m_lockToParentMatchRotation;
	float  m_lockToParentDuration;
	float  m_lockToParentElapsed;
	bool   m_lockToParentEffectActive;

	// --- Sway state ---
	float  m_swayAngle;
	float  m_swaySpeed;
	float  m_swayDamping;
	float  m_swayPhase;
	float  m_swayCurrentAngle;
	float  m_swayDuration;
	float  m_swayElapsed;
	bool   m_swayEffectActive;

	// --- Shake state ---
	float  m_shakeIntensity;
	float  m_shakeFrequency;
	Vector m_shakeOrigin;
	float  m_shakeDuration;
	float  m_shakeElapsed;
	float  m_shakePhase;
	bool   m_shakeEffectActive;

	// --- Float state ---
	float  m_floatHeight;
	float  m_floatDriftSpeed;
	float  m_floatRandomStrength;
	Vector m_floatOrigin;
	float  m_floatDuration;
	float  m_floatElapsed;
	float  m_floatPhase;
	float  m_floatRandomOffset;
	bool   m_floatEffectActive;

	// --- Conveyor state ---
	Vector m_conveyorDirection;
	float  m_conveyorSpeed;
	float  m_conveyorWrapDistance;
	Vector m_conveyorOrigin;
	float  m_conveyorTravelDistance;
	float  m_conveyorDuration;
	float  m_conveyorElapsed;
	bool   m_conveyorEffectActive;

	// --- Carousel state ---
	Vector m_carouselCenter;
	float  m_carouselRadius;
	float  m_carouselRotationSpeed;
	float  m_carouselAngle;
	float  m_carouselVerticalAmplitude;
	float  m_carouselVerticalSpeed;
	float  m_carouselVerticalPhase;
	float  m_carouselDuration;
	float  m_carouselElapsed;
	bool   m_carouselEffectActive;

	// --- Easing ---
	EaseType m_easeType;
	float    m_easeDuration;

	// --- Overall ---
	int m_activeForceMask;

	// --- Update helpers ---
	void updatePushForce(float elapsedTime);
	void updateSpinForce(float elapsedTime);
	void updateBreathingEffect(float elapsedTime);
	void updateBounceEffect(float elapsedTime);
	void updateWobbleEffect(float elapsedTime);
	void updateOrbitEffect(float elapsedTime);
	void updateHoverEffect(float elapsedTime);
	void updateFollowTargetEffect(float elapsedTime);
	void updateLockToParentEffect(float elapsedTime);
	void updateSwayEffect(float elapsedTime);
	void updateShakeEffect(float elapsedTime);
	void updateFloatEffect(float elapsedTime);
	void updateConveyorEffect(float elapsedTime);
	void updateCarouselEffect(float elapsedTime);
	void recalculateMode();
};

//===================================================================

#endif
