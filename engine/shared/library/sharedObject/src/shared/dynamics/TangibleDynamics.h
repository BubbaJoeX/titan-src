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
		FM_none       = 0,
		FM_push       = (1 << 0),
		FM_spin       = (1 << 1),
		FM_breathing  = (1 << 2),
		FM_bounce     = (1 << 3),
		FM_wobble     = (1 << 4),
		FM_orbit      = (1 << 5)
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

	// --- Wobble (sinusoidal position oscillation) ---

	void   setWobbleEffect(const Vector& amplitude, const Vector& frequency, float duration = -1.0f);
	void   clearWobbleEffect();
	Vector getWobbleAmplitude() const;
	Vector getWobbleFrequency() const;

	// --- Orbit (circular motion) ---

	void   setOrbitEffect(const Vector& center, float radius, float radiansPerSecond, float duration = -1.0f);
	void   clearOrbitEffect();
	Vector getOrbitCenter() const;
	float  getOrbitRadius() const;

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
	void recalculateMode();
};

//===================================================================

#endif
