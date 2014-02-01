#include "StdAfx.h"

#include "Physics_CollisionSet.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

/******************************
* CLASS CPhysicsCollisionSet
******************************/

// Is this class sort of like CPhysicsObjectPairHash?
// ShouldCollide is called by game code from the collision event handler in CPhysicsEnvironment

CPhysicsCollisionSet::CPhysicsCollisionSet(int iMaxEntries) {
	m_iMaxEntries = iMaxEntries;

	m_collArray = new int[iMaxEntries];
}

CPhysicsCollisionSet::~CPhysicsCollisionSet() {
	delete [] m_collArray;
}

void CPhysicsCollisionSet::EnableCollisions(int index0, int index1) {
	Assert((index0 < m_iMaxEntries && index0 > 0) || (index1 < m_iMaxEntries && index1 > 0));
	if ((index0 < m_iMaxEntries && index0 > 0) || (index1 < m_iMaxEntries && index1 > 0)) {
		return;
	}

	// Totally stolen from valve!
	m_collArray[index0] |= 1 << index1;
	m_collArray[index1] |= 1 << index0;
}

void CPhysicsCollisionSet::DisableCollisions(int index0, int index1) {
	Assert((index0 < m_iMaxEntries && index0 > 0) || (index1 < m_iMaxEntries && index1 > 0));
	if ((index0 < m_iMaxEntries && index0 > 0) || (index1 < m_iMaxEntries && index1 > 0)) {
		return;
	}

	m_collArray[index0] &= ~(1 << index1);
	m_collArray[index1] &= ~(1 << index0);
}

bool CPhysicsCollisionSet::ShouldCollide(int index0, int index1) {
	Assert((index0 < m_iMaxEntries && index0 > 0) || (index1 < m_iMaxEntries && index1 > 0));
	if ((index0 < m_iMaxEntries && index0 > 0) || (index1 < m_iMaxEntries && index1 > 0)) {
		return true;
	}

	return ((1 << index1) & m_collArray[index0]) != 0;
}

/*********************
* CREATION FUNCTIONS
*********************/

CPhysicsCollisionSet *CreateCollisionSet(int maxElements) {
	return new CPhysicsCollisionSet(maxElements);
}