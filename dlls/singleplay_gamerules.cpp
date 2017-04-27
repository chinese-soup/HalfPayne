/***
*
*	Copyright (c) 1996-2001, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
// teamplay_gamerules.cpp
//
#include	"extdll.h"
#include	"util.h"
#include	"cbase.h"
#include	"player.h"
#include	"weapons.h"
#include	"gamerules.h"
#include	"skill.h"
#include	"items.h"
#include	"cgm_gamerules.h"

extern DLL_GLOBAL CGameRules	*g_pGameRules;
extern DLL_GLOBAL BOOL	g_fGameOver;
extern int gmsgDeathMsg;	// client dll messages
extern int gmsgScoreInfo;
extern int gmsgMOTD;

int	gmsgEndCredits	= 0;

//=========================================================
//=========================================================
CHalfLifeRules::CHalfLifeRules( void ) : mapConfig( CustomGameModeConfig::GAME_MODE_CONFIG_MAP )
{
	if ( !gmsgEndCredits ) {
		gmsgEndCredits = REG_USER_MSG( "EndCredits", 0 );
	}

	ended = false;
	entitiesUsed = false;

	if ( !mapConfig.ReadFile( STRING( gpGlobals->mapname ) ) ) {
		g_engfuncs.pfnServerPrint( mapConfig.error.c_str() );
	}

	RefreshSkillData();
}

bool CHalfLifeRules::EntityShouldBePrevented( edict_t *entity )
{
	if ( entity ) {
		int modelIndex = entity->v.modelindex;
		std::string targetName = STRING( entity->v.targetname );

		auto foundIndex = mapConfig.entitiesToPrevent.begin();
		while ( foundIndex != mapConfig.entitiesToPrevent.end() ) {
			if ( foundIndex->mapName == std::string( STRING( gpGlobals->mapname ) ) &&
				( foundIndex->modelIndex == modelIndex || ( foundIndex->targetName == targetName && foundIndex->targetName.size() > 0 ) ) ) {
				return true;
			}
			
			foundIndex++;
			continue;
		}
	}

	return false;
}

void CHalfLifeRules::End( CBasePlayer *pPlayer )
{
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

void CHalfLifeRules::OnEnd( CBasePlayer *pPlayer )
{
	MESSAGE_BEGIN( MSG_ONE, gmsgEndCredits, NULL, pPlayer->pev );
	MESSAGE_END();
}

void CHalfLifeRules::OnChangeLevel()
{
	// it was previously a C style cast like everywhere else,
	// but this particular call could return CWorld instance
	// according to debugger - what the fuck?
	if ( CBasePlayer *pPlayer = dynamic_cast< CBasePlayer * >( CBasePlayer::Instance( g_engfuncs.pfnPEntityOfEntIndex( 1 ) ) ) ) {
		pPlayer->ClearSoundQueue();
	}

	if ( !mapConfig.ReadFile( STRING( gpGlobals->mapname ) ) ) {
		g_engfuncs.pfnServerPrint( mapConfig.error.c_str() );
	}

	entitiesUsed = false;
}

void CHalfLifeRules::HookModelIndex( edict_t *activator )
{
	HookModelIndex( activator, STRING( activator->v.targetname ) );
}

void CHalfLifeRules::HookModelIndex( edict_t *activator, const char *targetName )
{
	if ( !activator ) {
		return;
	}

	CBasePlayer *pPlayer = ( CBasePlayer * ) CBasePlayer::Instance( g_engfuncs.pfnPEntityOfEntIndex( 1 ) );
	if ( !pPlayer ) {
		return;
	}

	int modelIndex = activator->v.modelindex;
	const char *className = STRING( activator->v.classname );

	if ( CVAR_GET_FLOAT( "print_model_indexes" ) > 0.0f ) {
		char message[128];
		sprintf( message, "[%s] Hooked model index: %d; target name: %s; class name: %s\n", STRING( gpGlobals->mapname ), modelIndex, targetName, className );
		g_engfuncs.pfnServerPrint( message );
	}

	OnHookedModelIndex( pPlayer, activator, modelIndex, std::string( targetName ) );
}

void CHalfLifeRules::OnHookedModelIndex( CBasePlayer *pPlayer, edict_t *activator, int modelIndex, const std::string &targetName )
{
	// Does 'sounds' contain such index?
	auto soundIndex = mapConfig.sounds.begin();
	while ( soundIndex != mapConfig.sounds.end() ) {

		if ( soundIndex->mapName == std::string( STRING( gpGlobals->mapname ) ) &&
			( soundIndex->modelIndex == modelIndex || ( soundIndex->targetName == targetName && soundIndex->targetName.size() > 0 ) ) &&
			!pPlayer->ModelIndexHasBeenHooked( soundIndex->key.c_str() ) ) {

			// I'm very sorry for this memory leak for now
			string_t soundPathAllocated = ALLOC_STRING( soundIndex->soundPath.c_str() );

			pPlayer->AddToSoundQueue( soundPathAllocated, soundIndex->delay, soundIndex->isMaxCommentary, true );
			if ( !soundIndex->constant ) {
				pPlayer->RememberHookedModelIndex( ALLOC_STRING( soundIndex->key.c_str() ) ); // memory leak
				mapConfig.sounds.erase( soundIndex );
			}

		}

		soundIndex++;
	}
}
void CHalfLifeRules::Precache()
{
	// I'm very sorry for this memory leak for now
	for ( std::string sound : mapConfig.soundsToPrecache ) {
		PRECACHE_SOUND( ( char * ) STRING( ALLOC_STRING( sound.c_str() ) ) );
	}
}

//=========================================================
//=========================================================
void CHalfLifeRules::Think ( void )
{
}

//=========================================================
//=========================================================
BOOL CHalfLifeRules::IsMultiplayer( void )
{
	return FALSE;
}

//=========================================================
//=========================================================
BOOL CHalfLifeRules::IsDeathmatch ( void )
{
	return FALSE;
}

//=========================================================
//=========================================================
BOOL CHalfLifeRules::IsCoOp( void )
{
	return FALSE;
}


//=========================================================
//=========================================================
BOOL CHalfLifeRules::FShouldSwitchWeapon( CBasePlayer *pPlayer, CBasePlayerItem *pWeapon )
{
	if ( !pPlayer->m_pActiveItem )
	{
		// player doesn't have an active item!
		return TRUE;
	}

	if ( CVAR_GET_FLOAT( "hud_autoswitch" ) == 0.0f ) {
		return false;
	}

	if ( !pPlayer->m_pActiveItem->CanHolster() )
	{
		return FALSE;
	}

	return TRUE;
}

//=========================================================
//=========================================================
BOOL CHalfLifeRules :: GetNextBestWeapon( CBasePlayer *pPlayer, CBasePlayerItem *pCurrentWeapon )
{
	return FALSE;
}

//=========================================================
//=========================================================
BOOL CHalfLifeRules :: ClientConnected( edict_t *pEntity, const char *pszName, const char *pszAddress, char szRejectReason[ 128 ] )
{

	if ( CBasePlayer *player = dynamic_cast< CBasePlayer * >( CBasePlayer::Instance( pEntity ) ) ) {
		if ( player->noSaving ) {
			g_engfuncs.pfnServerPrint( "You're not allowed to load this savefile.\n" );
			return FALSE;
		}
	}
	
	return TRUE;
}

void CHalfLifeRules :: InitHUD( CBasePlayer *pl )
{
}

//=========================================================
//=========================================================
void CHalfLifeRules :: ClientDisconnected( edict_t *pClient )
{
}

//=========================================================
//=========================================================
float CHalfLifeRules::FlPlayerFallDamage( CBasePlayer *pPlayer )
{
	// subtract off the speed at which a player is allowed to fall without being hurt,
	// so damage will be based on speed beyond that, not the entire fall
	pPlayer->m_flFallVelocity -= PLAYER_MAX_SAFE_FALL_SPEED;
	return pPlayer->m_flFallVelocity * DAMAGE_FOR_FALL_SPEED;
}

//=========================================================
//=========================================================
void CHalfLifeRules :: PlayerSpawn( CBasePlayer *pPlayer )
{
	OnHookedModelIndex( pPlayer, NULL, CHANGE_LEVEL_MODEL_INDEX, "" );
}

//=========================================================
//=========================================================
BOOL CHalfLifeRules :: AllowAutoTargetCrosshair( void )
{
	return ( g_iSkillLevel == SKILL_EASY );
}

//=========================================================
//=========================================================
void CHalfLifeRules :: PlayerThink( CBasePlayer *pPlayer )
{
	if ( !entitiesUsed ) {
		auto entityToUse = mapConfig.entityUses.begin();
		while ( entityToUse != mapConfig.entityUses.end() ) {
			if ( entityToUse->mapName == std::string( STRING( gpGlobals->mapname ) ) ) {
				for ( int i = 0 ; i < 1024 ; i++ ) {
					edict_t *edict = g_engfuncs.pfnPEntityOfEntIndex( i );
					if ( !edict ) {
						continue;
					}

					if ( entityToUse->modelIndex == edict->v.modelindex ) {
						if ( CBaseEntity *entity = CBaseEntity::Instance( edict ) ) {
							entity->Use( pPlayer, pPlayer, USE_SET, 1 );
						}
					}
				}
			}

			entityToUse++;
		}

		entitiesUsed = true;
	}
}


//=========================================================
//=========================================================
BOOL CHalfLifeRules :: FPlayerCanRespawn( CBasePlayer *pPlayer )
{
	return TRUE;
}

//=========================================================
//=========================================================
float CHalfLifeRules :: FlPlayerSpawnTime( CBasePlayer *pPlayer )
{
	return gpGlobals->time;//now!
}

//=========================================================
// IPointsForKill - how many points awarded to anyone
// that kills this player?
//=========================================================
int CHalfLifeRules :: IPointsForKill( CBasePlayer *pAttacker, CBasePlayer *pKilled )
{
	return 1;
}

//=========================================================
// PlayerKilled - someone/something killed this player
//=========================================================
void CHalfLifeRules :: PlayerKilled( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
}

//=========================================================
// Deathnotice
//=========================================================
void CHalfLifeRules::DeathNotice( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor )
{
}

//=========================================================
// PlayerGotWeapon - player has grabbed a weapon that was
// sitting in the world
//=========================================================
void CHalfLifeRules :: PlayerGotWeapon( CBasePlayer *pPlayer, CBasePlayerItem *pWeapon )
{
	HookModelIndex( pWeapon->edict(), STRING( pWeapon->pev->classname ) );
}

//=========================================================
// FlWeaponRespawnTime - what is the time in the future
// at which this weapon may spawn?
//=========================================================
float CHalfLifeRules :: FlWeaponRespawnTime( CBasePlayerItem *pWeapon )
{
	return -1;
}

//=========================================================
// FlWeaponRespawnTime - Returns 0 if the weapon can respawn 
// now,  otherwise it returns the time at which it can try
// to spawn again.
//=========================================================
float CHalfLifeRules :: FlWeaponTryRespawn( CBasePlayerItem *pWeapon )
{
	return 0;
}

//=========================================================
// VecWeaponRespawnSpot - where should this weapon spawn?
// Some game variations may choose to randomize spawn locations
//=========================================================
Vector CHalfLifeRules :: VecWeaponRespawnSpot( CBasePlayerItem *pWeapon )
{
	return pWeapon->pev->origin;
}

//=========================================================
// WeaponShouldRespawn - any conditions inhibiting the
// respawning of this weapon?
//=========================================================
int CHalfLifeRules :: WeaponShouldRespawn( CBasePlayerItem *pWeapon )
{
	return GR_WEAPON_RESPAWN_NO;
}

//=========================================================
//=========================================================
BOOL CHalfLifeRules::CanHaveItem( CBasePlayer *pPlayer, CItem *pItem )
{
	return TRUE;
}

//=========================================================
//=========================================================
void CHalfLifeRules::PlayerGotItem( CBasePlayer *pPlayer, CItem *pItem )
{
	HookModelIndex( pItem->edict(), STRING( pItem->pev->classname ) );
}

//=========================================================
//=========================================================
int CHalfLifeRules::ItemShouldRespawn( CItem *pItem )
{
	return GR_ITEM_RESPAWN_NO;
}


//=========================================================
// At what time in the future may this Item respawn?
//=========================================================
float CHalfLifeRules::FlItemRespawnTime( CItem *pItem )
{
	return -1;
}

//=========================================================
// Where should this item respawn?
// Some game variations may choose to randomize spawn locations
//=========================================================
Vector CHalfLifeRules::VecItemRespawnSpot( CItem *pItem )
{
	return pItem->pev->origin;
}

//=========================================================
//=========================================================
BOOL CHalfLifeRules::IsAllowedToSpawn( CBaseEntity *pEntity )
{
	return TRUE;
}

//=========================================================
//=========================================================
void CHalfLifeRules::PlayerGotAmmo( CBasePlayer *pPlayer, CBasePlayerAmmo *pAmmo )
{
	HookModelIndex( pAmmo->edict(), STRING( pAmmo->pev->classname ) );
}

//=========================================================
//=========================================================
int CHalfLifeRules::AmmoShouldRespawn( CBasePlayerAmmo *pAmmo )
{
	return GR_AMMO_RESPAWN_NO;
}

//=========================================================
//=========================================================
float CHalfLifeRules::FlAmmoRespawnTime( CBasePlayerAmmo *pAmmo )
{
	return -1;
}

//=========================================================
//=========================================================
Vector CHalfLifeRules::VecAmmoRespawnSpot( CBasePlayerAmmo *pAmmo )
{
	return pAmmo->pev->origin;
}

//=========================================================
//=========================================================
float CHalfLifeRules::FlHealthChargerRechargeTime( void )
{
	return 0;// don't recharge
}

//=========================================================
//=========================================================
int CHalfLifeRules::DeadPlayerWeapons( CBasePlayer *pPlayer )
{
	return GR_PLR_DROP_GUN_NO;
}

//=========================================================
//=========================================================
int CHalfLifeRules::DeadPlayerAmmo( CBasePlayer *pPlayer )
{
	return GR_PLR_DROP_AMMO_NO;
}

//=========================================================
//=========================================================
int CHalfLifeRules::PlayerRelationship( CBaseEntity *pPlayer, CBaseEntity *pTarget )
{
	// why would a single player in half life need this? 
	return GR_NOTTEAMMATE;
}

//=========================================================
//=========================================================
BOOL CHalfLifeRules :: FAllowMonsters( void )
{
	return TRUE;
}