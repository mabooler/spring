#include "StdAfx.h"
#include "mmgr.h"

#include "WeaponProjectile.h"
#include "Sim/Weapons/WeaponDefHandler.h"
#include "Sound.h"
#include "Rendering/UnitModels/3DModelParser.h"
#include "Rendering/UnitModels/s3oParser.h"
#include "Rendering/UnitModels/3DOParser.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/Textures/TextureHandler.h"
#include "Rendering/UnitModels/UnitDrawer.h"
#include "Game/GameHelper.h"
#include "Map/Ground.h"
#include "LaserProjectile.h"
#include "FireBallProjectile.h"
#include "Matrix44f.h"
#include "Sim/Features/Feature.h"
#include "Sim/Units/Unit.h"
#include "Sim/Misc/InterceptHandler.h"
#include "GlobalUnsynced.h"
#include "LogOutput.h"
#ifdef TRACE_SYNC
	#include "Sync/SyncTracer.h"
#endif


#include "Sim/Weapons/WeaponDefHandler.h"

CR_BIND_DERIVED(CWeaponProjectile, CProjectile, );

CR_REG_METADATA(CWeaponProjectile,(
	CR_MEMBER(targeted),
//	CR_MEMBER(weaponDef),
	CR_MEMBER(weaponDefName),
	CR_MEMBER(target),
	CR_MEMBER(targetPos),
	CR_MEMBER(startpos),
	CR_MEMBER(ttl),
	CR_MEMBER(colorTeam),
	CR_MEMBER(bounces),
	CR_MEMBER(keepBouncing),
	CR_MEMBER(ceg),
	CR_MEMBER(cegTag),
	CR_MEMBER(interceptTarget),
	CR_MEMBER(id),
	CR_RESERVED(15),
	CR_POSTLOAD(PostLoad)
	));

CWeaponProjectile::CWeaponProjectile()
{
	targeted = false;
	weaponDef = 0;
	target = 0;
	ttl = 0;
	colorTeam = 0;
	interceptTarget = 0;
	bounces = 0;
	keepBouncing = true;
	cegTag = "";
}

CWeaponProjectile::CWeaponProjectile(const float3& pos, const float3& speed,
		CUnit* owner, CUnit* target, const float3 &targetPos,
		const WeaponDef* weaponDef, CWeaponProjectile* interceptTarget,
		bool synced, int ttl GML_PARG_C):
	CProjectile(pos, speed, owner, synced, true GML_PARG_P),
	weaponDef(weaponDef),
	weaponDefName(weaponDef? weaponDef->name: std::string("")),
	target(target),
	targetPos(targetPos),
	startpos(pos),
	targeted(false),
	interceptTarget(interceptTarget),
	colorTeam(0),
	bounces(0),
	keepBouncing(true),
	ttl(ttl),
	cegTag(weaponDef? weaponDef->cegTag: std::string(""))
{
	if (owner) {
		colorTeam = owner->team;
	}

	if (target) {
		AddDeathDependence(target);
	}

	if (interceptTarget) {
		interceptTarget->targeted = true;
		AddDeathDependence(interceptTarget);
	}
	if (weaponDef) {
		if(weaponDef->interceptedByShieldType) {
			interceptHandler.AddShieldInterceptableProjectile(this);
		}

		alwaysVisible = weaponDef->visuals.alwaysVisible;

		if (!weaponDef->visuals.modelName.empty()) {
			S3DOModel* model = modelParser->Load3DModel(string("objects3d/") + weaponDef->visuals.modelName, 1, colorTeam);
			if (model) {
				s3domodel = model;
			}
		}
		collisionFlags = weaponDef->collisionFlags;
	}
}

CWeaponProjectile::~CWeaponProjectile(void)
{
}

void CWeaponProjectile::Collision()
{
	if (/* !weaponDef->noExplode || gs->frameNum & 1 */ true) {
		// don't do damage only on odd-numbered frames
		// for noExplode projectiles (it breaks coldet)
		float3 impactDir = speed;
		impactDir.Normalize();

		// Dynamic Damage
		DamageArray dynDamages;
		if (weaponDef->dynDamageExp > 0)
			dynDamages = weaponDefHandler->DynamicDamages(weaponDef->damages,
					startpos, pos, (weaponDef->dynDamageRange > 0)
							? weaponDef->dynDamageRange
							: weaponDef->range,
					weaponDef->dynDamageExp, weaponDef->dynDamageMin,
					weaponDef->dynDamageInverted);

		helper->Explosion(pos,
			(weaponDef->dynDamageExp > 0) ? dynDamages : weaponDef->damages,
			weaponDef->areaOfEffect, weaponDef->edgeEffectiveness,
			weaponDef->explosionSpeed, owner, true,
			weaponDef->noExplode ? 0.3f : 1,
			weaponDef->noExplode || weaponDef->noSelfDamage, weaponDef->impactOnly,
			weaponDef->explosionGenerator, 0, impactDir, weaponDef->id);
	}

	if (weaponDef->soundhit.getID(0) > 0) {
		sound->PlaySample(weaponDef->soundhit.getID(0), this,
			weaponDef->soundhit.getVolume(0));
	}

	if (!weaponDef->noExplode){
		CProjectile::Collision();
	} else {
		if (TraveledRange()) {
			CProjectile::Collision();
		}
	}

}

void CWeaponProjectile::Collision(CFeature* feature)
{
	if(gs->randFloat() < weaponDef->fireStarter)
		feature->StartFire();

	Collision();
}

