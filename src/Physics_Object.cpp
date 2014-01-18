#include "StdAfx.h"

#include <cmodel.h>

#include "miscmath.h"
#include "Physics_Object.h"
#include "Physics_Environment.h"
#include "Physics_Collision.h"
#include "Physics_Constraint.h"
#include "Physics_FrictionSnapshot.h"
#include "Physics_ShadowController.h"
#include "Physics_DragController.h"
#include "Physics_SurfaceProps.h"
#include "convert.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

/*****************************
* CLASS CGhostTriggerCallback
* Purpose: For BecomeTrigger, etc.
* Tracks objects that enter/exit us.
*****************************/
class CGhostTriggerCallback : public btGhostObjectCallback {
	public:
		CGhostTriggerCallback(CPhysicsObject *pObject): m_pObject(pObject) {

		}

		void addedOverlappingObject(btCollisionObject *pObj) {
			if (!pObj) return;

			CPhysicsObject *pPhys = (CPhysicsObject *)pObj->getUserPointer();
			if (!pPhys) return;

			m_pObject->TriggerObjectEntered(pPhys);
		}

		void removedOverlappingObject(btCollisionObject *pObj) {
			if (!pObj) return;

			CPhysicsObject *pPhys = (CPhysicsObject *)pObj->getUserPointer();
			if (!pPhys) return;

			m_pObject->TriggerObjectExited(pPhys);
		}
	private:
		CPhysicsObject *m_pObject;
};

/***************************
* CLASS CPhysicsObject
***************************/

CPhysicsObject::CPhysicsObject() {
	m_contents = 0;
	m_iGameIndex = 0;
	m_pShadow = NULL;
	m_pFluidController = NULL;
	m_pEnv = NULL;
	m_pGameData = NULL;
	m_pObject = NULL;
	m_pGhostObject = NULL;
	m_pGhostCallback = NULL;
	m_pName = NULL;

	m_bRemoving = false;
}

CPhysicsObject::~CPhysicsObject() {
	m_bRemoving = true;

	if (m_pEnv) {
		RemoveShadowController();
		m_pEnv->GetDragController()->RemovePhysicsObject(this);

		if (m_pFluidController)
			m_pEnv->DestroyFluidController((IPhysicsFluidController *)m_pFluidController);
	}

	for (int i = 0; i < m_pConstraintVec.Count(); i++) {
		m_pConstraintVec[i]->ObjectDestroyed(this);
	}
	
	if (m_pEnv && m_pObject) {
		m_pEnv->GetBulletEnvironment()->removeRigidBody(m_pObject);

		// Sphere collision shape is allocated when we're a sphere. Delete it.
		if (m_bIsSphere)
			delete (btSphereShape *)m_pObject->getCollisionShape();

		delete m_pObject->getMotionState();
		delete m_pObject;
	}
}

bool CPhysicsObject::IsStatic() const {
	return (m_pObject->getCollisionFlags() & btCollisionObject::CF_STATIC_OBJECT);
}

bool CPhysicsObject::IsAsleep() const {
	return m_pObject->getActivationState() == ISLAND_SLEEPING || m_pObject->getActivationState() == DISABLE_SIMULATION;
}

bool CPhysicsObject::IsFluid() const {
	return m_pFluidController != NULL;
}

bool CPhysicsObject::IsHinged() const {
	NOT_IMPLEMENTED
	return false;
}

bool CPhysicsObject::IsMoveable() const {
	if (IsStatic() || !IsMotionEnabled()) return false;
	return true;
}

bool CPhysicsObject::IsAttachedToConstraint(bool bExternalOnly) const {
	// FIXME: What is bExternalOnly?
	return m_pConstraintVec.Count() > 0;
}

bool CPhysicsObject::IsCollisionEnabled() const {
	return !(m_pObject->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE);
}

bool CPhysicsObject::IsGravityEnabled() const {
	if (!IsStatic()) {
		return !(m_pObject->getFlags() & BT_DISABLE_WORLD_GRAVITY);
	}

	return false;
}

bool CPhysicsObject::IsDragEnabled() const {
	if (!IsStatic()) {
		return m_pEnv->GetDragController()->IsControlling(this);
	}

	return false;
}

bool CPhysicsObject::IsMotionEnabled() const {
	return m_bMotionEnabled;
}

