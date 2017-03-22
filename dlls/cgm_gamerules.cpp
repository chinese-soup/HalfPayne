#include	"extdll.h"
#include	"util.h"
#include	"cbase.h"
#include	"player.h"
#include	"client.h"
#include	"weapons.h"
#include	"cgm_gamerules.h"
#include	"custom_gamemode_config.h"
#include	<algorithm>
#include	"monsters.h"

// Custom Game Mode Rules

int	gmsgCustomEnd	= 0;

// CGameRules were recreated each level change and there were no built-in saving method,
// that means we'd lose config file state on each level change.
//
// This has been changed and now CGameRules state is preseved during CHANGE_LEVEL calls,
// using g_changeLevelOccured like below and see also CWorld::Precache 

extern int g_changeLevelOccured;

CCustomGameModeRules::CCustomGameModeRules( const char *configFolder ) : config( configFolder )
{
	if ( !gmsgCustomEnd ) {
		gmsgCustomEnd = REG_USER_MSG( "CustomEnd", -1 );
	}

	// Difficulty must be initialized separately and here, becuase entities are not yet spawned,
	// and they take some of the difficulty info at spawn (like CWallHealth)

	// Monster entities also have to be fetched at this moment for ClientPrecache.
	const char *configName = CVAR_GET_STRING( "gamemode_config" );
	config.ReadFile( configName );
	RefreshSkillData();

	ended = false;

	cheated = false;
	cheatedMessageSent = false;

	timeDelta = 0.0f;
	lastGlobalTime = 0.0f;
}

void CCustomGameModeRules::RestartGame() {
	char mapName[256];
	sprintf( mapName, "%s", config.startMap.c_str() );

	// Change level to [startmap] and queue the 'restart' command
	// for CCustomGameModeRules::PlayerSpawn.
	// Would be better just to execute 'cgm' command directly somehow.
	config.markedForRestart = true;
	CHANGE_LEVEL( mapName, NULL );
}

void CCustomGameModeRules::PlayerSpawn( CBasePlayer *pPlayer )
{
	if ( config.markedForRestart ) {
		config.markedForRestart = false;
		SERVER_COMMAND( "restart\n" );
		return;
	}

	pPlayer->activeGameMode = GAME_MODE_CUSTOM;
	pPlayer->activeGameModeConfig = ALLOC_STRING( config.configName.c_str() );

	// For first map
	SpawnEnemiesByConfig( STRING( gpGlobals->mapname ) );
	HookModelIndex( pPlayer->edict(), STRING( gpGlobals->mapname ), CHANGE_LEVEL_MODEL_INDEX );

	pPlayer->weaponRestricted = config.weaponRestricted;
	pPlayer->noSaving = config.noSaving;
	pPlayer->infiniteAmmo = config.infiniteAmmo;

	if ( config.instaGib ) {
		config.weaponRestricted = true;
		config.infiniteAmmo = true;

		pPlayer->weaponRestricted = true;
		pPlayer->infiniteAmmo = true;
		pPlayer->instaGib = true;

		pPlayer->SetEvilImpulse101( true );
		pPlayer->GiveNamedItem( "weapon_gauss", true );
		pPlayer->SetEvilImpulse101( false );
	}

	pPlayer->SetEvilImpulse101( true );
	for ( size_t i = 0; i < config.loadout.size( ); i++ ) {
		std::string loadoutItem = config.loadout.at( i );

		if ( loadoutItem == "all" ) {
			pPlayer->GiveAll( true );
			pPlayer->SetEvilImpulse101( true ); // it was set false by GiveAll
		}
		else {
			if ( loadoutItem == "item_healthkit" ) {
				pPlayer->TakePainkiller();
			} else if ( loadoutItem == "item_suit" ) {
				pPlayer->pev->weapons |= ( 1 << WEAPON_SUIT );
			} else if ( loadoutItem == "item_longjump" ) {
				pPlayer->m_fLongJump = TRUE;
				g_engfuncs.pfnSetPhysicsKeyValue( pPlayer->edict( ), "slj", "1" );
			} else {
				const char *item = allowedItems[CustomGameModeConfig::GetAllowedItemIndex( loadoutItem.c_str( ) )];
				pPlayer->GiveNamedItem( item, true );
			}
		}
	}
	pPlayer->SetEvilImpulse101( false );

	if ( !config.emptySlowmotion ) {
		pPlayer->TakeSlowmotionCharge( 100 );
	}

	if ( config.startPositionSpecified ) {
		pPlayer->pev->origin = config.startPosition;
	}
	if ( config.startYawSpecified ) {
		pPlayer->pev->angles[1] = config.startYaw;
	}

	if ( !config.noSlowmotion ) {

		if ( config.constantSlowmotion ) {
			pPlayer->TakeSlowmotionCharge( 100 );
			pPlayer->SetSlowMotion( true );
			pPlayer->infiniteSlowMotion = true;
			pPlayer->constantSlowmotion = true;
		}

		if ( config.infiniteSlowmotion ) {
			pPlayer->TakeSlowmotionCharge( 100 );
			pPlayer->infiniteSlowMotion = true;
		}

	} else {
		pPlayer->noSlowmotion = true;
	}

	if ( config.bulletPhysicsConstant ) {
		pPlayer->bulletPhysicsMode = BULLET_PHYSICS_CONSTANT;
	} else if ( config.bulletPhysicsEnemiesAndPlayerOnSlowmotion ) {
		pPlayer->bulletPhysicsMode = BULLET_PHYSICS_ENEMIES_AND_PLAYER_ON_SLOWMOTION;
	} else if ( config.bulletPhysicsDisabled ) {
		pPlayer->bulletPhysicsMode = BULLET_PHYSICS_DISABLED;
	}

	// Do not let player cheat by not starting at the [startmap]
	const char *startMap = config.startMap.c_str();
	const char *actualMap = STRING( gpGlobals->mapname );
	if ( strcmp( startMap, actualMap ) != 0 ) {
		cheated = true;
	}

}

