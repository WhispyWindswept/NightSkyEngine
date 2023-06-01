﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "BattleObject.h"

#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NightSkyGameState.h"
#include "ParticleManager.h"
#include "PlayerObject.h"
#include "NightSkyEngine/Battle/Bitflags.h"
#include "NightSkyEngine/Battle/Globals.h"
#include "NightSkyEngine/Data/ParticleData.h"
#include "NightSkyEngine/Miscellaneous/RandomManager.h"

// Sets default values
ABattleObject::ABattleObject()
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
}

// Called when the game starts or when spawned
void ABattleObject::BeginPlay()
{
	Super::BeginPlay();
	if (IsPlayer)
	{
		Player = Cast<APlayerObject>(this);
	}
}

void ABattleObject::Move()
{
	if (IsPlayer)
		Player->SetHitValuesOverTime();

	//Set previous pos values
	PrevPosX = PosX; 
	PrevPosY = PosY;

	SpeedX = SpeedX * SpeedXRatePerFrame / 100;
	SpeedY = SpeedY * SpeedYRatePerFrame / 100;
	SpeedZ = SpeedZ * SpeedZRatePerFrame / 100;
	const int32 FinalSpeedX = SpeedX * SpeedXRate / 100;
	const int32 FinalSpeedY = SpeedY * SpeedYRate / 100;
	const int32 FinalSpeedZ = SpeedZ * SpeedZRate / 100;

	if (MiscFlags & MISC_InertiaEnable) //only use inertia if enabled
	{
		if (PosY <= GroundHeight && MiscFlags & MISC_FloorCollisionActive) //only decrease inertia if grounded
		{
			Inertia = Inertia - Inertia / 10;
		}
		if (Inertia > -875 && Inertia < 875) //if inertia small enough, set to zero
		{
			Inertia = 0;
		}
		AddPosXWithDir(Inertia);
	}

	if (IsPlayer)
	{
		int32 ModifiedPushback;
		if (PosY > GroundHeight)
			ModifiedPushback = Player->Pushback * 84;
		else if (Player->Stance == ACT_Crouching)
			ModifiedPushback = Player->Pushback * 86;
		else
			ModifiedPushback = Player->Pushback * 88;

		Player->Pushback = ModifiedPushback / 100;

		if (PosY <= GroundHeight || !(Player->PlayerFlags & PLF_IsStunned))
			AddPosXWithDir(Player->Pushback);
	}

	AddPosXWithDir(FinalSpeedX); //apply speed

	if (IsPlayer && Player != nullptr)
	{
		if (Player->AirDashTimer == 0 || (SpeedY > 0 && ActionTime < 5)) // only set y speed if not airdashing/airdash startup not done
			{
			PosY += FinalSpeedY;
			if (PosY > GroundHeight || !(MiscFlags & MISC_FloorCollisionActive))
				SpeedY -= Gravity;
			}
		else
		{
			SpeedY = 0;
		}
	}
	else
	{
		PosY += FinalSpeedY;
		if (PosY > GroundHeight || !(MiscFlags & MISC_FloorCollisionActive))
			SpeedY -= Gravity;
	}
		
	if (PosY < GroundHeight && MiscFlags & MISC_FloorCollisionActive) //if on ground, force y values to zero
	{
		PosY = GroundHeight;
	}

	PosZ += FinalSpeedZ;
}

// Called every frame
void ABattleObject::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!GameState)
	{
		ScreenSpaceDepthOffset = 0;
		OrthoBlendActive = 1;
		return;
	}
	
	if (GameState->BattleState.CurrentSequenceTime >= 0)
	{
		ScreenSpaceDepthOffset = 0;
		OrthoBlendActive = FMath::Lerp(OrthoBlendActive, 0, 0.2);
	}
	else
	{
		if (IsPlayer)
			ScreenSpaceDepthOffset = (MaxPlayerObjects - DrawPriority) * 25;
		else
			ScreenSpaceDepthOffset = (MaxBattleObjects - DrawPriority) * 5;
		OrthoBlendActive = FMath::Lerp(OrthoBlendActive, 1, 0.2);
	}
	TInlineComponentArray<UPrimitiveComponent*> Components(this);
	GetComponents(Components);
	for (const auto Component : Components)
	{
		for (int64 i = 0; i < Component->GetNumMaterials(); i++)
		{
			if (const auto MIDynamic = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(i)); IsValid(MIDynamic))
			{
				MIDynamic->SetScalarParameterValue(FName(TEXT("ScreenSpaceDepthOffset")), ScreenSpaceDepthOffset);
				MIDynamic->SetScalarParameterValue(FName(TEXT("OrthoBlendActive")), OrthoBlendActive);
			}
		}
	}
}

void ABattleObject::HandlePushCollision(ABattleObject* OtherObj)
{
	if (MiscFlags & MISC_PushCollisionActive && OtherObj->MiscFlags & MISC_PushCollisionActive)
	{
		if (Hitstop <= 0 && ((!OtherObj->IsPlayer || OtherObj->Player->PlayerFlags & PLF_IsThrowLock) == 0 || (!IsPlayer || Player->PlayerFlags & PLF_IsThrowLock) == 0))
		{
			if (T >= OtherObj->B && B <= OtherObj->T && R >= OtherObj->L && L <= OtherObj->R)
			{
				bool IsPushLeft;
				int CollisionDepth;
				if(PrevPosX == OtherObj->PrevPosX)
				{
					if (PosX == OtherObj->PosX)
					{
						if (IsPlayer == OtherObj->IsPlayer)
						{
							if (Player->WallTouchTimer == OtherObj->Player->WallTouchTimer)
							{
								IsPushLeft = Player->TeamIndex > 0;
							}
							else
							{
								IsPushLeft = Player->WallTouchTimer > OtherObj->Player->WallTouchTimer;
								if (PosX > 0)
								{
									IsPushLeft = Player->WallTouchTimer <= OtherObj->Player->WallTouchTimer;
								}
							}
						}
						else
						{
							IsPushLeft = IsPlayer > OtherObj->IsPlayer;
						}
					}
					else
					{
						IsPushLeft = PosX < OtherObj->PosX;
					}
				}
				else
				{
					IsPushLeft = PrevPosX < OtherObj->PrevPosX;
				}
				if (IsPushLeft)
				{
					CollisionDepth = OtherObj->L - R;
				}
				else
				{
					CollisionDepth = OtherObj->R - L;
				}
				PosX += CollisionDepth / 2;
				OtherObj->PosX -= CollisionDepth / 2;
			}
		}
	}
}