void CWeaponProjectile::Collision(CUnit* unit)
{
	if (/* !weaponDef->noExplode || gs->frameNum & 1 */ true) {
		// don't do damage only on odd-numbered frames
		// for noExplode projectiles (it breaks coldet)
		float3 impactDir = speed;
		impactDir.Normalize();

		// Dynamic Damage
		DamageArray damages;
		if (weaponDef->dynDamageExp > 0) {
			damages = weaponDefHandler->DynamicDamages(weaponDef->damages,
					startpos, pos,
					weaponDef->dynDamageRange > 0?
						weaponDef->dynDamageRange:
						weaponDef->range,
					weaponDef->dynDamageExp, weaponDef->dynDamageMin,
					weaponDef->dynDamageInverted);
		} else {
			damages = weaponDef->damages;
		}

		helper->Explosion(pos, damages,
			weaponDef->areaOfEffect, weaponDef->edgeEffectiveness,
			weaponDef->explosionSpeed, owner, true,
			weaponDef->noExplode ? 0.3f : 1,
			weaponDef->noExplode, weaponDef->impactOnly,
			weaponDef->explosionGenerator, unit,
			impactDir, weaponDef->id);
	}

	if (weaponDef->soundhit.getID(0) > 0) {
		sound->PlaySample(weaponDef->soundhit.getID(0), this,
				weaponDef->soundhit.getVolume(0));
	}

	if(!weaponDef->noExplode)
		CProjectile::Collision(unit);
	else {
		if (TraveledRange())
			CProjectile::Collision();
	}
}

void CWeaponProjectile::Update()
{
	//pos+=speed;

	//if(weaponDef->gravityAffected)
	//	speed.y+=mapInfo->map.gravity;


	//if(weaponDef->noExplode)
	//{
 //       if(TraveledRange())
	//		CProjectile::Collision();
	//}

	//if(speed.Length()<weaponDef->maxvelocity)
	//	speed += dir*weaponDef->weaponacceleration

	CProjectile::Update();
	UpdateGroundBounce();
}

void CWeaponProjectile::UpdateGroundBounce()
{
	if((weaponDef->groundBounce || weaponDef->waterBounce)
			&& (ttl > 0 || weaponDef->numBounce >= 0))
	{
		float wh = 0;
		if(!weaponDef->waterBounce)
		{
			wh = ground->GetHeight2(pos.x, pos.z);
		} else if(weaponDef->groundBounce)
		{
			wh = ground->GetHeight(pos.x, pos.z);
		}
		if(pos.y < wh)
		{
			bounces++;
			if(!keepBouncing || (this->weaponDef->numBounce >= 0
							&& bounces > this->weaponDef->numBounce))
			{
				//Collision();
				keepBouncing = false;
			} else {
				float3 tempPos = pos;
				const float3& normal = ground->GetNormal(pos.x, pos.z);
				pos -= speed;
				float dot = speed.dot(normal);
				speed -= (speed + normal*fabs(dot))*(1 - weaponDef->bounceSlip);
				speed += (normal*(fabs(dot)))*(1 + weaponDef->bounceRebound);
				pos += speed;
				if(weaponDef->bounceExplosionGenerator) {
					weaponDef->bounceExplosionGenerator->Explosion(pos,
							speed.Length(), 1, owner, 1, NULL, normal);
				}
			}
		}
	}

}

bool CWeaponProjectile::TraveledRange()
{
	float trangeSq = (pos - startpos).SqLength();
	return (trangeSq > (weaponDef->range * weaponDef->range));
}



void CWeaponProjectile::DrawUnitPart()
{
	float3 dir(speed);
	dir.Normalize();
	glPushMatrix();
	float3 rightdir;

	if (dir.y != 1)
		rightdir = dir.cross(UpVector);
	else
		rightdir = float3(1, 0, 0);

	rightdir.Normalize();
	float3 updir(rightdir.cross(dir));

	CMatrix44f transMatrix(drawPos,-rightdir,updir,dir);

	glMultMatrixf(&transMatrix[0]);
//	glCallList(modelDispList);
	glCallList(s3domodel->rootobject3do?s3domodel->rootobject3do->displist:s3domodel->rootobjects3o->displist); // dont cache displists because of delayed loading
	glPopMatrix();
}

void CWeaponProjectile::DrawS3O(void)
{
	unitDrawer->SetS3OTeamColour(colorTeam);
	DrawUnitPart();
}



void CWeaponProjectile::DependentDied(CObject* o)
{
	if(o==interceptTarget)
		interceptTarget=0;

	if(o==target)
		target=0;

	CProjectile::DependentDied(o);
}

void CWeaponProjectile::PostLoad()
{
	weaponDef = weaponDefHandler->GetWeapon(weaponDefName);

//	if(weaponDef->interceptedByShieldType)
//		interceptHandler.AddShieldInterceptableProjectile(this);

	if(!weaponDef->visuals.modelName.empty()){
		S3DOModel* model = modelParser->Load3DModel(
				string("objects3d/") + weaponDef->visuals.modelName, 1, colorTeam);
		if(model){
			s3domodel = model;
/*			if(s3domodel->rootobject3do)
				modelDispList= model->rootobject3do->displist;
			else
				modelDispList= model->rootobjects3o->displist;*/
		}
	}

//	collisionFlags = weaponDef->collisionFlags;
}