void CCustomGameModeRules::OnChangeLevel() {
	SpawnEnemiesByConfig( STRING( gpGlobals->mapname ) );

	// it was previously a C style cast like everywhere else,
	// but this particular call could return CWorld instance
	// according to debugger - what the fuck?
	if ( CBasePlayer *pPlayer = dynamic_cast< CBasePlayer * >( CBasePlayer::Instance( g_engfuncs.pfnPEntityOfEntIndex( 1 ) ) ) ) {
		pPlayer->ClearSoundQueue();
	}
}

BOOL CCustomGameModeRules::CanHavePlayerItem( CBasePlayer *pPlayer, CBasePlayerItem *pWeapon )
{
	if ( !pPlayer->weaponRestricted ) {
		return CHalfLifeRules::CanHavePlayerItem( pPlayer, pWeapon );
	}

	if ( !pPlayer->HasWeapons() ) {
		return CHalfLifeRules::CanHavePlayerItem( pPlayer, pWeapon );
	}

	return FALSE;
}

void CCustomGameModeRules::PlayerThink( CBasePlayer *pPlayer )
{
	timeDelta = ( gpGlobals->time - lastGlobalTime );

	lastGlobalTime = gpGlobals->time;

	if ( pPlayer->pev->deadflag == DEAD_NO ) {
		
		// This is terribly wrong, it would be better to reset lastGlobalTime on actual change level event
		// It was made to prevent timer messup during level changes, because each level has it's own local time
		if ( fabs( timeDelta ) <= 0.1 ) {
			if ( pPlayer->slowMotionEnabled ) {
				pPlayer->secondsInSlowmotion += timeDelta;
			}
		}

		CheckForCheats( pPlayer );
	}
}