void ABattleObject::HandleHitCollision(APlayerObject* OtherChar)
{
	if (AttackFlags & ATK_IsAttacking && AttackFlags & ATK_HitActive && !(OtherChar->InvulnFlags & INV_StrikeInvulnerable) && !OtherChar->StrikeInvulnerableTimer && OtherChar != Player)
	{
		if (!(AttackFlags & ATK_AttackHeadAttribute && OtherChar->InvulnFlags & INV_HeadInvulnerable) && !(AttackFlags & ATK_AttackProjectileAttribute && OtherChar->InvulnFlags & INV_ProjectileInvulnerable))
		{
			for (int i = 0; i < CollisionArraySize; i++)
			{
				if (Boxes[i].Type == BOX_Hit)
				{
					for (int j = 0; j < CollisionArraySize; j++)
					{
						if (OtherChar->Boxes[j].Type == BOX_Hurt)
						{
							FCollisionBox Hitbox = Boxes[i];

							FCollisionBox Hurtbox = OtherChar->Boxes[j];

							if (Direction == DIR_Right)
							{
								Hitbox.PosX += PosX;
							}
							else
							{
								Hitbox.PosX = -Hitbox.PosX + PosX;  
							}
							Hitbox.PosY += PosY;
							if (OtherChar->Direction == DIR_Right)
							{
								Hurtbox.PosX += OtherChar->PosX;
							}
							else
							{
								Hurtbox.PosX = -Hurtbox.PosX + OtherChar->PosX;  
							}
							Hurtbox.PosY += OtherChar->PosY;
							
							if (Hitbox.PosY + Hitbox.SizeY / 2 >= Hurtbox.PosY - Hurtbox.SizeY / 2
								&& Hitbox.PosY - Hitbox.SizeY / 2 <= Hurtbox.PosY + Hurtbox.SizeY / 2
								&& Hitbox.PosX + Hitbox.SizeX / 2 >= Hurtbox.PosX - Hurtbox.SizeX / 2
								&& Hitbox.PosX - Hitbox.SizeX / 2 <= Hurtbox.PosX + Hurtbox.SizeX / 2)
							{
								GameState->SetDrawPriorityFront(this);
								OtherChar->StunTime = 2147483647;
								OtherChar->FaceOpponent();
								OtherChar->HaltMomentum();
								OtherChar->PlayerFlags |= PLF_IsStunned;
								AttackFlags &= ~ATK_HitActive;
								AttackFlags |= ATK_HasHit;
								
								int CollisionDepthX;
								if (Hitbox.PosX < OtherChar->PosX)
								{
									CollisionDepthX = OtherChar->PosX - (Hitbox.PosX + Hitbox.SizeX / 2);
									HitPosX = Hitbox.PosX + CollisionDepthX / 2;
								}
								else
								{
									CollisionDepthX = Hitbox.PosX - Hitbox.SizeX / 2 - OtherChar->PosX;
									HitPosX = Hitbox.PosX - CollisionDepthX / 2;
								}
								int CollisionDepthY;
								int32 CenterPosY = OtherChar->PosY;
								switch (OtherChar->Stance)
								{
								case ACT_Standing:
								case ACT_Jumping:
								default:
									CenterPosY += 200000;
									break;
								case ACT_Crouching:
									CenterPosY += 90000;
									break;
								}
								if (Hitbox.PosY < CenterPosY)
								{
									CollisionDepthY = CenterPosY - (Hitbox.PosY + Hitbox.SizeY / 2);
									HitPosY = Hitbox.PosY + CollisionDepthY / 2;
								}
								else
								{
									CollisionDepthY = Hitbox.PosY - Hitbox.SizeY / 2 - CenterPosY;
									HitPosY = Hitbox.PosY - CollisionDepthY / 2;
								}
								
								TriggerEvent(EVT_HitOrBlock);
								
								if (OtherChar->IsCorrectBlock(HitCommon.BlockType)) //check blocking
								{

									CreateCommonParticle("cmn_guard", POS_Hit, FVector(0, 100, 0), FRotator(HitCommon.HitAngle, 0, 0));
									TriggerEvent(EVT_Block);
									
									const int32 ChipDamage = NormalHit.Damage * HitCommon.ChipDamagePercent / 100;
									OtherChar->CurrentHealth -= ChipDamage;
									
									const FHitData Data = InitHitDataByAttackLevel(false);
									OtherChar->ReceivedHitCommon = HitCommon;
									OtherChar->ReceivedHit = Data;
									
									if (OtherChar->CurrentHealth <= 0)
									{
										EHitAction HACT;
										
										if (OtherChar->PosY == OtherChar->GroundHeight && !(OtherChar->PlayerFlags & PLF_IsKnockedDown))
											HACT = NormalHit.GroundHitAction;
										else
											HACT = NormalHit.AirHitAction;
										
										OtherChar->HandleHitAction(HACT);
									}
									else
									{
										OtherChar->HandleBlockAction();
										OtherChar->AirDashTimer = 0;
										if (OtherChar->PlayerFlags & PLF_TouchingWall)
										{
											Pushback = OtherChar->Pushback;
											OtherChar->Pushback = 0;
										}
									}
									// OtherChar->AddMeter(HitEffect.HitDamage * OtherChar->MeterPercentOnReceiveHitGuard / 100);
									// Player->AddMeter(HitEffect.HitDamage * Player->MeterPercentOnHitGuard / 100);
								}
								else if ((OtherChar->AttackFlags & ATK_IsAttacking) == 0)
								{
									TriggerEvent(EVT_Hit);
									
									const FHitData Data = InitHitDataByAttackLevel(false);
									CreateCommonParticle(HitCommon.HitVFXOverride.GetString(), POS_Hit, FVector(0, 100, 0), FRotator(HitCommon.HitAngle, 0, 0));
									OtherChar->ReceivedHitCommon = HitCommon;
									OtherChar->ReceivedHit = Data;
									EHitAction HACT;
										
									if (OtherChar->PosY == OtherChar->GroundHeight && !(OtherChar->PlayerFlags & PLF_IsKnockedDown))
										HACT = NormalHit.GroundHitAction;
									else
										HACT = NormalHit.AirHitAction;

									OtherChar->HandleHitAction(HACT);
								}
								else
								{
									TriggerEvent(EVT_Hit);
									TriggerEvent(EVT_CounterHit);
									
									const FHitData CounterData = InitHitDataByAttackLevel(true);
									CreateCommonParticle(HitCommon.HitVFXOverride.GetString(), POS_Hit, FVector(0, 100, 0), FRotator(HitCommon.HitAngle, 0, 0));
									OtherChar->ReceivedHitCommon = HitCommon;
									OtherChar->ReceivedHit = CounterData;
									OtherChar->ReceivedHit = CounterData;
									EHitAction HACT;
										
									if (OtherChar->PosY == OtherChar->GroundHeight && !(OtherChar->PlayerFlags & PLF_IsKnockedDown))
										HACT = CounterHit.GroundHitAction;
									else
										HACT = CounterHit.AirHitAction;
									
									OtherChar->HandleHitAction(HACT);
								}
								return;
							}
						}
					}
				}
			}
		}
	}
}