void CPhysicsObject::EnableCollisions(bool enable) {
	if (IsCollisionEnabled() == enable) return;

	if (enable) {
		m_pObject->setCollisionFlags(m_pObject->getCollisionFlags() & ~btCollisionObject::CF_NO_CONTACT_RESPONSE);
	} else {
		m_pObject->setCollisionFlags(m_pObject->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
	}
}

void CPhysicsObject::EnableGravity(bool enable) {
	if (IsGravityEnabled() == enable || IsStatic()) return;

	if (enable) {
		m_pObject->setGravity(m_pEnv->GetBulletEnvironment()->getGravity());
		m_pObject->setFlags(m_pObject->getFlags() & ~BT_DISABLE_WORLD_GRAVITY);
	} else {
		m_pObject->setGravity(btVector3(0,0,0));
		m_pObject->setFlags(m_pObject->getFlags() | BT_DISABLE_WORLD_GRAVITY);
	}
}

void CPhysicsObject::EnableDrag(bool enable)  {
	if (IsStatic() || enable == IsDragEnabled())
		return;

	if (enable) {
		m_pEnv->GetDragController()->AddPhysicsObject(this);
	} else {
		m_pEnv->GetDragController()->RemovePhysicsObject(this);
	}
}

void CPhysicsObject::EnableMotion(bool enable) {
	if (IsMotionEnabled() == enable || IsStatic()) return;
	m_bMotionEnabled = enable;

	// FIXME: Does this cause any issues with player controllers? (player controller angular factor is always 0!)
	// TODO: We should be setting a flag on the object to disable motion, not this!
	if (enable) {
		m_pObject->setLinearFactor(btVector3(1, 1, 1));
		m_pObject->setAngularFactor(1);
	} else {
		m_pObject->setLinearVelocity(btVector3(0, 0, 0));
		m_pObject->setAngularVelocity(btVector3(0, 0, 0));
		
		m_pObject->setLinearFactor(btVector3(0, 0, 0));
		m_pObject->setAngularFactor(0);
	}
}

void CPhysicsObject::SetGameData(void *pGameData) {
	m_pGameData = pGameData;
}

void *CPhysicsObject::GetGameData() const {
	return m_pGameData;
}

void CPhysicsObject::SetGameFlags(unsigned short userFlags) {
	m_gameFlags = userFlags;
}

unsigned short CPhysicsObject::GetGameFlags() const {
	return m_gameFlags;
}

void CPhysicsObject::SetGameIndex(unsigned short gameIndex) {
	m_iGameIndex = gameIndex;
}

unsigned short CPhysicsObject::GetGameIndex() const {
	return m_iGameIndex;
}

void CPhysicsObject::SetCallbackFlags(unsigned short callbackflags) {
	m_callbacks = callbackflags;
}

unsigned short CPhysicsObject::GetCallbackFlags() const {
	return m_callbacks;
}

// UNEXPOSED
void CPhysicsObject::AddCallbackFlags(unsigned short flags) {
	m_callbacks |= flags;
}

// UNEXPOSED
void CPhysicsObject::RemoveCallbackFlags(unsigned short flags) {
	m_callbacks &= ~(flags);
}

void CPhysicsObject::Wake() {
	// Static objects can't wake!
	if (IsStatic())
		return;

	m_pObject->setActivationState(ACTIVE_TAG);
}

void CPhysicsObject::Sleep() {
	// Static objects can't sleep!
	if (IsStatic())
		return;

	m_pObject->setActivationState(ISLAND_SLEEPING);
}

void CPhysicsObject::RecheckCollisionFilter() {
	// Remove any collision points that we shouldn't be colliding with now
	btOverlappingPairCache *pCache = m_pEnv->GetBulletEnvironment()->getBroadphase()->getOverlappingPairCache();
	btBroadphasePairArray arr = pCache->getOverlappingPairArray();
	CCollisionSolver *pSolver = m_pEnv->GetCollisionSolver();

	for (int i = pCache->getNumOverlappingPairs()-1; i >= 0; i--) {
		btBroadphasePair &pair = arr[i];
		btCollisionObject *pBody0 = (btCollisionObject *)pair.m_pProxy0->m_clientObject;
		btCollisionObject *pBody1 = (btCollisionObject *)pair.m_pProxy1->m_clientObject;

		CPhysicsObject *pObj0 = (CPhysicsObject *)pBody0->getUserPointer();
		CPhysicsObject *pObj1 = (CPhysicsObject *)pBody1->getUserPointer();

		if (pSolver && !pSolver->NeedsCollision(pObj0, pObj1)) {
			pCache->removeOverlappingPair(pair.m_pProxy0, pair.m_pProxy1, m_pEnv->GetBulletEnvironment()->getDispatcher());
		}
	}
}

void CPhysicsObject::RecheckContactPoints() {
	return;
}

void CPhysicsObject::UpdateCollide() {
	btVector3 inertia;

	btCollisionShape *pShape = m_pObject->getCollisionShape();
	pShape->calculateLocalInertia(m_fMass, inertia);

	m_pObject->setMassProps(m_fMass, inertia);
	m_pObject->updateInertiaTensor();
}

void CPhysicsObject::SetMass(float mass) {
	if (IsStatic()) return;

	m_fMass = mass;

	btVector3 inertia = m_pObject->getInvInertiaDiagLocal();

	// Inverse the inverse to get the not inverse (unless in the case that the not inverse is inverse, therefore you must inverse the universe)
	inertia.setX(SAFE_DIVIDE(1.0, inertia.x()));
	inertia.setY(SAFE_DIVIDE(1.0, inertia.y()));
	inertia.setZ(SAFE_DIVIDE(1.0, inertia.z()));

	m_pObject->setMassProps(mass, inertia);
}

float CPhysicsObject::GetMass() const {
	return m_fMass;
}

float CPhysicsObject::GetInvMass() const {
	return SAFE_DIVIDE(1, m_fMass);
}

Vector CPhysicsObject::GetInertia() const {
	btVector3 btvec = m_pObject->getInvInertiaDiagLocal();

	// Invert the inverse inertia to get inertia
	btvec.setX(SAFE_DIVIDE(1.0, btvec.x()));
	btvec.setY(SAFE_DIVIDE(1.0, btvec.y()));
	btvec.setZ(SAFE_DIVIDE(1.0, btvec.z()));

	Vector hlvec;
	ConvertDirectionToHL(btvec, hlvec);
	VectorAbs(hlvec, hlvec);
	return hlvec;
}

Vector CPhysicsObject::GetInvInertia() const {
	btVector3 btvec = m_pObject->getInvInertiaDiagLocal();
	Vector hlvec;
	ConvertDirectionToHL(btvec, hlvec);
	VectorAbs(hlvec, hlvec);
	return hlvec;
}

void CPhysicsObject::SetInertia(const Vector &inertia) {
	btVector3 btvec;
	ConvertDirectionToBull(inertia, btvec);
	btvec = btvec.absolute();

	btvec.setX(SAFE_DIVIDE(1.0, btvec.x()));
	btvec.setY(SAFE_DIVIDE(1.0, btvec.y()));
	btvec.setZ(SAFE_DIVIDE(1.0, btvec.z()));

	m_pObject->setInvInertiaDiagLocal(btvec);
	m_pObject->updateInertiaTensor();
}

// FIXME: The API is confusing because we need to add the BT_DISABLE_WORLD_GRAVITY flag to the object
// by calling EnableGravity(false)
void CPhysicsObject::SetLocalGravity(const Vector &gravityVector) {
	btVector3 tmp;
	ConvertPosToBull(gravityVector, tmp);
	m_pObject->setGravity(tmp);
}

Vector CPhysicsObject::GetLocalGravity() const {
	Vector tmp;
	ConvertPosToHL(m_pObject->getGravity(), tmp);
	return tmp;
}

void CPhysicsObject::SetDamping(const float *speed, const float *rot) {
	if (!speed && !rot) return;

	m_pObject->setDamping(speed ? *speed : m_pObject->getLinearDamping(),
						  rot ? *rot : m_pObject->getAngularDamping());
}

void CPhysicsObject::GetDamping(float *speed, float *rot) const {
	if (speed) *speed = m_pObject->getLinearDamping();
	if (rot) *rot = m_pObject->getAngularDamping();
}

void CPhysicsObject::SetDragCoefficient(float *pDrag, float *pAngularDrag) {
	if (pDrag)
		m_dragCoefficient = *pDrag;

	if (pAngularDrag)
		m_angDragCoefficient = *pAngularDrag;
}

void CPhysicsObject::SetBuoyancyRatio(float ratio) {
	m_fBuoyancyRatio = ratio;
}

int CPhysicsObject::GetMaterialIndex() const {
	return m_materialIndex;
}

void CPhysicsObject::SetMaterialIndex(int materialIndex) {
	surfacedata_t *pSurface = g_SurfaceDatabase.GetSurfaceData(materialIndex);

	if (pSurface) {
		m_materialIndex = materialIndex;
		m_pObject->setFriction(pSurface->physics.friction);
		//m_pObject->setRollingFriction(pSurface->physics.friction);
		m_pObject->setRestitution(min(pSurface->physics.elasticity, 1));

		// FIXME: Figure out how to convert damping values.

		// ratio = (mass / volume) / density
		// or (actual density) / (prop density)
		m_fBuoyancyRatio = SAFE_DIVIDE(SAFE_DIVIDE(m_fMass, m_fVolume), pSurface->physics.density);
	}
}

unsigned int CPhysicsObject::GetContents() const {
	return m_contents;
}

void CPhysicsObject::SetContents(unsigned int contents) {
	m_contents = contents;
}

void CPhysicsObject::SetSleepThresholds(const float *linVel, const float *angVel) {
	if (!linVel && !angVel) return;

	m_pObject->setSleepingThresholds(linVel ? ConvertDistanceToBull(*linVel) : m_pObject->getLinearSleepingThreshold(),
									 angVel ? DEG2RAD(*angVel) : m_pObject->getAngularSleepingThreshold());
}

void CPhysicsObject::GetSleepThresholds(float *linVel, float *angVel) const {
	if (linVel) {
		*linVel = ConvertDistanceToHL(m_pObject->getLinearSleepingThreshold());
	}

	if (angVel) {
		*angVel = RAD2DEG(m_pObject->getAngularSleepingThreshold());
	}
}

float CPhysicsObject::GetSphereRadius() const {
	btCollisionShape *shape = m_pObject->getCollisionShape();
	if (shape->getShapeType() != SPHERE_SHAPE_PROXYTYPE)
		return 0;

	btSphereShape *sphere = (btSphereShape *)shape;
	return ConvertDistanceToHL(sphere->getRadius());
}

float CPhysicsObject::GetEnergy() const {
	// (1/2) * mass * velocity^2
	float e = 0.5f * GetMass() * m_pObject->getLinearVelocity().dot(m_pObject->getLinearVelocity());
	e += 0.5f * GetMass() * m_pObject->getAngularVelocity().dot(m_pObject->getAngularVelocity());
	return ConvertEnergyToHL(e);
}

Vector CPhysicsObject::GetMassCenterLocalSpace() const {
	btTransform bullTransform = ((btMassCenterMotionState *)m_pObject->getMotionState())->m_centerOfMassOffset;
	Vector HLMassCenter;
	ConvertPosToHL(bullTransform.getOrigin(), HLMassCenter);

	return HLMassCenter;
}

void CPhysicsObject::SetPosition(const Vector &worldPosition, const QAngle &angles, bool isTeleport) {
	btVector3 bullPos;
	btMatrix3x3 bullAngles;

	ConvertPosToBull(worldPosition, bullPos);
	ConvertRotationToBull(angles, bullAngles);
	btTransform trans(bullAngles, bullPos);

	m_pObject->setWorldTransform(trans * ((btMassCenterMotionState *)m_pObject->getMotionState())->m_centerOfMassOffset);

	// Assumed this is the behavior of IVP. If you teleport an object, you don't want it to be stupidly frozen in the air.
	// Change this if behavior of IVP is different!
	if (isTeleport)
		m_pObject->activate();
}

void CPhysicsObject::SetPositionMatrix(const matrix3x4_t &matrix, bool isTeleport) {
	btTransform trans;
	ConvertMatrixToBull(matrix, trans);
	m_pObject->setWorldTransform(trans * ((btMassCenterMotionState *)m_pObject->getMotionState())->m_centerOfMassOffset);

	if (isTeleport)
		m_pObject->activate();
}

void CPhysicsObject::GetPosition(Vector *worldPosition, QAngle *angles) const {
	if (!worldPosition && !angles) return;

	btTransform transform = m_pObject->getWorldTransform() * ((btMassCenterMotionState *)m_pObject->getMotionState())->m_centerOfMassOffset.inverse();
	if (worldPosition) ConvertPosToHL(transform.getOrigin(), *worldPosition);
	if (angles) ConvertRotationToHL(transform.getBasis(), *angles);
}

void CPhysicsObject::GetPositionMatrix(matrix3x4_t *positionMatrix) const {
	if (!positionMatrix) return;

	btTransform transform = m_pObject->getWorldTransform() * ((btMassCenterMotionState *)m_pObject->getMotionState())->m_centerOfMassOffset.inverse();
	ConvertMatrixToHL(transform, *positionMatrix);
}

void CPhysicsObject::SetVelocity(const Vector *velocity, const AngularImpulse *angularVelocity) {
	if (!velocity && !angularVelocity) return;

	if (!IsMoveable() || !IsMotionEnabled()) {
		return;
	}
	Wake();

	btVector3 vel, angvel;
	if (velocity) {
		ConvertPosToBull(*velocity, vel);
		m_pObject->setLinearVelocity(vel);
	}

	// Angular velocity is supplied in local space.
	if (angularVelocity) {
		ConvertAngularImpulseToBull(*angularVelocity, angvel);
		angvel = m_pObject->getWorldTransform().getBasis() * angvel;

		m_pObject->setAngularVelocity(angvel);
	}
}

void CPhysicsObject::SetVelocityInstantaneous(const Vector *velocity, const AngularImpulse *angularVelocity) {
	// FIXME: what is different from SetVelocity?
	// Sets velocity in the same "iteration"
	SetVelocity(velocity, angularVelocity);
}

void CPhysicsObject::GetVelocity(Vector *velocity, AngularImpulse *angularVelocity) const {
	if (!velocity && !angularVelocity) return;

	if (velocity)
		ConvertPosToHL(m_pObject->getLinearVelocity(), *velocity);

	// Angular velocity is supplied in local space.
	if (angularVelocity) {
		btVector3 angVel = m_pObject->getAngularVelocity();
		angVel = m_pObject->getWorldTransform().getBasis().transpose() * angVel;

		ConvertAngularImpulseToHL(angVel, *angularVelocity);
	}
}

void CPhysicsObject::AddVelocity(const Vector *velocity, const AngularImpulse *angularVelocity) {
	if (!velocity && !angularVelocity) return;

	if (!IsMoveable() || !IsMotionEnabled()) {
		return;
	}
	Wake();

	btVector3 bullvelocity, bullangular;
	if (velocity) {
		ConvertPosToBull(*velocity, bullvelocity);
		m_pObject->setLinearVelocity(m_pObject->getLinearVelocity() + bullvelocity);
	}

	// Angular velocity is supplied in local space.
	if (angularVelocity) {
		ConvertAngularImpulseToBull(*angularVelocity, bullangular);
		bullangular = m_pObject->getWorldTransform().getBasis() * bullangular;

		m_pObject->setAngularVelocity(m_pObject->getAngularVelocity() + bullangular);
	}
}

void CPhysicsObject::GetVelocityAtPoint(const Vector &worldPosition, Vector *pVelocity) const {
	if (!pVelocity) return;

	Vector localPos;
	WorldToLocal(&localPos, worldPosition);

	btVector3 vec;
	ConvertPosToBull(localPos, vec);
	ConvertPosToHL(m_pObject->getVelocityInLocalPoint(vec), *pVelocity);
}

void CPhysicsObject::GetImplicitVelocity(Vector *velocity, AngularImpulse *angularVelocity) const {
	if (!velocity && !angularVelocity) return;

	// gets the velocity actually moved by the object in the last simulation update
	NOT_IMPLEMENTED
}

void CPhysicsObject::LocalToWorld(Vector *worldPosition, const Vector &localPosition) const {
	if (!worldPosition) return;

	matrix3x4_t matrix;
	GetPositionMatrix(&matrix);
	VectorTransform(localPosition, matrix, *worldPosition);
}

void CPhysicsObject::WorldToLocal(Vector *localPosition, const Vector &worldPosition) const {
	if (!localPosition) return;

	matrix3x4_t matrix;
	GetPositionMatrix(&matrix);
	VectorITransform(worldPosition, matrix, *localPosition);
}

void CPhysicsObject::LocalToWorldVector(Vector *worldVector, const Vector &localVector) const {
	if (!worldVector) return;

	matrix3x4_t matrix;
	GetPositionMatrix(&matrix);
	VectorRotate(localVector, matrix, *worldVector);
}

void CPhysicsObject::WorldToLocalVector(Vector *localVector, const Vector &worldVector) const {
	if (!localVector) return;

	matrix3x4_t matrix;
	GetPositionMatrix(&matrix);
	VectorIRotate(worldVector, matrix, *localVector);
}

void CPhysicsObject::ApplyForceCenter(const Vector &forceVector) {
	if (!IsMoveable() || !IsMotionEnabled()) {
		return;
	}
	Wake();

	// forceVector is in kg*in/s
	// bullet takes forces in newtons, aka kg*m/s

	btVector3 force;
	ConvertForceImpulseToBull(forceVector, force);
	m_pObject->applyCentralImpulse(force);
}

void CPhysicsObject::ApplyForceOffset(const Vector &forceVector, const Vector &worldPosition) {
	if (!IsMoveable() || !IsMotionEnabled()) {
		return;
	}
	Wake();

	Vector local;
	WorldToLocal(&local, worldPosition);

	btVector3 force, offset;
	ConvertForceImpulseToBull(forceVector, force);
	ConvertPosToBull(local, offset);
	m_pObject->applyImpulse(force, offset);
	Wake();
}

void CPhysicsObject::ApplyTorqueCenter(const AngularImpulse &torque) {
	if (!IsMoveable() || !IsMotionEnabled()) {
		return;
	}
	Wake();

	btVector3 bullTorque;
	ConvertAngularImpulseToBull(torque, bullTorque);
	m_pObject->applyTorqueImpulse(bullTorque);
}

// Output passed to ApplyForceCenter/ApplyTorqueCenter
void CPhysicsObject::CalculateForceOffset(const Vector &forceVector, const Vector &worldPosition, Vector *centerForce, AngularImpulse *centerTorque) const {
	if (!centerForce && !centerTorque) return;

	btVector3 pos, force;
	ConvertPosToBull(worldPosition, pos);
	ConvertForceImpulseToBull(forceVector, force);

	pos = pos - m_pObject->getWorldTransform().getOrigin();

	btVector3 cross = pos.cross(force);

	if (centerForce) {
		ConvertForceImpulseToHL(force, *centerForce);
	}

	if (centerTorque) {
		ConvertAngularImpulseToHL(cross, *centerTorque);
	}
}

// Thrusters call this and pass output to AddVelocity
void CPhysicsObject::CalculateVelocityOffset(const Vector &forceVector, const Vector &worldPosition, Vector *centerVelocity, AngularImpulse *centerAngularVelocity) const {
	if (!centerVelocity && !centerAngularVelocity) return;

	btVector3 force, pos;
	ConvertForceImpulseToBull(forceVector, force);
	ConvertPosToBull(worldPosition, pos);

	pos = pos - m_pObject->getWorldTransform().getOrigin();

	btVector3 cross = pos.cross(force);

	// cross.set_pairwise_mult( &cross, core->get_inv_rot_inertia());

	// Linear velocity
	if (centerVelocity) {
		force *= m_pObject->getInvMass();
		ConvertForceImpulseToHL(force, *centerVelocity);
	}

	if (centerAngularVelocity) {
		ConvertAngularImpulseToHL(cross, *centerAngularVelocity);
	}
}

float CPhysicsObject::CalculateLinearDrag(const Vector &unitDirection) const {
	btVector3 bull_unitDirection;
	ConvertDirectionToBull(unitDirection, bull_unitDirection);
	return GetDragInDirection(bull_unitDirection);
}

float CPhysicsObject::CalculateAngularDrag(const Vector &objectSpaceRotationAxis) const {
	btVector3 bull_unitDirection;
	ConvertDirectionToBull(objectSpaceRotationAxis, bull_unitDirection);
	return DEG2RAD(GetAngularDragInDirection(bull_unitDirection));
}

// This function is a silly hack, games should be using the friction snapshot instead.
bool CPhysicsObject::GetContactPoint(Vector *contactPoint, IPhysicsObject **contactObject) const {
	if (!contactPoint && !contactObject) return false;

	int numManifolds = m_pEnv->GetBulletEnvironment()->getDispatcher()->getNumManifolds();
	for (int i = 0; i < numManifolds; i++) {
		btPersistentManifold *contactManifold = m_pEnv->GetBulletEnvironment()->getDispatcher()->getManifoldByIndexInternal(i);
		const btCollisionObject *obA = contactManifold->getBody0();
		const btCollisionObject *obB = contactManifold->getBody1();

		if (contactManifold->getNumContacts() <= 0)
			continue;

		// Interface specifies this function as a hack - return any point of contact.
		btManifoldPoint bullContactPoint = contactManifold->getContactPoint(0);

		if (obA == m_pObject) {
			btVector3 bullContactVec = bullContactPoint.getPositionWorldOnA();

			if (contactPoint) {
				ConvertPosToHL(bullContactVec, *contactPoint);
			}

			if (contactObject) {
				*contactObject = (IPhysicsObject *)obB->getUserPointer();
			}

			return true;
		} else if (obB == m_pObject) {
			btVector3 bullContactVec = bullContactPoint.getPositionWorldOnB();

			if (contactPoint) {
				ConvertPosToHL(bullContactVec, *contactPoint);
			}

			if (contactObject) {
				*contactObject = (IPhysicsObject *)obA->getUserPointer();
			}

			return true;
		}
	}

	return false; // Bool in contact
}

void CPhysicsObject::SetShadow(float maxSpeed, float maxAngularSpeed, bool allowPhysicsMovement, bool allowPhysicsRotation) {
	if (m_pShadow) {
		m_pShadow->MaxSpeed(maxSpeed, maxAngularSpeed);
		m_pShadow->SetAllowsTranslation(allowPhysicsMovement);
		m_pShadow->SetAllowsRotation(allowPhysicsRotation);
	} else {
		unsigned int flags = GetCallbackFlags() | CALLBACK_SHADOW_COLLISION;
		flags &= ~CALLBACK_GLOBAL_FRICTION;
		flags &= ~CALLBACK_GLOBAL_COLLIDE_STATIC;
		SetCallbackFlags(flags);

		m_pShadow = (CShadowController *)m_pEnv->CreateShadowController(this, allowPhysicsMovement, allowPhysicsRotation);
		m_pShadow->MaxSpeed(maxSpeed, maxAngularSpeed);
	}
}

void CPhysicsObject::UpdateShadow(const Vector &targetPosition, const QAngle &targetAngles, bool tempDisableGravity, float timeOffset) {
	if (m_pShadow) {
		m_pShadow->Update(targetPosition, targetAngles, timeOffset);
	}
}

int CPhysicsObject::GetShadowPosition(Vector *position, QAngle *angles) const {
	if (!m_pShadow || (!position && !angles)) return 1;

	btTransform transform;
	((btMassCenterMotionState *)m_pObject->getMotionState())->getGraphicTransform(transform);

	if (position)
		ConvertPosToHL(transform.getOrigin(), *position);

	if (angles)
		ConvertRotationToHL(transform.getBasis(), *angles);

	// Ticks simulated since last UpdateShadow()
	return m_pShadow->GetTicksSinceUpdate();
}

IPhysicsShadowController *CPhysicsObject::GetShadowController() const {
	return m_pShadow;
}

void CPhysicsObject::RemoveShadowController() {
	if (m_pShadow)
		m_pEnv->DestroyShadowController(m_pShadow);

	RemoveCallbackFlags(CALLBACK_SHADOW_COLLISION);
	AddCallbackFlags(CALLBACK_GLOBAL_FRICTION | CALLBACK_GLOBAL_COLLIDE_STATIC);

	m_pShadow = NULL;
}

float CPhysicsObject::ComputeShadowControl(const hlshadowcontrol_params_t &params, float secondsToArrival, float dt) {
	return ComputeShadowControllerHL(this, params, secondsToArrival, dt);
}

const CPhysCollide *CPhysicsObject::GetCollide() const {
	return (CPhysCollide *)m_pObject->getCollisionShape();
}

CPhysCollide *CPhysicsObject::GetCollide() {
	return (CPhysCollide *)m_pObject->getCollisionShape();
}

const char *CPhysicsObject::GetName() const {
	return m_pName;
}

bool CPhysicsObject::IsTrigger() const {
	return m_pGhostObject != NULL || m_pFluidController != NULL;
}

void CPhysicsObject::BecomeTrigger() {
	if (IsTrigger())
		return;

	EnableDrag(false);
	EnableGravity(false);

	m_pEnv->GetBulletEnvironment()->removeRigidBody(m_pObject);

	m_pGhostObject = new btGhostObject;
	m_pGhostObject->setCollisionShape(m_pObject->getCollisionShape());
	m_pGhostObject->setUserPointer(this);
	m_pGhostObject->setCollisionFlags(m_pGhostObject->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE | btCollisionObject::CF_STATIC_OBJECT);
	m_pGhostObject->setWorldTransform(m_pObject->getWorldTransform());
	
	m_pGhostCallback = new CGhostTriggerCallback(this);
	if (m_pGhostCallback)
		m_pGhostObject->setCallback(m_pGhostCallback);

	m_pEnv->GetBulletEnvironment()->addCollisionObject(m_pGhostObject, COLGROUP_WORLD, ~COLGROUP_WORLD);
}

void CPhysicsObject::RemoveTrigger() {
	if (!IsTrigger())
		return;

	EnableDrag(true);
	EnableGravity(true);

	m_pObject->setWorldTransform(m_pGhostObject->getWorldTransform());

	if (IsStatic())
		m_pEnv->GetBulletEnvironment()->addRigidBody(m_pObject, COLGROUP_WORLD, ~COLGROUP_WORLD);
	else
		m_pEnv->GetBulletEnvironment()->addRigidBody(m_pObject);

	m_pGhostObject->setCallback(NULL);
	delete m_pGhostCallback;
	m_pGhostCallback = NULL;

	m_pEnv->GetBulletEnvironment()->removeCollisionObject(m_pGhostObject);
	delete m_pGhostObject;
	m_pGhostObject = NULL;
}

void CPhysicsObject::TriggerObjectEntered(CPhysicsObject *pObject) {
	// Doesn't work.
	//m_pEnv->HandleObjectEnteredTrigger(this, pObject);
}

void CPhysicsObject::TriggerObjectExited(CPhysicsObject *pObject) {
	//m_pEnv->HandleObjectExitedTrigger(this, pObject);
}

void CPhysicsObject::BecomeHinged(int localAxis) {
	// localAxis is the axis we're hinged to.
	// Called on attached object of constraint if the world -> local axis is close enough to a unit axis direction (I think)
	NOT_IMPLEMENTED
}

void CPhysicsObject::RemoveHinged() {
	NOT_IMPLEMENTED
}

IPhysicsFrictionSnapshot *CPhysicsObject::CreateFrictionSnapshot() {
	return ::CreateFrictionSnapshot(this);
}

void CPhysicsObject::DestroyFrictionSnapshot(IPhysicsFrictionSnapshot *pSnapshot) {
	delete (CPhysicsFrictionSnapshot *)pSnapshot;
}

void CPhysicsObject::OutputDebugInfo() const {
	Msg("-----------------\n");

	if (m_pName)
		Msg("Object: %s\n", m_pName);

	Msg("Mass: %f (inv %f)\n", GetMass(), GetInvMass());

	Vector pos;
	QAngle ang;
	GetPosition(&pos, &ang);
	Msg("Position: %f %f %f\nAngle: %f %f %f\n", pos.x, pos.y, pos.z, ang.x, ang.y, ang.z);

	Vector inertia = GetInertia();
	Vector invinertia = GetInvInertia();
	Msg("Inertia: %f %f %f (inv %f %f %f)\n", inertia.x, inertia.y, inertia.z, invinertia.x, invinertia.y, invinertia.z);

	Vector vel;
	AngularImpulse angvel;
	GetVelocity(&vel, &angvel);
	Msg("Velocity: %f, %f, %f\nAng Velocity: %f, %f, %f\n", vel.x, vel.y, vel.z, angvel.x, angvel.y, angvel.z);

	float dampspeed, damprot;
	GetDamping(&dampspeed, &damprot);
	Msg("Damping %f linear, %f angular\n", dampspeed, damprot);

	Vector dragBasis;
	Vector angDragBasis;
	ConvertPosToHL(m_dragBasis, dragBasis);
	ConvertDirectionToHL(m_angDragBasis, angDragBasis);
	Msg("Linear Drag: %f, %f, %f (factor %f)\n", dragBasis.x, dragBasis.y, dragBasis.z, m_dragCoefficient);
	Msg("Angular Drag: %f, %f, %f (factor %f)\n", angDragBasis.x, angDragBasis.y, angDragBasis.z, m_angDragCoefficient);

	// TODO: Attached to x controllers

	Msg("State: %s, Collision %s, Motion %s, Drag %s, Flags %04X (game %04x, index %d)\n", 
		IsAsleep() ? "Asleep" : "Awake",
		IsCollisionEnabled() ? "Enabled" : "Disabled",
		IsStatic() ? "Static" : IsMotionEnabled() ? "Enabled" : "Disabled",
		IsDragEnabled() ? "Enabled" : "Disabled",
		m_pObject->getFlags(),
		GetGameFlags(),
		GetGameIndex()
	);

	
	const char *pMaterialStr = g_SurfaceDatabase.GetPropName(m_materialIndex);
	surfacedata_t *surfaceData = g_SurfaceDatabase.GetSurfaceData(m_materialIndex);
	if (surfaceData) {
		Msg("Material: %s : density(%f), thickness(%f), friction(%f), elasticity(%f)\n", 
			pMaterialStr, surfaceData->physics.density, surfaceData->physics.thickness, surfaceData->physics.friction, surfaceData->physics.elasticity);
	}

	Msg("-- COLLISION SHAPE INFO --\n");
	g_PhysicsCollision.OutputDebugInfo((CPhysCollide *)m_pObject->getCollisionShape());
}

// UNEXPOSED
void CPhysicsObject::Init(CPhysicsEnvironment *pEnv, btRigidBody *pObject, int materialIndex, objectparams_t *pParams, bool isStatic, bool isSphere) {
	m_pEnv				= pEnv;
	m_pObject			= pObject;
	m_bIsSphere			= isSphere;
	m_gameFlags			= 0;
	m_bMotionEnabled	= !isStatic;
	m_fMass				= (pParams && !isStatic) ? pParams->mass : 0;
	m_pGameData			= NULL;
	m_pName				= NULL;
	m_fVolume			= 0;
	m_callbacks			= CALLBACK_GLOBAL_COLLISION | CALLBACK_GLOBAL_FRICTION | CALLBACK_FLUID_TOUCH | CALLBACK_GLOBAL_TOUCH | CALLBACK_GLOBAL_COLLIDE_STATIC | CALLBACK_DO_FLUID_SIMULATION;
	m_iLastActivationState = -1;

	m_pObject->setUserPointer(this);
	m_pObject->setSleepingThresholds(SLEEP_LINEAR_THRESHOLD, SLEEP_ANGULAR_THRESHOLD);
	m_pObject->setActivationState(ISLAND_SLEEPING); // All objects start asleep.

	if (pParams) {
		m_pGameData		= pParams->pGameData;
		m_pName			= pParams->pName;
		m_fVolume		= pParams->volume * CUBIC_METERS_PER_CUBIC_INCH;
		EnableCollisions(pParams->enableCollisions);
	}

	SetMaterialIndex(materialIndex);
	SetContents(MASK_SOLID);

	// Compute our air drag values.
	float drag = 0;
	float angDrag = 0;
	if (pParams) {
		drag = pParams->dragCoefficient;
		angDrag = pParams->dragCoefficient;
	}

	if (isStatic || !GetCollide()) {
		drag = 0;
		angDrag = 0;
	}

	ComputeDragBasis(isStatic);

	if (!isStatic && drag != 0.0f) {
		EnableDrag(true);
	}

	m_dragCoefficient = drag;
	m_angDragCoefficient = angDrag;

	// Compute our continuous collision detection stuff (for fast moving objects, prevents tunneling)
	// This doesn't work on compound objects! see: btDiscreteDynamicsWorld::integrateTransforms
	if (!isStatic) {
		btVector3 mins, maxs;
		m_pObject->getCollisionShape()->getAabb(btTransform::getIdentity(), mins, maxs);
		mins = mins.absolute();
		maxs = maxs.absolute();

		float maxradius = min(min(maxs.getX(), maxs.getY()), maxs.getZ());
		float minradius = min(min(mins.getX(), mins.getY()), mins.getZ());
		float radius = min(maxradius, minradius);

		m_pObject->setCcdMotionThreshold((radius / 2) * (radius / 2));
		m_pObject->setCcdSweptSphereRadius(0.7f * radius);
	}

	if (isStatic) {
		m_pObject->setCollisionFlags(m_pObject->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
		m_pEnv->GetBulletEnvironment()->addRigidBody(m_pObject, COLGROUP_WORLD, ~COLGROUP_WORLD);
	} else {
		m_pEnv->GetBulletEnvironment()->addRigidBody(m_pObject);
	}
}

// UNEXPOSED
CPhysicsEnvironment *CPhysicsObject::GetVPhysicsEnvironment() {
	return m_pEnv;
}

// UNEXPOSED
btRigidBody *CPhysicsObject::GetObject() {
	return m_pObject;
}

// UNEXPOSED
// Purpose: Constraints will call this when we're one of the constrained objects.
void CPhysicsObject::AttachedToConstraint(CPhysicsConstraint *pConstraint) {
	m_pConstraintVec.AddToTail(pConstraint);
}

// UNEXPOSED
// Purpose: Constraints will call this when we're one of the constrained objects.
void CPhysicsObject::DetachedFromConstraint(CPhysicsConstraint *pConstraint) {
	m_pConstraintVec.FindAndRemove(pConstraint);
}

// UNEXPOSED
float CPhysicsObject::GetDragInDirection(const btVector3 &dir) const {
	btVector3 out;
	btMatrix3x3 mat = m_pObject->getCenterOfMassTransform().getBasis();
	BtMatrix_vimult(mat, dir, out);

	return m_dragCoefficient * fabs(out.getX() * m_dragBasis.getX()) + 
		fabs(out.getY() * m_dragBasis.getY()) +	
		fabs(out.getZ() * m_dragBasis.getZ());
}

// UNEXPOSED
float CPhysicsObject::GetAngularDragInDirection(const btVector3 &dir) const {
	return m_angDragCoefficient * fabs(dir.getX() * m_angDragBasis.getX()) +
		fabs(dir.getY() * m_angDragBasis.getY()) +
		fabs(dir.getZ() * m_angDragBasis.getZ());
}

// UNEXPOSED
void CPhysicsObject::ComputeDragBasis(bool isStatic) {
	m_dragBasis.setZero();
	m_angDragBasis.setZero();

	if (!isStatic && GetCollide()) {
		btCollisionShape *shape = m_pObject->getCollisionShape();

		btVector3 min, max, delta;
		btTransform ident = btTransform::getIdentity();

		shape->getAabb(ident, min, max);

		delta = max - min;
		delta = delta.absolute();

		m_dragBasis.setX(delta.y() * delta.z());
		m_dragBasis.setY(delta.x() * delta.z());
		m_dragBasis.setZ(delta.x() * delta.y());
		m_dragBasis *= GetInvMass();

		btVector3 ang = m_pObject->getInvInertiaDiagLocal();
		delta *= 0.5;

		m_angDragBasis.setX(AngDragIntegral(ang[0], delta.x(), delta.y(), delta.z()) + AngDragIntegral(ang[0], delta.x(), delta.z(), delta.y()));
		m_angDragBasis.setY(AngDragIntegral(ang[1], delta.y(), delta.x(), delta.z()) + AngDragIntegral(ang[1], delta.y(), delta.z(), delta.x()));
		m_angDragBasis.setZ(AngDragIntegral(ang[2], delta.z(), delta.x(), delta.y()) + AngDragIntegral(ang[2], delta.z(), delta.y(), delta.x()));
	}
}

btVector3 CPhysicsObject::GetBullMassCenterOffset() const {
	return ((btMassCenterMotionState *)m_pObject->getMotionState())->m_centerOfMassOffset.getOrigin();
}

void CPhysicsObject::TransferToEnvironment(CPhysicsEnvironment *pDest) {
	m_pEnv->GetBulletEnvironment()->removeRigidBody(m_pObject);
	m_pEnv = pDest;

	m_pEnv->GetBulletEnvironment()->addRigidBody(m_pObject);
}

/************************
* CREATION FUNCTIONS
************************/

CPhysicsObject *CreatePhysicsObject(CPhysicsEnvironment *pEnvironment, const CPhysCollide *pCollisionModel, int materialIndex, const Vector &position, const QAngle &angles, objectparams_t *pParams, bool isStatic) {
	btCollisionShape *shape = (btCollisionShape *)pCollisionModel;
	Assert(shape);
	if (!shape) return NULL; 
	
	btVector3 vector;
	btMatrix3x3 matrix;
	ConvertPosToBull(position, vector);
	ConvertRotationToBull(angles, matrix);
	btTransform transform(matrix, vector);

	btTransform masscenter = btTransform::getIdentity();

	physshapeinfo_t *shapeInfo = (physshapeinfo_t *)shape->getUserPointer();
	if (shapeInfo)
		masscenter.setOrigin(shapeInfo->massCenter);

	/*
	// Doesn't work unless we shift the collision model
	if (pParams && pParams->massCenterOverride) {
		btVector3 vecMassCenter;
		ConvertPosToBull(*pParams->massCenterOverride, vecMassCenter);
		masscenter.setOrigin(vecMassCenter);
	}
	*/

	float mass = 0;
	btVector3 inertiaFactor(1, 1, 1);

	if (pParams && !isStatic) {
		mass = pParams->mass;

		// Don't allow the inertia factor to be less than 0!
		if (pParams->inertia > 0)
			inertiaFactor.setValue(pParams->inertia, pParams->inertia, pParams->inertia);
	}

	btVector3 inertia(0, 0, 0);

	if (!isStatic)
		shape->calculateLocalInertia(mass, inertia);

	inertia *= inertiaFactor;

	btMassCenterMotionState *motionstate = new btMassCenterMotionState(masscenter);
	motionstate->setGraphicTransform(transform);
	btRigidBody::btRigidBodyConstructionInfo info(mass, motionstate, shape, inertia);

	if (pParams) {
		//info.m_linearDamping = pParams->damping;
		//info.m_angularDamping = pParams->rotdamping;

		// FIXME: We should be using inertia values from source. Figure out a proper conversion.
		// Inertia with props is 1 (always?) and 25 with ragdolls (always?)
		//info.m_localInertia = btVector3(pParams->inertia, pParams->inertia, pParams->inertia);
	}

	btRigidBody *body = new btRigidBody(info);

	CPhysicsObject *pObject = new CPhysicsObject;
	pObject->Init(pEnvironment, body, materialIndex, pParams, isStatic);
	
	return pObject;
}

CPhysicsObject *CreatePhysicsSphere(CPhysicsEnvironment *pEnvironment, float radius, int materialIndex, const Vector &position, const QAngle &angles, objectparams_t *pParams, bool isStatic) {
	if (!pEnvironment) return NULL;

	// Conversion unnecessary as this is an exposed function.
	btSphereShape *shape = (btSphereShape *)g_PhysicsCollision.SphereToConvex(radius);
	
	btVector3 vector;
	btMatrix3x3 matrix;
	ConvertPosToBull(position, vector);
	ConvertRotationToBull(angles, matrix);
	btTransform transform(matrix, vector);

	float mass = 0;
	float volume = 0;

	if (pParams) {
		mass = isStatic ? 0 : pParams->mass;

		volume = pParams->volume;
		if (volume <= 0) {
			pParams->volume = (4 / 3) * M_PI * radius * radius * radius;
		}
	}

	btMassCenterMotionState *motionstate = new btMassCenterMotionState();
	motionstate->setGraphicTransform(transform);
	btRigidBody::btRigidBodyConstructionInfo info(mass, motionstate, shape);

	btRigidBody *body = new btRigidBody(info);

	CPhysicsObject *pObject = new CPhysicsObject;
	pObject->Init(pEnvironment, body, materialIndex, pParams, isStatic, true);

	return pObject;
}