// TODO: isHeadshot and other flags are having DRY issue with overloaded function in CBlackMesaMinute
void CCustomGameModeRules::OnKilledEntityByPlayer( CBasePlayer *pPlayer, CBaseEntity *victim ) {
	bool isHeadshot = false;
	if ( CBaseMonster *monsterVictim = dynamic_cast< CBaseMonster * >( victim ) ) {
		if ( monsterVictim->m_LastHitGroup == HITGROUP_HEAD ) {
			isHeadshot = true;
		}
	}
	
	bool killedByExplosion = victim->killedByExplosion;
	bool killedByCrowbar = victim->killedByCrowbar;
	bool destroyedGrenade = strcmp( STRING( victim->pev->classname ), "grenade" ) == 0;

	pPlayer->kills++;

	if ( killedByExplosion ) {
		pPlayer->explosiveKills++;
	} else if ( killedByCrowbar ) {
		pPlayer->crowbarKills++;
	} else if ( isHeadshot ) {
		pPlayer->headshotKills++;
	} else if ( destroyedGrenade ) {
		pPlayer->projectileKills++;
	}
}

void CCustomGameModeRules::CheckForCheats( CBasePlayer *pPlayer )
{
	if ( cheated && cheatedMessageSent || ended ) {
		return;
	}

	if ( cheated ) {
		OnCheated( pPlayer );
		return;
	}

	if ( ( pPlayer->pev->flags & FL_GODMODE ) ||
		 ( pPlayer->pev->flags & FL_NOTARGET ) ||
		 ( pPlayer->pev->movetype & MOVETYPE_NOCLIP ) ||
		 pPlayer->usedCheat ) {
		cheated = true;
	}

}

void CCustomGameModeRules::OnCheated( CBasePlayer *pPlayer ) {
	cheatedMessageSent = true;
}

void CCustomGameModeRules::End( CBasePlayer *pPlayer ) {

	if ( ended ) {
		return;
	}

	if ( pPlayer->slowMotionEnabled ) {
		pPlayer->ToggleSlowMotion();
	}

	ended = true;

	pPlayer->pev->movetype = MOVETYPE_NONE;
	pPlayer->pev->flags |= FL_NOTARGET;
	pPlayer->RemoveAllItems( true );

	OnEnd( pPlayer );
}

void CCustomGameModeRules::OnEnd( CBasePlayer *pPlayer ) {
	MESSAGE_BEGIN( MSG_ONE, gmsgCustomEnd, NULL, pPlayer->pev );

	WRITE_STRING( config.name.c_str() );

		WRITE_FLOAT( pPlayer->secondsInSlowmotion );
		WRITE_SHORT( pPlayer->kills );
		WRITE_SHORT( pPlayer->headshotKills );
		WRITE_SHORT( pPlayer->explosiveKills );
		WRITE_SHORT( pPlayer->crowbarKills );
		WRITE_SHORT( pPlayer->projectileKills );

	MESSAGE_END();
}

void CCustomGameModeRules::HookModelIndex( edict_t *activator, const char *mapName, int modelIndex )
{
	CBasePlayer *pPlayer = ( CBasePlayer * ) CBasePlayer::Instance( g_engfuncs.pfnPEntityOfEntIndex( 1 ) );
	if ( !pPlayer ) {
		return;
	}

	ModelIndex indexToFind( mapName, modelIndex );
	
	// Does endTriggers contain such index?
	auto foundIndex = config.endTriggers.find( indexToFind );
	if ( foundIndex != config.endTriggers.end() ) {
		config.endTriggers.erase( foundIndex );

		End( pPlayer );
	}

	// Does 'sounds' contain such index?
	auto soundIndex = config.sounds.begin();
	while ( soundIndex != config.sounds.end() ) {

		if ( soundIndex->mapName == std::string( mapName ) &&
			 soundIndex->modelIndex == modelIndex &&
			 !pPlayer->ModelIndexHasBeenHooked( soundIndex->key.c_str() ) ) {

			// I'm very sorry for this memory leak for now
			string_t soundPathAllocated = ALLOC_STRING( soundIndex->soundPath.c_str() );

			pPlayer->AddToSoundQueue( soundPathAllocated, soundIndex->delay, soundIndex->isMaxCommentary );
			if ( !soundIndex->constant ) {
				pPlayer->RememberHookedModelIndex( soundPathAllocated );
				config.sounds.erase( soundIndex );
			}

		}

		soundIndex++;
	}
}