FHitData ABattleObject::InitHitDataByAttackLevel(bool IsCounter)
{
	if (HitCommon.AttackLevel < 0)
		HitCommon.AttackLevel = 0;
	if (HitCommon.AttackLevel > 4)
		HitCommon.AttackLevel = 5;
	
	switch (HitCommon.AttackLevel)
	{
	case 0:
	default:
		if (HitCommon.BlockstopModifier == -1)
			HitCommon.BlockstopModifier = 0;
		if (HitCommon.Blockstun == -1)
			HitCommon.Blockstun = 9;
		if (HitCommon.GroundGuardPushbackX == -1)
			HitCommon.GroundGuardPushbackX = 20000;
		if (HitCommon.AirGuardPushbackX == -1)
			HitCommon.AirGuardPushbackX = 7500;
		if (HitCommon.AirGuardPushbackY == -1)
			HitCommon.AirGuardPushbackY = 15000;
		if (HitCommon.GuardGravity == -1)
			HitCommon.GuardGravity = 1900;
		if (NormalHit.Hitstop == -1)
			NormalHit.Hitstop = 11;
		if (NormalHit.Hitstun == -1)
			NormalHit.Hitstun = 10;
		if (NormalHit.Untech == -1)
			NormalHit.Untech = 10;
		if (NormalHit.Damage == -1)
			NormalHit.Damage = 300;
		if (NormalHit.GroundPushbackX == -1)
			NormalHit.GroundPushbackX = 25000;
		if (NormalHit.AirPushbackX == -1)
			NormalHit.AirPushbackX = 15000;
		if (NormalHit.AirPushbackY == -1)
			NormalHit.AirPushbackY = 30000;
		if (NormalHit.Gravity == -1)
			NormalHit.Gravity = 1900;
		if (CounterHit.Hitstop == -1)
			CounterHit.Hitstop = NormalHit.Hitstop;
		switch (HitCommon.VFXType)
		{
		case EHitVFXType::VFX_Strike:
		case EHitVFXType::VFX_Slash:
			HitCommon.HitVFXOverride.SetString("cmn_hit_s");
			break;
		case EHitVFXType::VFX_Special:
			HitCommon.HitVFXOverride.SetString("cmn_hit_sp");
			break;
		}
		break;
	case 1:
		if (HitCommon.BlockstopModifier == -1)
			HitCommon.BlockstopModifier = 0;
		if (HitCommon.Blockstun == -1)
			HitCommon.Blockstun = 11;
		if (HitCommon.GroundGuardPushbackX == -1)
			HitCommon.GroundGuardPushbackX = 22500;
		if (HitCommon.AirGuardPushbackX == -1)
			HitCommon.AirGuardPushbackX = 7500;
		if (HitCommon.AirGuardPushbackY == -1)
			HitCommon.AirGuardPushbackY = 15025;
		if (HitCommon.GuardGravity == -1)
			HitCommon.GuardGravity = 1900;
		if (NormalHit.Hitstop == -1)
			NormalHit.Hitstop = 12;
		if (NormalHit.Hitstun == -1)
			NormalHit.Hitstun = 12;
		if (NormalHit.Untech == -1)
			NormalHit.Untech = 12;
		if (NormalHit.Damage == -1)
			NormalHit.Damage = 400;
		if (NormalHit.GroundPushbackX == -1)
			NormalHit.GroundPushbackX = 27500;
		if (NormalHit.AirPushbackX == -1)
			NormalHit.AirPushbackX = 15000;
		if (NormalHit.AirPushbackY == -1)
			NormalHit.AirPushbackY = 30050;
		if (NormalHit.Gravity == -1)
			NormalHit.Gravity = 1900;
		if (CounterHit.Hitstop == -1)
			CounterHit.Hitstop = NormalHit.Hitstop + 2;
		switch (HitCommon.VFXType)
		{
		case EHitVFXType::VFX_Strike:
		case EHitVFXType::VFX_Slash:
			HitCommon.HitVFXOverride.SetString("cmn_hit_s");
			break;
		case EHitVFXType::VFX_Special:
			HitCommon.HitVFXOverride.SetString("cmn_hit_sp");
			break;
		}
		break;
	case 2:
		if (HitCommon.BlockstopModifier == -1)
			HitCommon.BlockstopModifier = 0;
		if (HitCommon.Blockstun == -1)
			HitCommon.Blockstun = 13;
		if (HitCommon.GroundGuardPushbackX == -1)
			HitCommon.GroundGuardPushbackX = 27000;
		if (HitCommon.AirGuardPushbackX == -1)
			HitCommon.AirGuardPushbackX = 7500;
		if (HitCommon.AirGuardPushbackY == -1)
			HitCommon.AirGuardPushbackY = 15050;
		if (HitCommon.GuardGravity == -1)
			HitCommon.GuardGravity = 1900;
		if (NormalHit.Hitstop == -1)
			NormalHit.Hitstop = 13;
		if (NormalHit.Hitstun == -1)
			NormalHit.Hitstun = 14;
		if (NormalHit.Untech == -1)
			NormalHit.Untech = 14;
		if (NormalHit.Damage == -1)
			NormalHit.Damage = 600;
		if (NormalHit.GroundPushbackX == -1)
			NormalHit.GroundPushbackX = 30000;
		if (NormalHit.AirPushbackX == -1)
			NormalHit.AirPushbackX = 15000;
		if (NormalHit.AirPushbackY == -1)
			NormalHit.AirPushbackY = 30100;
		if (NormalHit.Gravity == -1)
			NormalHit.Gravity = 1900;
		if (CounterHit.Hitstop == -1)
			CounterHit.Hitstop = NormalHit.Hitstop + 4;
		switch (HitCommon.VFXType)
		{
		case EHitVFXType::VFX_Strike:
		case EHitVFXType::VFX_Slash:
			HitCommon.HitVFXOverride.SetString("cmn_hit_m");
			break;
		case EHitVFXType::VFX_Special:
			HitCommon.HitVFXOverride.SetString("cmn_hit_sp");
			break;
		}
		break;
	case 3:
		if (HitCommon.BlockstopModifier == -1)
			HitCommon.BlockstopModifier = 0;
		if (HitCommon.Blockstun == -1)
			HitCommon.Blockstun = 16;
		if (HitCommon.GroundGuardPushbackX == -1)
			HitCommon.GroundGuardPushbackX = 30000;
		if (HitCommon.AirGuardPushbackX == -1)
			HitCommon.AirGuardPushbackX = 7500;
		if (HitCommon.AirGuardPushbackY == -1)
			HitCommon.AirGuardPushbackY = 15075;
		if (HitCommon.GuardGravity == -1)
			HitCommon.GuardGravity = 1900;
		if (NormalHit.Hitstop == -1)
			NormalHit.Hitstop = 14;
		if (NormalHit.Hitstun == -1)
			NormalHit.Hitstun = 17;
		if (NormalHit.Untech == -1)
			NormalHit.Untech = 16;
		if (NormalHit.Damage == -1)
			NormalHit.Damage = 800;
		if (NormalHit.GroundPushbackX == -1)
			NormalHit.GroundPushbackX = 35000;
		if (NormalHit.AirPushbackX == -1)
			NormalHit.AirPushbackX = 15000;
		if (NormalHit.AirPushbackY == -1)
			NormalHit.AirPushbackY = 30150;
		if (NormalHit.Gravity == -1)
			NormalHit.Gravity = 1900;
		if (CounterHit.Hitstop == -1)
			CounterHit.Hitstop = NormalHit.Hitstop + 8;
		switch (HitCommon.VFXType)
		{
		case EHitVFXType::VFX_Strike:
		case EHitVFXType::VFX_Slash:
			HitCommon.HitVFXOverride.SetString("cmn_hit_m");
			break;
		case EHitVFXType::VFX_Special:
			HitCommon.HitVFXOverride.SetString("cmn_hit_sp");
			break;
		}
		break;
	case 4:
		if (HitCommon.BlockstopModifier == -1)
			HitCommon.BlockstopModifier = 0;
		if (HitCommon.Blockstun == -1)
			HitCommon.Blockstun = 18;
		if (HitCommon.GroundGuardPushbackX == -1)
			HitCommon.GroundGuardPushbackX = 35000;
		if (HitCommon.AirGuardPushbackX == -1)
			HitCommon.AirGuardPushbackX = 7500;
		if (HitCommon.AirGuardPushbackY == -1)
			HitCommon.AirGuardPushbackY = 15100;
		if (HitCommon.GuardGravity == -1)
			HitCommon.GuardGravity = 1900;
		if (NormalHit.Hitstop == -1)
			NormalHit.Hitstop = 15;
		if (NormalHit.Hitstun == -1)
			NormalHit.Hitstun = 19;
		if (NormalHit.Untech == -1)
			NormalHit.Untech = 18;
		if (NormalHit.Damage == -1)
			NormalHit.Damage = 1000;
		if (NormalHit.GroundPushbackX == -1)
			NormalHit.GroundPushbackX = 40000;
		if (NormalHit.AirPushbackX == -1)
			NormalHit.AirPushbackX = 15000;
		if (NormalHit.AirPushbackY == -1)
			NormalHit.AirPushbackY = 30200;
		if (NormalHit.Gravity == -1)
			NormalHit.Gravity = 1900;
		if (CounterHit.Hitstop == -1)
			CounterHit.Hitstop = NormalHit.Hitstop + 12;
		switch (HitCommon.VFXType)
		{
		case EHitVFXType::VFX_Strike:
		case EHitVFXType::VFX_Slash:
			HitCommon.HitVFXOverride.SetString("cmn_hit_l");
			break;
		case EHitVFXType::VFX_Special:
			HitCommon.HitVFXOverride.SetString("cmn_hit_sp");
			break;
		}
		break;
	case 5:
		if (HitCommon.BlockstopModifier == -1)
			HitCommon.BlockstopModifier = 0;
		if (HitCommon.Blockstun == -1)
			HitCommon.Blockstun = 20;
		if (HitCommon.GroundGuardPushbackX == -1)
			HitCommon.GroundGuardPushbackX = 45000;
		if (HitCommon.AirGuardPushbackX == -1)
			HitCommon.AirGuardPushbackX = 7500;
		if (HitCommon.AirGuardPushbackY == -1)
			HitCommon.AirGuardPushbackY = 15125;
		if (HitCommon.GuardGravity == -1)
			HitCommon.GuardGravity = 1900;
		if (NormalHit.Hitstop == -1)
			NormalHit.Hitstop = 18;
		if (NormalHit.Hitstun == -1)
			NormalHit.Hitstun = 22;
		if (NormalHit.Untech == -1)
			NormalHit.Untech = 21;
		if (NormalHit.Damage == -1)
			NormalHit.Damage = 1250;
		if (NormalHit.GroundPushbackX == -1)
			NormalHit.GroundPushbackX = 50000;
		if (NormalHit.AirPushbackX == -1)
			NormalHit.AirPushbackX = 15000;
		if (NormalHit.AirPushbackY == -1)
			NormalHit.AirPushbackY = 30200;
		if (NormalHit.Gravity == -1)
			NormalHit.Gravity = 30250;
		if (CounterHit.Hitstop == -1)
			CounterHit.Hitstop = NormalHit.Hitstop + 16;
		switch (HitCommon.VFXType)
		{
		case EHitVFXType::VFX_Strike:
		case EHitVFXType::VFX_Slash:
			HitCommon.HitVFXOverride.SetString("cmn_hit_l");
			break;
		case EHitVFXType::VFX_Special:
			HitCommon.HitVFXOverride.SetString("cmn_hit_sp");
			break;
		}
		break;
	}
	
	if (CounterHit.Hitstun == -1)
		CounterHit.Hitstun = NormalHit.Hitstun;
	if (CounterHit.Untech == -1)
		CounterHit.Untech = NormalHit.Untech * 2;
	if (CounterHit.Damage == -1)
		CounterHit.Damage = NormalHit.Damage * 110 / 100;
	if (CounterHit.GroundPushbackX == -1)
		CounterHit.GroundPushbackX = NormalHit.GroundPushbackX;
	if (CounterHit.AirPushbackX == -1)
		CounterHit.AirPushbackX = NormalHit.AirPushbackX;
	if (CounterHit.AirPushbackY == -1)
		CounterHit.AirPushbackY = NormalHit.AirPushbackY;
	if (CounterHit.Gravity == -1)
		CounterHit.Gravity = NormalHit.Gravity;
	if (CounterHit.AirPushbackXOverTime.Value == -1)
		CounterHit.AirPushbackXOverTime.Value = NormalHit.AirPushbackXOverTime.Value;
	if (CounterHit.AirPushbackXOverTime.BeginFrame == -1)
		CounterHit.AirPushbackXOverTime.BeginFrame = NormalHit.AirPushbackXOverTime.BeginFrame;
	if (CounterHit.AirPushbackXOverTime.EndFrame == -1)
		CounterHit.AirPushbackXOverTime.EndFrame = NormalHit.AirPushbackXOverTime.EndFrame;
	if (CounterHit.AirPushbackYOverTime.Value == -1)
		CounterHit.AirPushbackYOverTime.Value = NormalHit.AirPushbackYOverTime.Value;
	if (CounterHit.AirPushbackYOverTime.BeginFrame == -1)
		CounterHit.AirPushbackYOverTime.BeginFrame = NormalHit.AirPushbackYOverTime.BeginFrame;
	if (CounterHit.AirPushbackYOverTime.EndFrame == -1)
		CounterHit.AirPushbackYOverTime.EndFrame = NormalHit.AirPushbackYOverTime.EndFrame;
	if (CounterHit.GravityOverTime.Value == -1)
		CounterHit.GravityOverTime.Value = NormalHit.GravityOverTime.Value;
	if (CounterHit.GravityOverTime.BeginFrame == -1)
		CounterHit.GravityOverTime.BeginFrame = NormalHit.GravityOverTime.BeginFrame;
	if (CounterHit.GravityOverTime.EndFrame == -1)
		CounterHit.GravityOverTime.EndFrame = NormalHit.GravityOverTime.EndFrame;
	if (CounterHit.BlowbackLevel == -1)
		CounterHit.BlowbackLevel = NormalHit.BlowbackLevel;
	if (CounterHit.FloatingCrumpleType == FLT_None)
		CounterHit.FloatingCrumpleType = NormalHit.FloatingCrumpleType;

	if (CounterHit.Position.Type == HPT_Non)
		CounterHit.Position.Type = NormalHit.Position.Type;
	if (CounterHit.Position.PosX == -1)
		CounterHit.Position.PosX = NormalHit.Position.PosX;
	if (CounterHit.Position.PosY == -1)
		CounterHit.Position.PosY = NormalHit.Position.PosY;

	if (CounterHit.GroundHitAction == HACT_GroundNormal)
		CounterHit.GroundHitAction = NormalHit.GroundHitAction;
	if (CounterHit.AirHitAction == HACT_AirNormal)
		CounterHit.AirHitAction = NormalHit.AirHitAction;

	if (NormalHit.KnockdownTime == -1)
		NormalHit.KnockdownTime = 12;
	if (CounterHit.KnockdownTime == -1)
		CounterHit.KnockdownTime = NormalHit.KnockdownTime;
	
	if (NormalHit.WallBounce.WallBounceXSpeed == -1)
		NormalHit.WallBounce.WallBounceXSpeed = NormalHit.AirPushbackX;
	if (NormalHit.WallBounce.WallBounceXRate == -1)
		NormalHit.WallBounce.WallBounceXRate = 33;
	if (NormalHit.WallBounce.WallBounceYSpeed == -1)
		NormalHit.WallBounce.WallBounceYSpeed = NormalHit.AirPushbackY;
	if (NormalHit.WallBounce.WallBounceYRate == -1)
		NormalHit.WallBounce.WallBounceYRate = 100;
	if (NormalHit.WallBounce.WallBounceGravity == -1)
		NormalHit.WallBounce.WallBounceGravity = NormalHit.Gravity;

	if (CounterHit.WallBounce.WallBounceXSpeed == -1)
		CounterHit.WallBounce.WallBounceXSpeed = NormalHit.WallBounce.WallBounceXSpeed;
	if (CounterHit.WallBounce.WallBounceXRate == -1)
		CounterHit.WallBounce.WallBounceXRate = NormalHit.WallBounce.WallBounceXRate;
	if (CounterHit.WallBounce.WallBounceYSpeed == -1)
		CounterHit.WallBounce.WallBounceYSpeed = NormalHit.WallBounce.WallBounceYSpeed;
	if (CounterHit.WallBounce.WallBounceYRate == -1)
		CounterHit.WallBounce.WallBounceYRate = NormalHit.WallBounce.WallBounceYRate;
	if (CounterHit.WallBounce.WallBounceGravity == -1)
		CounterHit.WallBounce.WallBounceGravity = NormalHit.WallBounce.WallBounceGravity;
	
	if (NormalHit.GroundBounce.GroundBounceXSpeed == -1)
		NormalHit.GroundBounce.GroundBounceXSpeed = NormalHit.AirPushbackX;
	if (NormalHit.GroundBounce.GroundBounceXRate == -1)
		NormalHit.GroundBounce.GroundBounceXRate = 100;
	if (NormalHit.GroundBounce.GroundBounceYSpeed == -1)
		NormalHit.GroundBounce.GroundBounceYSpeed = NormalHit.AirPushbackY;
	if (NormalHit.GroundBounce.GroundBounceYRate == -1)
		NormalHit.GroundBounce.GroundBounceYRate = 100;
	if (NormalHit.GroundBounce.GroundBounceGravity == -1)
		NormalHit.GroundBounce.GroundBounceGravity = NormalHit.Gravity;

	if (CounterHit.GroundBounce.GroundBounceXSpeed == -1)
		CounterHit.GroundBounce.GroundBounceXSpeed = NormalHit.GroundBounce.GroundBounceXSpeed;
	if (CounterHit.GroundBounce.GroundBounceXRate == -1)
		CounterHit.GroundBounce.GroundBounceXRate = NormalHit.GroundBounce.GroundBounceXRate;
	if (CounterHit.GroundBounce.GroundBounceYSpeed == -1)
		CounterHit.GroundBounce.GroundBounceYSpeed = NormalHit.GroundBounce.GroundBounceYSpeed;
	if (CounterHit.GroundBounce.GroundBounceYRate == -1)
		CounterHit.GroundBounce.GroundBounceYRate = NormalHit.GroundBounce.GroundBounceYRate;
	if (CounterHit.GroundBounce.GroundBounceGravity == -1)
		CounterHit.GroundBounce.GroundBounceGravity = NormalHit.GroundBounce.GroundBounceGravity;
	
	FHitData Data;
	if (!IsCounter)
		Data = NormalHit;
	else
		Data = CounterHit;
	
	return Data;
}

void ABattleObject::HandleClashCollision(ABattleObject* OtherObj)
{
	if (AttackFlags & ATK_IsAttacking && AttackFlags & ATK_HitActive && OtherObj != Player && OtherObj->AttackFlags & ATK_IsAttacking && OtherObj->AttackFlags & ATK_HitActive)
	{
		for (int i = 0; i < CollisionArraySize; i++)
		{
			if (Boxes[i].Type == BOX_Hit)
			{
				for (int j = 0; j < CollisionArraySize; j++)
				{
					if (OtherObj->Boxes[j].Type == BOX_Hit)
					{
						FCollisionBox Hitbox = Boxes[i];

						FCollisionBox OtherHitbox = OtherObj->Boxes[j];

						if (Direction == DIR_Right)
						{
							Hitbox.PosX += PosX;
						}
						else
						{
							Hitbox.PosX = -Hitbox.PosX + PosX;  
						}
						Hitbox.PosY += PosY;
						if (OtherObj->Direction == DIR_Right)
						{
							OtherHitbox.PosX += OtherObj->PosX;
						}
						else
						{
							OtherHitbox.PosX = -OtherHitbox.PosX + OtherObj->PosX;  
						}
						OtherHitbox.PosY += OtherObj->PosY;
							
						if (Hitbox.PosY + Hitbox.SizeY / 2 >= OtherHitbox.PosY - OtherHitbox.SizeY / 2
							&& Hitbox.PosY - Hitbox.SizeY / 2 <= OtherHitbox.PosY + OtherHitbox.SizeY / 2
							&& Hitbox.PosX + Hitbox.SizeX / 2 >= OtherHitbox.PosX - OtherHitbox.SizeX / 2
							&& Hitbox.PosX - Hitbox.SizeX / 2 <= OtherHitbox.PosX + OtherHitbox.SizeX / 2)
						{
							int CollisionDepthX;
							if (Hitbox.PosX < OtherHitbox.PosX)
							{
								CollisionDepthX = OtherHitbox.PosX - OtherHitbox.SizeX / 2 - (Hitbox.PosX + Hitbox.SizeX / 2);
								HitPosX = Hitbox.PosX - CollisionDepthX;
							}
							else
							{
								CollisionDepthX = Hitbox.PosX - Hitbox.SizeX / 2 - (OtherHitbox.PosX + OtherHitbox.SizeX / 2);
								HitPosX = Hitbox.PosX + CollisionDepthX;
							}
							int CollisionDepthY;
							if (Hitbox.PosY < OtherHitbox.PosY)
							{
								CollisionDepthY = OtherHitbox.PosY - OtherHitbox.SizeY / 2 - (Hitbox.PosY + Hitbox.SizeY / 2);
								HitPosY = Hitbox.PosY - CollisionDepthY;
							}
							else
							{
								CollisionDepthY = Hitbox.PosY - Hitbox.SizeY / 2 - (OtherHitbox.PosY + OtherHitbox.SizeY / 2);
								HitPosY = Hitbox.PosY + CollisionDepthY;
							}
							
							if (IsPlayer && OtherObj->IsPlayer)
							{
								Hitstop = 16;
								OtherObj->Hitstop = 16;
								AttackFlags &= ~ATK_HitActive;
								OtherObj->AttackFlags &= ~ATK_HitActive;
								OtherObj->HitPosX = HitPosX;
								OtherObj->HitPosY = HitPosY;
								Player->EnableAttacks();
								Player->EnableCancelIntoSelf(true);
								Player->EnableState(ENB_ForwardDash);
								OtherObj->Player->EnableAttacks();
								OtherObj->Player->EnableCancelIntoSelf(true);
								OtherObj->Player->EnableState(ENB_ForwardDash);
								TriggerEvent(EVT_HitOrBlock);
								OtherObj->TriggerEvent(EVT_HitOrBlock);
								CreateCommonParticle("cmn_hit_clash", POS_Hit, FVector(0, 100, 0));
                                // PlayCommonSound("HitClash");
								return;
							}
							if (!IsPlayer && !OtherObj->IsPlayer)
							{
								OtherObj->Hitstop = 16;
								Hitstop = 16;
								AttackFlags &= ~ATK_HitActive;
								OtherObj->AttackFlags &= ~ATK_HitActive;
								OtherObj->HitPosX = HitPosX;
								OtherObj->HitPosY = HitPosY;
								OtherObj->TriggerEvent(EVT_HitOrBlock);
								TriggerEvent(EVT_HitOrBlock);
								CreateCommonParticle("cmn_hit_clash", POS_Hit, FVector(0, 100, 0));
                                //PlayCommonSound("HitClash");
								return;
							}
							return;
						}
					}
				}
			}
		}
	}
}