void CCustomGameModeRules::SpawnEnemiesByConfig( const char *mapName )
{
	if ( config.entitySpawns.size() == 0 ) {
		return;
	}

	std::vector<EntitySpawn>::iterator entitySpawn = config.entitySpawns.begin();
	for ( entitySpawn; entitySpawn != config.entitySpawns.end(); entitySpawn++ ) {
		if ( entitySpawn->mapName == mapName ) {
			CBaseEntity::Create( allowedEntities[CustomGameModeConfig::GetAllowedEntityIndex( entitySpawn->entityName.c_str() )], entitySpawn->origin, Vector( 0, entitySpawn->angle, 0 ) );
			config.entitySpawns.erase( entitySpawn );
			entitySpawn--;
		}
	}
}

void CCustomGameModeRules::Precache() {

	for ( std::string spawn : config.entitiesToPrecache ) {
		UTIL_PrecacheOther( spawn.c_str() );
	}
	
	// I'm very sorry for this memory leak for now
	for ( std::string sound : config.soundsToPrecache ) {
		PRECACHE_SOUND( ( char * ) STRING( ALLOC_STRING( sound.c_str() ) ) );
	}

}

// Hardcoded values so it won't depend on console variables
void CCustomGameModeRules::RefreshSkillData() 
{
	gSkillData.barneyHealth = 35;
	gSkillData.slaveDmgClawrake = 25.0f;

	gSkillData.leechHealth = 2.0f;
	gSkillData.leechDmgBite = 2.0f;

	gSkillData.scientistHealth = 20.0f;

	gSkillData.snarkHealth = 2.0f;
	gSkillData.snarkDmgBite = 10.0f;
	gSkillData.snarkDmgPop = 5.0f;

	gSkillData.plrDmgCrowbar = 10.0f;
	gSkillData.plrDmg9MM = 8.0f;
	gSkillData.plrDmg357 = 40.0f;
	gSkillData.plrDmgMP5 = 5.0f;
	gSkillData.plrDmgM203Grenade = 100.0f;
	gSkillData.plrDmgBuckshot = 5.0f;
	gSkillData.plrDmgCrossbowClient = 10.0f;
	gSkillData.plrDmgCrossbowMonster = 50.0f;
	gSkillData.plrDmgRPG = 100.0f;
	gSkillData.plrDmgGauss = 20.0f;
	gSkillData.plrDmgEgonNarrow = 6.0f;
	gSkillData.plrDmgEgonWide = 14.0f;
	gSkillData.plrDmgHornet = 7;
	gSkillData.plrDmgHandGrenade = 100.0f;
	gSkillData.plrDmgSatchel = 150.0f;
	gSkillData.plrDmgTripmine = 150.0f;

	gSkillData.healthkitCapacity = 15.0f; // doesn't matter - it's painkiller
	gSkillData.scientistHeal = 25.0f;

	if ( config.powerfulHeadshots ) {
		gSkillData.monHead = 10.0f;
	} else {
		gSkillData.monHead = 3.0f;
	}
	gSkillData.monChest = 1.0f;
	gSkillData.monStomach = 1.0f;
	gSkillData.monLeg = 1.0f;
	gSkillData.monArm = 1.0f;

	gSkillData.plrHead = 3.0f;
	gSkillData.plrChest = 1.0f;
	gSkillData.plrStomach = 1.0f;
	gSkillData.plrLeg = 1.0f;
	gSkillData.plrArm = 1.0f;

	if ( config.difficulty == CustomGameModeConfig::GAME_DIFFICULTY_EASY ) {
		
		gSkillData.iSkillLevel = 1;

		gSkillData.agruntHealth = 60.0f;
		gSkillData.agruntDmgPunch = 10.0f;

		gSkillData.apacheHealth = 150.0f;
	
		gSkillData.bigmommaHealthFactor = 1.0f;
		gSkillData.bigmommaDmgSlash = 50.0f;
		gSkillData.bigmommaDmgBlast = 100.0f;
		gSkillData.bigmommaRadiusBlast = 250.0f;

		gSkillData.bullsquidHealth = 40.0f;
		gSkillData.bullsquidDmgBite = 15.0f;
		gSkillData.bullsquidDmgWhip = 25.0f;
		gSkillData.bullsquidDmgSpit = 10.0f;

		gSkillData.gargantuaHealth = 800.0f;
		gSkillData.gargantuaDmgSlash = 10.0f;
		gSkillData.gargantuaDmgFire = 3.0f;
		gSkillData.gargantuaDmgStomp = 50.0f;

		gSkillData.hassassinHealth = 30.0f;

		gSkillData.headcrabHealth = 10.0f;
		gSkillData.headcrabDmgBite = 5.0f;

		gSkillData.hgruntHealth = 50.0f;
		gSkillData.hgruntDmgKick = 5.0f;
		gSkillData.hgruntShotgunPellets = 3.0f;
		gSkillData.hgruntGrenadeSpeed = 400.0f;

		gSkillData.houndeyeHealth = 20.0f;
		gSkillData.houndeyeDmgBlast = 10.0f;

		gSkillData.slaveHealth = 30.0f;
		gSkillData.slaveDmgClaw = 8.0f;
		gSkillData.slaveDmgZap = 10.0f;

		gSkillData.ichthyosaurHealth = 200.0f;
		gSkillData.ichthyosaurDmgShake = 20.0f;

		gSkillData.controllerHealth = 60.0f;
		gSkillData.controllerDmgZap = 15.0f;
		gSkillData.controllerSpeedBall = 650.0f;
		gSkillData.controllerDmgBall = 3.0f;

		gSkillData.nihilanthHealth = 800.0f;
		gSkillData.nihilanthZap = 30.0f;
	
		gSkillData.zombieHealth = 50.0f;
		gSkillData.zombieDmgOneSlash = 10.0f;
		gSkillData.zombieDmgBothSlash = 25.0f;

		gSkillData.turretHealth = 50.0f;
		gSkillData.miniturretHealth = 40.0f;
		gSkillData.sentryHealth = 40.0f;

		gSkillData.monDmg12MM = 8.0f;
		gSkillData.monDmgMP5 = 3.0f;
		gSkillData.monDmg9MM = 5.0f;
		
		gSkillData.monDmgHornet = 4.0f;

		gSkillData.suitchargerCapacity = 75.0f;
		gSkillData.batteryCapacity = 15.0f;
		gSkillData.healthchargerCapacity = 50.0f;
		
	} else if ( config.difficulty == CustomGameModeConfig::GAME_DIFFICULTY_MEDIUM ) {
		gSkillData.iSkillLevel = 2;

		gSkillData.agruntHealth = 90.0f;
		gSkillData.agruntDmgPunch = 20.0f;

		gSkillData.apacheHealth = 250.0f;
	
		gSkillData.bigmommaHealthFactor = 1.5f;
		gSkillData.bigmommaDmgSlash = 60.0f;
		gSkillData.bigmommaDmgBlast = 120.0f;
		gSkillData.bigmommaRadiusBlast = 250.0f;

		gSkillData.bullsquidHealth = 40.0f;
		gSkillData.bullsquidDmgBite = 25.0f;
		gSkillData.bullsquidDmgWhip = 35.0f;
		gSkillData.bullsquidDmgSpit = 10.0f;

		gSkillData.gargantuaHealth = 800.0f;
		gSkillData.gargantuaDmgSlash = 30.0f;
		gSkillData.gargantuaDmgFire = 5.0f;
		gSkillData.gargantuaDmgStomp = 100.0f;

		gSkillData.hassassinHealth = 50.0f;

		gSkillData.headcrabHealth = 10.0f;
		gSkillData.headcrabDmgBite = 10.0f;

		gSkillData.hgruntHealth = 50.0f;
		gSkillData.hgruntDmgKick = 10.0f;
		gSkillData.hgruntShotgunPellets = 5.0f;
		gSkillData.hgruntGrenadeSpeed = 600.0f;

		gSkillData.houndeyeHealth = 20.0f;
		gSkillData.houndeyeDmgBlast = 15.0f;

		gSkillData.slaveHealth = 30.0f;
		gSkillData.slaveDmgClaw = 10.0f;
		gSkillData.slaveDmgZap = 10.0f;

		gSkillData.ichthyosaurHealth = 200.0f;
		gSkillData.ichthyosaurDmgShake = 35.0f;

		gSkillData.controllerHealth = 60.0f;
		gSkillData.controllerDmgZap = 25.0f;
		gSkillData.controllerSpeedBall = 800.0f;
		gSkillData.controllerDmgBall = 4.0f;

		gSkillData.nihilanthHealth = 800.0f;
		gSkillData.nihilanthZap = 30.0f;
	
		gSkillData.zombieHealth = 50.0f;
		gSkillData.zombieDmgOneSlash = 20.0f;
		gSkillData.zombieDmgBothSlash = 40.0f;

		gSkillData.turretHealth = 50.0f;
		gSkillData.miniturretHealth = 40.0f;
		gSkillData.sentryHealth = 40.0f;

		gSkillData.monDmg12MM = 10.0f;
		gSkillData.monDmgMP5 = 4.0f;
		gSkillData.monDmg9MM = 5.0f;
		
		gSkillData.monDmgHornet = 5.0f;

		gSkillData.suitchargerCapacity = 50.0f;
		gSkillData.batteryCapacity = 15.0f;
		gSkillData.healthchargerCapacity = 40.0f;
	} else if ( config.difficulty == CustomGameModeConfig::GAME_DIFFICULTY_HARD ) {
		gSkillData.iSkillLevel = 3;

		gSkillData.agruntHealth = 120.0f;
		gSkillData.agruntDmgPunch = 20.0f;

		gSkillData.apacheHealth = 400.0f;
	
		gSkillData.bigmommaHealthFactor = 2.0f;
		gSkillData.bigmommaDmgSlash = 70.0f;
		gSkillData.bigmommaDmgBlast = 160.0f;
		gSkillData.bigmommaRadiusBlast = 275.0f;

		gSkillData.bullsquidHealth = 40.0f;
		gSkillData.bullsquidDmgBite = 25.0f;
		gSkillData.bullsquidDmgWhip = 35.0f;
		gSkillData.bullsquidDmgSpit = 10.0f;

		gSkillData.gargantuaHealth = 1000.0f;
		gSkillData.gargantuaDmgSlash = 30.0f;
		gSkillData.gargantuaDmgFire = 5.0f;
		gSkillData.gargantuaDmgStomp = 100.0f;

		gSkillData.hassassinHealth = 50.0f;

		gSkillData.headcrabHealth = 20.0f;
		gSkillData.headcrabDmgBite = 10.0f;

		gSkillData.hgruntHealth = 80.0f;
		gSkillData.hgruntDmgKick = 10.0f;
		gSkillData.hgruntShotgunPellets = 6.0f;
		gSkillData.hgruntGrenadeSpeed = 800.0f;

		gSkillData.houndeyeHealth = 30.0f;
		gSkillData.houndeyeDmgBlast = 15.0f;

		gSkillData.slaveHealth = 60.0f;
		gSkillData.slaveDmgClaw = 10.0f;
		gSkillData.slaveDmgZap = 15.0f;

		gSkillData.ichthyosaurHealth = 400.0f;
		gSkillData.ichthyosaurDmgShake = 50.0f;

		gSkillData.controllerHealth = 100.0f;
		gSkillData.controllerDmgZap = 35.0f;
		gSkillData.controllerSpeedBall = 1000.0f;
		gSkillData.controllerDmgBall = 5.0f;

		gSkillData.nihilanthHealth = 1000.0f;
		gSkillData.nihilanthZap = 50.0f;
	
		gSkillData.zombieHealth = 100.0f;
		gSkillData.zombieDmgOneSlash = 20.0f;
		gSkillData.zombieDmgBothSlash = 40.0f;

		gSkillData.turretHealth = 60.0f;
		gSkillData.miniturretHealth = 50.0f;
		gSkillData.sentryHealth = 50.0f;

		gSkillData.monDmg12MM = 10.0f;
		gSkillData.monDmgMP5 = 5.0f;
		gSkillData.monDmg9MM = 8.0f;
		
		gSkillData.monDmgHornet = 8.0f;

		gSkillData.suitchargerCapacity = 35.0f;
		gSkillData.batteryCapacity = 10.0f;
		gSkillData.healthchargerCapacity = 25.0f;
	}
}