void ABattleObject::HandleFlip()
{
	const EObjDir CurrentDir = Direction;
	if (!Player->Enemy) return;
	if (PosX < Player->Enemy->PosX)
	{
		SetFacing(DIR_Right);
	}
	else if (PosX > Player->Enemy->PosX)
	{
		SetFacing(DIR_Left);
	}
	if (CurrentDir != Direction)
	{
		SpeedX = -SpeedX;
		Inertia = -Inertia;
		if (IsPlayer)
		{
			Player->StoredInputBuffer.FlipInputsInBuffer();
			if (Player->Stance == ACT_Standing && Player->EnableFlags & ENB_Standing)
				Player->JumpToState("StandFlip");
			else if (Player->Stance == ACT_Crouching && Player->EnableFlags & ENB_Crouching)
				Player->JumpToState("CrouchFlip");
			else if (Player->EnableFlags & ENB_Jumping)
				Player->JumpToState("JumpFlip");
		}
	}
}

void ABattleObject::PosTypeToPosition(EPosType Type, int32* OutPosX, int32* OutPosY) const
{
	switch (Type)
	{
	case POS_Self:
		*OutPosX = PosX;
		*OutPosY = PosY;
		break;
	case POS_Player:
		*OutPosX = Player->PosX;
		*OutPosY = Player->PosY;
		break;
	case POS_Center:
		*OutPosX = PosX;
		if (!IsPlayer)
		{
			*OutPosY = PosY;
			break;
		}
		{
			int32 CenterPosY = PosY;
			switch (Player->Stance)
			{
			case ACT_Standing:
			case ACT_Jumping:
			default:
				CenterPosY += 200000;
				break;
			case ACT_Crouching:
				CenterPosY += 90000;
				break;
			}
			*OutPosY = CenterPosY;
		}
		break;
	case POS_Enemy:
		*OutPosX = Player->Enemy->PosX;
		*OutPosY = Player->Enemy->PosY;
		break;
	case POS_Hit:
		*OutPosX = HitPosX;
		*OutPosY = HitPosY;
		break;
	default:
		break;
	}
}

void ABattleObject::TriggerEvent(EEventType EventType)
{
	UState* State = ObjectState;
	if (IsPlayer)
		State = Player->StoredStateMachine.CurrentState;
	if (!IsValid(State))
		return;
	UFunction* const Func = State->FindFunction(FName(EventHandlers[EventType].FunctionName.GetString()));
	if (IsValid(Func) && Func->ParmsSize == 0)
	{
		State->ProcessEvent(Func, nullptr);
	}
}

void ABattleObject::SaveForRollback(unsigned char* Buffer) const
{
	FMemory::Memcpy(Buffer, &ObjSync, SizeOfBattleObject);
}

void ABattleObject::LoadForRollback(const unsigned char* Buffer)
{
	FMemory::Memcpy(&ObjSync, Buffer, SizeOfBattleObject);
	if (!IsPlayer)
	{
		const int StateIndex = Player->ObjectStateNames.Find(ObjectStateName.GetString());
		if (StateIndex != INDEX_NONE)
		{
			ObjectState = DuplicateObject(Player->ObjectStates[StateIndex], this);
			ObjectState->Parent = this;
		}
	}
}

void ABattleObject::LogForSyncTestFile(FILE* file)
{
	if(file)
	{
		fprintf(file,"BattleActor:\n");
		fprintf(file,"\tPosX: %d\n", PosX);
		fprintf(file,"\tPosY: %d\n", PosY);
		fprintf(file,"\tPrevPosX: %d\n", PrevPosX);
		fprintf(file,"\tPrevPosY: %d\n", PrevPosY);
		fprintf(file,"\tSpeedX: %d\n", SpeedX);
		fprintf(file,"\tSpeedY: %d\n", SpeedY);
		fprintf(file,"\tGravity: %d\n", Gravity);
		fprintf(file,"\tInertia: %d\n", Inertia);
		fprintf(file,"\tActionTime: %d\n", ActionTime);
		fprintf(file,"\tPushHeight: %d\n", PushHeight);
		fprintf(file,"\tPushHeightLow: %d\n", PushHeightLow);
		fprintf(file,"\tPushWidth: %d\n", PushWidth);
		fprintf(file,"\tStunTime: %d\n", StunTime);
		fprintf(file,"\tStunTimeMax: %d\n", StunTimeMax);
		fprintf(file,"\tHitstop: %d\n", Hitstop);
		fprintf(file,"\tCelName: %s\n", CelName.GetString());
		fprintf(file,"\tFacingRight: %d\n", AttackFlags);
		fprintf(file,"\tDirection: %d\n", static_cast<int>(Direction));
		fprintf(file,"\tMiscFlags: %d\n", MiscFlags);
		fprintf(file,"\tCelIndex: %d\n", CelIndex);
		fprintf(file,"\tTimeUntilNextCel: %d\n", TimeUntilNextCel);
		fprintf(file,"\tAnimFrame: %d\n", AnimFrame);
	}
}

void ABattleObject::UpdateVisualLocation()
{
	if (IsValid(LinkedParticle))
	{
		FVector FinalScale = ScaleForLink;
		if (Direction == DIR_Left)
			FinalScale.Y = -FinalScale.Y;
		LinkedParticle->SetRelativeScale3D(FinalScale);
	}
	for (const auto LinkedMesh : LinkedMeshes)
	{
		if (IsValid(LinkedMesh))
		{
			FVector FinalScale = ScaleForLink;
			if (Direction == DIR_Left)
				FinalScale.Y = -FinalScale.Y;
			LinkedMesh->SetRelativeScale3D(FinalScale);
		}
	}
	SetActorLocation(FVector(static_cast<float>(PosX) / COORD_SCALE, static_cast<float>(PosZ) / COORD_SCALE, static_cast<float>(PosY) / COORD_SCALE));
}

void ABattleObject::FuncCall(const FName& FuncName) const
{
	UState* CurrentState = ObjectState;
	if (IsPlayer)
		CurrentState = Player->StoredStateMachine.CurrentState;

	UFunction* const Func = CurrentState->FindFunction(FuncName);
	if (IsValid(Func) && Func->ParmsSize == 0)
	{
		CurrentState->ProcessEvent(Func, nullptr);
	}
}

void ABattleObject::GetBoxes()
{
	if (Player->CommonCollisionData != nullptr)
	{
		for (int i = 0; i < Player->CommonCollisionData->CollisionFrames.Num(); i++)
		{
			if (Player->CommonCollisionData->CollisionFrames[i].CelName == CelName.GetString())
			{
				AnimName.SetString(Player->CommonCollisionData->CollisionFrames[i].AnimName);
				AnimFrame = Player->CommonCollisionData->CollisionFrames[i].AnimFrame;
				for (int j = 0; j < CollisionArraySize; j++)
				{
					if (j < Player->CommonCollisionData->CollisionFrames[i].Boxes.Num())
					{
						FCollisionBox CollisionBoxInternal;
						CollisionBoxInternal.Type = Player->CommonCollisionData->CollisionFrames[i].Boxes[j].Type;
						CollisionBoxInternal.PosX = Player->CommonCollisionData->CollisionFrames[i].Boxes[j].PosX;
						CollisionBoxInternal.PosY = Player->CommonCollisionData->CollisionFrames[i].Boxes[j].PosY;
						CollisionBoxInternal.SizeX = Player->CommonCollisionData->CollisionFrames[i].Boxes[j].SizeX;
						CollisionBoxInternal.SizeY = Player->CommonCollisionData->CollisionFrames[i].Boxes[j].SizeY;
						Boxes[j] = CollisionBoxInternal;
					}
					else
					{
						Boxes[j].Type = BOX_Hurt;
						Boxes[j].PosX = -10000000;
						Boxes[j].PosY = -10000000;
						Boxes[j].SizeX = 0;
						Boxes[j].SizeY = 0;
					}
				}
				return;
			}
		}
		for (int j = 0; j < CollisionArraySize; j++)
		{
			Boxes[j].Type = BOX_Hurt;
			Boxes[j].PosX = -10000000;
			Boxes[j].PosY = -10000000;
			Boxes[j].SizeX = 0;
			Boxes[j].SizeY = 0;
		}
	}
	if (Player->CollisionData != nullptr)
	{
		for (int i = 0; i < Player->CollisionData->CollisionFrames.Num(); i++)
		{
			if (Player->CollisionData->CollisionFrames[i].CelName == CelName.GetString())
			{
				AnimName.SetString(Player->CollisionData->CollisionFrames[i].AnimName);
				AnimFrame = Player->CollisionData->CollisionFrames[i].AnimFrame;
				for (int j = 0; j < CollisionArraySize; j++)
				{
					if (j < Player->CollisionData->CollisionFrames[i].Boxes.Num())
					{
						FCollisionBox CollisionBoxInternal;
						CollisionBoxInternal.Type = Player->CollisionData->CollisionFrames[i].Boxes[j].Type;
						CollisionBoxInternal.PosX = Player->CollisionData->CollisionFrames[i].Boxes[j].PosX;
						CollisionBoxInternal.PosY = Player->CollisionData->CollisionFrames[i].Boxes[j].PosY;
						CollisionBoxInternal.SizeX = Player->CollisionData->CollisionFrames[i].Boxes[j].SizeX;
						CollisionBoxInternal.SizeY = Player->CollisionData->CollisionFrames[i].Boxes[j].SizeY;
						Boxes[j] = CollisionBoxInternal;
					}
					else
					{
						Boxes[j].Type = BOX_Hurt;
						Boxes[j].PosX = -10000000;
						Boxes[j].PosY = -10000000;
						Boxes[j].SizeX = 0;
						Boxes[j].SizeY = 0;
					}
				}
				return;
			}
		}
		for (int j = 0; j < CollisionArraySize; j++)
		{
			Boxes[j].Type = BOX_Hurt;
			Boxes[j].PosX = -10000000;
			Boxes[j].PosY = -10000000;
			Boxes[j].SizeX = 0;
			Boxes[j].SizeY = 0;
		}
	}
}

void ABattleObject::InitObject()
{
	if (IsPlayer)
		return;
	if (IsValid(LinkedParticle))
	{
		LinkedParticle->Deactivate();
		LinkedParticle = nullptr;
	}
	for (auto LinkedMesh : LinkedMeshes)
	{
		if (IsValid(LinkedMesh))
		{
			LinkedMesh->Deactivate();
			LinkedMesh->SetRelativeLocation(FVector(0,0,-100000));
			LinkedMesh = nullptr;
		}
	}
	GameState->SetDrawPriorityFront(this);
	ObjectState->Parent = this;
	SetActorLocation(FVector(static_cast<float>(PosX) / COORD_SCALE, static_cast<float>(PosZ) / COORD_SCALE, static_cast<float>(PosY) / COORD_SCALE)); //set visual location and scale in unreal
	if (Direction == DIR_Left)
	{
		SetActorScale3D(FVector(-1, 1, 1));
	}
	else
	{
		SetActorScale3D(FVector(1, 1, 1));
	}
}

void ABattleObject::Update()
{
	if (Direction == DIR_Left)
	{
		SetActorScale3D(FVector(-1, 1, 1));
	}
	else
	{
		SetActorScale3D(FVector(1, 1, 1));
	}

	L = PosX - PushWidth / 2; //sets pushboxes
	R = PosX + PushWidth / 2;
	if (Direction == DIR_Right)
		R += PushWidthFront;
	else
		L -= PushWidthFront;
	T = PosY + PushHeight;
	B = PosY - PushHeightLow;
	
	if (SuperFreezeTimer > 0)
	{
		if (SuperFreezeTimer == 1)
		{
			TriggerEvent(EVT_SuperFreezeEnd);
			Player->BattleHudVisibility(true);
		}
		SuperFreezeTimer--;
		return;
	}
		
	if (Hitstop > 0) //break if hitstop active.
	{
		Hitstop--;
		return;
	}
	
	if (MiscFlags & MISC_FlipEnable)
		HandleFlip();

	TriggerEvent(EVT_Update);
	Move();
	
	if (PosY == GroundHeight && PrevPosY != GroundHeight)
	{
		TriggerEvent(EVT_Landing);
		SpeedX = 0;
	}

	if (!IsPlayer)
	{
		if (ActionTime == 0)
		{
			TriggerEvent(EVT_Enter);
		}
		
		ObjectState->CallExec();
		TimeUntilNextCel--;
		if (TimeUntilNextCel == 0)
			CelIndex++;
		
		GetBoxes();
		GameState->SetScreenBounds();
		GameState->SetWallCollision();
		ActionTime++;
		UpdateVisualLocation();
	}
}

void ABattleObject::ResetObject()
{
	if (IsPlayer)
		return;
	if (IsValid(LinkedParticle))
	{
		LinkedParticle->SetVisibility(false);
		LinkedParticle = nullptr;
	}
	for (auto LinkedMesh : LinkedMeshes)
	{
		if (IsValid(LinkedMesh))
		{
			LinkedMesh->SetVisibility(false);
			LinkedMesh->SetRelativeLocation(FVector(0,0,-100000));
			LinkedMesh = nullptr;
		}
	}
	IsActive = false;
	PosX = 0;
	PosY = 0;
	PosZ = 0;
	PrevPosX = 0;
	PrevPosY = 0;
	PrevPosZ = 0;
	SpeedX = 0;
	SpeedY = 0;
	SpeedZ = 0;
	Gravity = 1900;
	Inertia = 0;
	ActionTime = 0;
	PushHeight = 0;
	PushHeightLow = 0;
	PushWidth = 0;
	PushWidthFront  = 0;
	Hitstop = 0;
	L = 0;
	R = 0;
	T = 0;
	B = 0;
	HitCommon = FHitDataCommon();
	NormalHit = FHitData();
	CounterHit = FHitData();
	ReceivedHitCommon = FHitDataCommon();
	ReceivedHit = FHitData();
	AttackFlags = ATK_AttackProjectileAttribute;
	StunTime = 0;
	StunTimeMax = 0;
	Hitstop = 0;
	MiscFlags = 0;
	Direction = DIR_Right;
	SpeedXRate = 100;
	SpeedXRatePerFrame = 100;
	SpeedYRate = 100;
	SpeedYRatePerFrame = 100;
	SpeedZRate = 100;
	SpeedZRatePerFrame = 100;
	GroundHeight = 0;
	ReturnReg = false;
	ActionReg1 = 0;
	ActionReg2 = 0;
	ActionReg3 = 0;
	ActionReg4 = 0;
	ActionReg5 = 0;
	ActionReg6 = 0;
	ActionReg7 = 0;
	ActionReg8 = 0;
	ObjectReg1 = 0;
	ObjectReg2 = 0;
	ObjectReg3 = 0;
	ObjectReg4 = 0;
	ObjectReg5 = 0;
	ObjectReg6 = 0;
	ObjectReg7 = 0;
	ObjectReg8 = 0;
	SuperFreezeTimer = 0;
	CelName.SetString("");
	AnimName.SetString("");
	AnimFrame = 0;
	CelIndex = 0;
	TimeUntilNextCel = 0;
	for (auto& Handler : EventHandlers)
		Handler = FEventHandler();
	EventHandlers[EVT_Enter].FunctionName.SetString("Init");	
	HitPosX = 0;
	HitPosY = 0;
	for (auto& Box : Boxes)
	{
		Box = FCollisionBox();
	}
	ObjectStateName.SetString("");
	ObjectID = 0;
	Player = nullptr;
	GameState->SetDrawPriorityFront(this);
}

void ABattleObject::InitEventHandler(EEventType EventType, FName FuncName)
{
	EventHandlers[EventType].FunctionName.SetString(FuncName.ToString());
}

void ABattleObject::RemoveEventHandler(EEventType EventType)
{
	EventHandlers[EventType].FunctionName.SetString("");
}

FString ABattleObject::GetCelName()
{
	return CelName.GetString();
}

FString ABattleObject::GetAnimName()
{
	return AnimName.GetString();
}

FString ABattleObject::GetLabelName()
{
	return LabelName.GetString();
}

void ABattleObject::SetCelName(FString InName)
{
	CelName.SetString(InName);
}

void ABattleObject::GotoLabel(FString InName, bool ResetState)
{
	LabelName.SetString(InName);
	if (IsPlayer && ResetState)
		Player->JumpToState(Player->GetCurrentStateName(), true);
	else
		GotoLabelActive = true;
}

void ABattleObject::AddPosXWithDir(int InPosX)
{
	if (Direction == DIR_Right)
	{
		PosX += InPosX;
	}
	else
	{
		PosX -= InPosX;
	}
}

void ABattleObject::SetSpeedXRaw(int InSpeedX)
{
	if (Direction == DIR_Right)
	{
		SpeedX = InSpeedX;
	}
	else
	{
		SpeedX = -InSpeedX;
	}
}

void ABattleObject::AddSpeedXRaw(int InSpeedX)
{
	if (Direction == DIR_Right)
	{
		SpeedX += InSpeedX;
	}
	else
	{
		SpeedX -= InSpeedX;
	}
}

int32 ABattleObject::CalculateDistanceBetweenPoints(EDistanceType Type, EObjType Obj1, EPosType Pos1, EObjType Obj2,
	EPosType Pos2)
{
	const ABattleObject* Actor1 = GetBattleObject(Obj1);
	const ABattleObject* Actor2 = GetBattleObject(Obj2);
	if (IsValid(Actor1) && IsValid(Actor2))
	{
		int32 PosX1 = 0;
		int32 PosX2 = 0;
		int32 PosY1 = 0;
		int32 PosY2 = 0;

		Actor1->PosTypeToPosition(Pos1, &PosX1, &PosY1);
		Actor2->PosTypeToPosition(Pos2, &PosX2, &PosY2);

		int32 ObjDist;
		
		switch (Type)
		{
		case DIST_Distance:
			ObjDist = isqrt(static_cast<int64>(PosX2 - PosX1) * static_cast<int64>(PosX2 - PosX1) + static_cast<int64>(PosY2 - PosY1) * static_cast<int64>(PosY2 - PosY1));
			break;
		case DIST_DistanceX:
			ObjDist = abs(PosX2 - PosX1);
			break;
		case DIST_DistanceY:
			ObjDist = abs(PosY2 - PosY1);
			break;
		case DIST_FrontDistanceX:
			{
				int DirFlag = 1;
				if (Actor1->Direction == DIR_Left)
				{
					DirFlag = -1;
				}
				ObjDist = abs(PosX2 - PosX1) * DirFlag;
			}
			break;
		default:
			return 0;
		}
		return ObjDist;
	}
	return 0;
}

void ABattleObject::SetFacing(EObjDir NewDir)
{
	Direction = NewDir;
}

void ABattleObject::FlipCharacter()
{
	if (Direction == DIR_Right)
		Direction = DIR_Left;
	else
		Direction = DIR_Right;
}

void ABattleObject::FaceOpponent()
{
	const EObjDir CurrentDir = Direction;
	if (!Player->Enemy) return;
	if (PosX < Player->Enemy->PosX)
	{
		SetFacing(DIR_Right);
	}
	else if (PosX > Player->Enemy->PosX)
	{
		SetFacing(DIR_Left);
	}
	if (CurrentDir != Direction)
	{
		SpeedX = -SpeedX;
		Inertia = -Inertia;
	}
}

bool ABattleObject::CheckIsGrounded()
{
	return PosY <= GroundHeight;
}

void ABattleObject::EnableHit(bool Enabled)
{
	if (Enabled)
	{
		AttackFlags |= ATK_HitActive;
	}
	else
	{
		AttackFlags &= ~ATK_HitActive;
	}
	AttackFlags &= ~ATK_HasHit;
}

void ABattleObject::SetAttacking(bool Attacking)
{
	if (Attacking)
	{
		AttackFlags |= ATK_IsAttacking;
	}
	else
	{
		AttackFlags &= ~ATK_IsAttacking;
	}
	AttackFlags &= ~ATK_HasHit;
}

void ABattleObject::DeactivateObject()
{
	if (IsPlayer) // Don't use on players
		return;
	// Remove from player cache
	for (int i = 0; i < 32; i++)
	{
		if (this == Player->ChildBattleObjects[i])
		{
			Player->ChildBattleObjects[i] = nullptr;
			break;
		}
	}
	for (int i = 0; i < 16; i++)
	{
		if (this == Player->StoredBattleObjects[i])
		{
			Player->StoredBattleObjects[i] = nullptr;
			break;
		}
	}

	// Wait until the next frame to complete
	MiscFlags |= MISC_DeactivateOnNextUpdate;
}

void ABattleObject::EnableFlip(bool Enabled)
{
	if (Enabled)
	{
		MiscFlags |= MISC_FlipEnable;
	}
	else
	{
		MiscFlags = MiscFlags & ~MISC_FlipEnable;
	}
}

void ABattleObject::EnableInertia()
{
	MiscFlags |= MISC_InertiaEnable;
}

void ABattleObject::DisableInertia()
{
	MiscFlags = MiscFlags & ~MISC_InertiaEnable;
}

void ABattleObject::HaltMomentum()
{
	SpeedX = 0;
	SpeedY = 0;
	SpeedZ = 0;
	Gravity = 0;
	Inertia = 0;
}

void ABattleObject::SetPushCollisionActive(bool Active)
{
	if (Active)
		MiscFlags |= MISC_PushCollisionActive;
	else
		MiscFlags &= ~MISC_PushCollisionActive;
}

void ABattleObject::CreateCommonParticle(FString Name, EPosType PosType, FVector Offset, FRotator Rotation)
{
	if (Player->CommonParticleData != nullptr)
	{
		for (FParticleStruct ParticleStruct : Player->CommonParticleData->ParticleStructs)
		{
			if (ParticleStruct.Name == Name)
			{
				if (Direction == DIR_Left)
				{
					Rotation.Pitch = -Rotation.Pitch;
					Offset = FVector(-Offset.X, Offset.Y, Offset.Z);
				}
				int32 TmpPosX;
				int32 TmpPosY;
				PosTypeToPosition(PosType, &TmpPosX, &TmpPosY);
				const FVector FinalLocation = Offset + FVector(TmpPosX / COORD_SCALE, 0, TmpPosY / COORD_SCALE);
				GameState->ParticleManager->NiagaraComponents.Add(UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ParticleStruct.ParticleSystem, FinalLocation, Rotation, GetActorScale()));
				UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(GameState->ParticleManager->NiagaraComponents.Last());
				NiagaraComponent->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
				NiagaraComponent->SetNiagaraVariableFloat("SpriteRotate", Rotation.Pitch);
				if (Direction == DIR_Left)
				{
					NiagaraComponent->SetNiagaraVariableVec2("UVScale", FVector2D(-1, 1));
					NiagaraComponent->SetNiagaraVariableVec2("PivotOffset", FVector2D(0, 0.5));
					NiagaraComponent->SetNiagaraVariableFloat("SpriteRotate", -Rotation.Pitch);
				}
				break;
			}
		}
	}
}

void ABattleObject::CreateCharaParticle(FString Name, EPosType PosType, FVector Offset, FRotator Rotation)
{
	if (Player->CharaParticleData != nullptr)
	{
		for (FParticleStruct ParticleStruct : Player->CharaParticleData->ParticleStructs)
		{
			if (ParticleStruct.Name == Name)
			{
				if (Direction == DIR_Left)
					Offset = FVector(-Offset.X, Offset.Y, Offset.Z);
				int32 TmpPosX;
				int32 TmpPosY;
				PosTypeToPosition(PosType, &TmpPosX, &TmpPosY);
				const FVector FinalLocation = Offset + FVector(TmpPosX / COORD_SCALE, 0, TmpPosY / COORD_SCALE);
				GameState->ParticleManager->NiagaraComponents.Add(UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ParticleStruct.ParticleSystem, FinalLocation, Rotation, GetActorScale()));
				UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(GameState->ParticleManager->NiagaraComponents.Last());
				NiagaraComponent->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
				NiagaraComponent->SetNiagaraVariableFloat("SpriteRotate", Rotation.Pitch);
				if (Direction == DIR_Left)
				{
					NiagaraComponent->SetNiagaraVariableVec2("UVScale", FVector2D(-1, 1));
					NiagaraComponent->SetNiagaraVariableVec2("PivotOffset", FVector2D(0, 0.5));
					NiagaraComponent->SetNiagaraVariableFloat("SpriteRotate", -Rotation.Pitch);
				}
				break;
			}
		}
	}
}

void ABattleObject::LinkCommonParticle(FString Name)
{
	if (IsPlayer)
		return;
	if (Player->CommonParticleData != nullptr)
	{
		for (FParticleStruct ParticleStruct : Player->CommonParticleData->ParticleStructs)
		{
			if (ParticleStruct.Name == Name)
			{
				FVector Scale;
				if (Direction == DIR_Left)
				{
					Scale = FVector(-1,1, 1);
				}
				else
				{
					Scale = FVector(1, 1, 1);
				}
				GameState->ParticleManager->NiagaraComponents.Add(UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ParticleStruct.ParticleSystem, GetActorLocation(), GetActorRotation(), Scale));
				UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(GameState->ParticleManager->NiagaraComponents.Last());
				NiagaraComponent->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
				NiagaraComponent->SetNiagaraVariableBool("NeedsRollback", true);
				LinkedParticle = NiagaraComponent;
				LinkedParticle->SetVisibility(false);
				if (Direction == DIR_Left)
					NiagaraComponent->SetNiagaraVariableVec2("UVScale", FVector2D(-1, 1));
				break;
			}
		}
	}
}

void ABattleObject::LinkCharaParticle(FString Name)
{
	if (IsPlayer)
		return;
	if (Player->CharaParticleData != nullptr)
	{
		for (FParticleStruct ParticleStruct : Player->CharaParticleData->ParticleStructs)
		{
			if (ParticleStruct.Name == Name)
			{
				FVector Scale;
				if (Direction == DIR_Left)
				{
					Scale = FVector(-1,1, 1);
				}
				else
				{
					Scale = FVector(1, 1, 1);
				}
				GameState->ParticleManager->NiagaraComponents.Add(UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ParticleStruct.ParticleSystem, GetActorLocation(), GetActorRotation(), Scale));
				UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(GameState->ParticleManager->NiagaraComponents.Last());
				NiagaraComponent->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
				NiagaraComponent->SetNiagaraVariableBool("NeedsRollback", true);
				LinkedParticle = NiagaraComponent;
				LinkedParticle->SetVisibility(false);
				if (Direction == DIR_Left)
					NiagaraComponent->SetNiagaraVariableVec2("UVScale", FVector2D(-1, 1));
				break;
			}
		}
	}
}

int32 ABattleObject::GenerateRandomNumber(int32 Min, int32 Max)
{
	if (Min > Max)
	{
		const int32 Temp = Max;
		Max = Min;
		Min = Temp;
	}
	int32 Result = FRandomManager::GenerateRandomNumber();
	Result = Result % (Max - Min + 1);
	return Result;
}

ABattleObject* ABattleObject::GetBattleObject(EObjType Type)
{
	switch (Type)
	{
	case OBJ_Self:
		return this;
	case OBJ_Enemy:
		return Player->Enemy;
	case OBJ_Parent:
		return Player;
	case OBJ_Child0:
		if (IsPlayer && Player->StoredBattleObjects[0])
			if (Player->StoredBattleObjects[0]->IsActive)
				return Player->StoredBattleObjects[0];
		return nullptr;
	case OBJ_Child1:
		if (IsPlayer && Player->StoredBattleObjects[1])
			if (Player->StoredBattleObjects[1]->IsActive)
				return Player->StoredBattleObjects[1];
		return nullptr;
	case OBJ_Child2:
		if (IsPlayer && Player->StoredBattleObjects[2])
			if (Player->StoredBattleObjects[2]->IsActive)
				return Player->StoredBattleObjects[2];
		return nullptr;
	case OBJ_Child3:
		if (IsPlayer && Player->StoredBattleObjects[3])
			if (Player->StoredBattleObjects[3]->IsActive)
				return Player->StoredBattleObjects[3];
		return nullptr;
	case OBJ_Child4:
		if (IsPlayer && Player->StoredBattleObjects[4])
			if (Player->StoredBattleObjects[4]->IsActive)
				return Player->StoredBattleObjects[4];
		return nullptr;
	case OBJ_Child5:
		if (IsPlayer && Player->StoredBattleObjects[5])
			if (Player->StoredBattleObjects[5]->IsActive)
				return Player->StoredBattleObjects[5];
		return nullptr;
	case OBJ_Child6:
		if (IsPlayer && Player->StoredBattleObjects[6])
			if (Player->StoredBattleObjects[6]->IsActive)
				return Player->StoredBattleObjects[6];
		return nullptr;
	case OBJ_Child7:
		if (IsPlayer && Player->StoredBattleObjects[7])
			if (Player->StoredBattleObjects[7]->IsActive)
				return Player->StoredBattleObjects[7];
		return nullptr;
	case OBJ_Child8:
		if (IsPlayer && Player->StoredBattleObjects[8])
			if (Player->StoredBattleObjects[8]->IsActive)
				return Player->StoredBattleObjects[8];
		return nullptr;
	case OBJ_Child9:
		if (IsPlayer && Player->StoredBattleObjects[9])
			if (Player->StoredBattleObjects[9]->IsActive)
				return Player->StoredBattleObjects[9];
		return nullptr;
	case OBJ_Child10:
		if (IsPlayer && Player->StoredBattleObjects[10])
			if (Player->StoredBattleObjects[10]->IsActive)
				return Player->StoredBattleObjects[10];
		return nullptr;
	case OBJ_Child11:
		if (IsPlayer && Player->StoredBattleObjects[11])
			if (Player->StoredBattleObjects[11]->IsActive)
				return Player->StoredBattleObjects[11];
		return nullptr;
	case OBJ_Child12:
		if (IsPlayer && Player->StoredBattleObjects[12])
			if (Player->StoredBattleObjects[12]->IsActive)
				return Player->StoredBattleObjects[12];
		return nullptr;
	case OBJ_Child13:
		if (IsPlayer && Player->StoredBattleObjects[13])
			if (Player->StoredBattleObjects[13]->IsActive)
				return Player->StoredBattleObjects[13];
		return nullptr;
	case OBJ_Child14:
		if (IsPlayer && Player->StoredBattleObjects[14])
			if (Player->StoredBattleObjects[14]->IsActive)
				return Player->StoredBattleObjects[14];
		return nullptr;
	case OBJ_Child15:
		if (IsPlayer && Player->StoredBattleObjects[15])
			if (Player->StoredBattleObjects[15]->IsActive)
				return Player->StoredBattleObjects[15];
		return nullptr;
	default:
		return nullptr;
	}
}
