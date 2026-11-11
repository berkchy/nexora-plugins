#include <nexora/nexora_plugin.h>
#include <wrect.h>
#include <cl_dll.h>
#include <cvardef.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <map>
#include <string>
#include <algorithm>

namespace
{

bool KfEnabled();

const int MAX_DEATHNOTICES = 4;
const int DEATHNOTICE_CONTAINER = MAX_DEATHNOTICES + 1;

const nexora_plugin_info_t s_info =
{
	"killfeed",
	"1.0.0",
	NEXORA_PLUGIN_API_VERSION,
	40,
};

const nexora_plugin_api_t *g_api = nullptr;
cl_enginefunc_t *g_eng = nullptr;

cvar_t *hud_killfeed_fade = nullptr;
cvar_t *hud_killfeed_fadein = nullptr;
cvar_t *hud_killfeed_fadeout = nullptr;
cvar_t *hud_killfeed_fxtime = nullptr;
cvar_t *hud_killfeed_holdtime = nullptr;
cvar_t *hud_killfeed_color = nullptr;
cvar_t *hud_killfeed_alpha = nullptr;
cvar_t *hud_killfeed_bgcolor = nullptr;
cvar_t *hud_killfeed_bgalpha = nullptr;
cvar_t *hud_killfeed_rowspacing = nullptr;
cvar_t *hud_killfeed_enable = nullptr;

struct KFNotice
{
	char szKiller[64];
	char szVictim[64];
	char szWeapon[40];
	int headshot;
	bool suicide;
	bool teamkill;
	bool nonplayerKill;
	int killerTeam;
	int victimTeam;
	float flSpawnTime;
	float flDisplayTime;
};

KFNotice g_notices[DEATHNOTICE_CONTAINER];

struct KFSprite
{
	HSPRITE hspr;
	wrect_t rc;
	bool valid;
};

std::map< std::string, KFSprite > g_spriteCache;

const int KF_TEAM_TERRORIST = 1;
const int KF_TEAM_CT = 2;
const int KF_TEAM_SPECTATOR = 3;

void KfParseColor( const char *value, int &r, int &g, int &b )
{
	r = g = b = 255;

	if ( value && value[0] )
	{
		if ( std::sscanf( value, "%d %d %d", &r, &g, &b ) != 3 )
			r = g = b = 255;
	}

	r = std::max( 0, std::min( 255, r ) );
	g = std::max( 0, std::min( 255, g ) );
	b = std::max( 0, std::min( 255, b ) );
}

void KfTeamColor( int team, int &r, int &g, int &b )
{
	switch ( team )
	{
	case KF_TEAM_CT:
		r = 79; g = 156; b = 240;
		break;
	case KF_TEAM_TERRORIST:
		r = 224; g = 147; b = 74;
		break;
	default:
		r = 150; g = 156; b = 164;
		break;
	}
}

int KfTextWidth( const char *text )
{
	if ( !text || !text[0] )
		return 0;

	int width = 0;
	int height = 0;
	g_eng->pfnDrawConsoleStringLen( text, &width, &height );
	return width;
}

void KfColor( int r, int g, int b, float alpha )
{
	g_eng->pfnDrawSetTextColor( ( r / 255.0f ) * alpha, ( g / 255.0f ) * alpha, ( b / 255.0f ) * alpha );
}

const KFSprite *KFFindSprite( const std::string &name, const char *fallbackName )
{
	const auto it = g_spriteCache.find( name );

	if ( it != g_spriteCache.end() )
		return &it->second;

	int count = 0;
	client_sprite_t *list = g_eng->pfnSPR_GetList( "sprites/hud.txt", &count );

	if ( !list )
		return nullptr;

	const KFSprite *found = nullptr;
	KFSprite sprite;
	sprite.hspr = NULL;
	sprite.rc = { 0, 0, 0, 0 };
	sprite.valid = false;

	for ( int i = 0; i < count; i++ )
	{
		if ( std::strcmp( list[i].szName, name.c_str() ) != 0 )
			continue;

		sprite.rc = list[i].rc;

		char path[256];
		std::snprintf( path, sizeof( path ), "sprites/%s.spr", list[i].szSprite );

		sprite.hspr = g_eng->pfnSPR_Load( path );
		sprite.valid = sprite.hspr != NULL;
		g_spriteCache[ name ] = sprite;
		found = &g_spriteCache[ name ];
		break;
	}

	if ( !found && fallbackName && fallbackName != name )
		return KFFindSprite( fallbackName, nullptr );

	return found;
}

const KFSprite *KFWeaponSprite( const char *weapon )
{
	if ( !weapon || !weapon[0] )
		return KFFindSprite( "d_skull", nullptr );

	return KFFindSprite( weapon, "d_skull" );
}

void KFInsertNotice( const nexora_deathmsg_args_t *death )
{
	int i = 0;

	for ( ; i < MAX_DEATHNOTICES; i++ )
	{
		if ( g_notices[i].szWeapon[0] == '\0' )
			break;
	}

	if ( i == MAX_DEATHNOTICES )
	{
		std::memmove( g_notices, g_notices + 1, sizeof( KFNotice ) * ( MAX_DEATHNOTICES - 1 ) );

		g_notices[MAX_DEATHNOTICES - 1] = {};
		i = MAX_DEATHNOTICES - 1;
	}

	std::memset( &g_notices[i], 0, sizeof( KFNotice ) );

	const char *killerName = death->killer_name ? death->killer_name : "";
	const char *victimName = death->victim_name ? death->victim_name : "";
	const char *weapon = death->weapon ? death->weapon : "";

	if ( std::strlen( killerName ) >= sizeof( g_notices[i].szKiller ) )
		killerName = "";

	if ( std::strlen( victimName ) >= sizeof( g_notices[i].szVictim ) )
		victimName = "";

	std::strcpy( g_notices[i].szKiller, killerName );
	std::strcpy( g_notices[i].szVictim, victimName );

	if ( std::strlen( weapon ) >= sizeof( g_notices[i].szWeapon ) )
		weapon = "";

	std::strcpy( g_notices[i].szWeapon, weapon );

	if ( death->victim == 255 )
	{
		const char *object = weapon + 2;
		std::strncpy( g_notices[i].szVictim, object, sizeof( g_notices[i].szVictim ) - 1 );
		g_notices[i].szVictim[sizeof( g_notices[i].szVictim ) - 1] = '\0';
	}

	g_notices[i].headshot = death->headshot;
	g_notices[i].suicide = death->is_suicide != 0;
	g_notices[i].teamkill = death->is_teamkill != 0;
	g_notices[i].nonplayerKill = death->is_nonplayer_kill != 0;
	g_notices[i].killerTeam = death->killer_team;
	g_notices[i].victimTeam = death->victim_team;
	g_notices[i].flSpawnTime = death->time;

	float holdTime = hud_killfeed_holdtime ? hud_killfeed_holdtime->value : 5.0f;
	holdTime = std::max( 0.5f, std::min( 60.0f, holdTime ) );

	g_notices[i].flDisplayTime = death->time + holdTime;
}

void KFRemoveExpired( float flTime )
{
	for ( int i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( g_notices[i].szWeapon[0] == '\0' )
			continue;

		if ( g_notices[i].flDisplayTime < flTime )
		{
			std::memmove( &g_notices[i], &g_notices[i + 1], sizeof( KFNotice ) * ( MAX_DEATHNOTICES - i ) );
			std::memset( &g_notices[MAX_DEATHNOTICES], 0, sizeof( KFNotice ) );
			i--;
		}
	}
}

void KFDrawNotice( const KFNotice &item, int index, float flTime, int scale, int rowHeight, int rowSpacing )
{
	int r = 220, g = 224, b = 228;

	const int pad = 10 * scale;
	const int top = 48 * scale;
	const int y = top + index * ( rowHeight + rowSpacing );
	const int hsSize = 10 * scale;
	const int gap = 4 * scale;

	float fade = 1.0f;

	if ( hud_killfeed_fade && hud_killfeed_fade->value >= 0.5f )
	{
		float fxTime = hud_killfeed_fxtime ? hud_killfeed_fxtime->value : 0.25f;
		fxTime = std::max( 0.05f, std::min( 2.0f, fxTime ) );

		const float elapsed = flTime - item.flSpawnTime;
		const float remaining = item.flDisplayTime - flTime;

		if ( hud_killfeed_fadein && hud_killfeed_fadein->value >= 0.5f && elapsed < fxTime )
			fade = std::min( fade, elapsed / fxTime );

		if ( hud_killfeed_fadeout && hud_killfeed_fadeout->value >= 0.5f && remaining < fxTime )
			fade = std::min( fade, remaining / fxTime );
	}

	fade = std::max( 0.0f, std::min( 1.0f, fade ) );

	int textBaseAlpha = hud_killfeed_alpha ? static_cast<int>( hud_killfeed_alpha->value ) : 235;
	textBaseAlpha = std::max( 0, std::min( 255, textBaseAlpha ) );

	int bgBaseAlpha = hud_killfeed_bgalpha ? static_cast<int>( hud_killfeed_bgalpha->value ) : 110;
	bgBaseAlpha = std::max( 0, std::min( 255, bgBaseAlpha ) );

	const float textAlpha = ( textBaseAlpha / 255.0f ) * fade;
	const int bgAlpha = static_cast<int>( bgBaseAlpha * fade );

	int screenWidth = 640;
	int screenHeight = 480;

	if ( g_api->get_screen_size )
		g_api->get_screen_size( &screenWidth, &screenHeight );

	const int right = screenWidth - pad;

	const KFSprite *weapon = KFWeaponSprite( item.szWeapon );
	const bool showWeapon = weapon && weapon->valid;

	const int weaponWidth = showWeapon ? ( weapon->rc.right - weapon->rc.left ) : ( 14 * scale );
	const int weaponHeight = showWeapon ? ( weapon->rc.bottom - weapon->rc.top ) : ( 14 * scale );

	const int killerWidth = KfTextWidth( item.szKiller );
	const int victimWidth = KfTextWidth( item.szVictim );
	const bool showHeadshot = item.headshot != 0;

	int x = right;

	if ( victimWidth > 0 )
		x -= victimWidth;

	if ( showHeadshot )
		x -= gap + hsSize;

	x -= gap + weaponWidth;

	if ( killerWidth > 0 )
		x -= gap + killerWidth;

	const int left = x - pad;
	const int width = right - left;

	int bgR, bgG, bgB;
	KfParseColor( hud_killfeed_bgcolor ? hud_killfeed_bgcolor->string : nullptr, bgR, bgG, bgB );

	if ( bgAlpha > 0 )
		g_eng->pfnFillRGBABlend( left, y - 2 * scale, width, rowHeight + 2 * scale, bgR, bgG, bgB, bgAlpha );

	const bool useConfigColor = hud_killfeed_color && hud_killfeed_color->string[0];

	int cfgR, cfgG, cfgB;
	if ( useConfigColor )
		KfParseColor( hud_killfeed_color->string, cfgR, cfgG, cfgB );

	x = right;

	if ( victimWidth > 0 )
	{
		x -= victimWidth;

		if ( useConfigColor )
			KfColor( cfgR, cfgG, cfgB, textAlpha );
		else
		{
			int tr, tg, tb;
			KfTeamColor( item.victimTeam, tr, tg, tb );
			KfColor( tr, tg, tb, textAlpha );
		}

		g_eng->pfnDrawConsoleString( x, y, const_cast<char *>( item.szVictim ) );
	}

	if ( showHeadshot )
	{
		x -= gap + hsSize;

		const KFSprite *hs = KFFindSprite( "d_headshot", "d_skull" );

		if ( hs && hs->valid )
		{
			g_eng->pfnSPR_Set( hs->hspr, 238, 239, 240 );
			g_eng->pfnSPR_DrawAdditive( 0, x, y - 1 * scale, &hs->rc );
		}
	}

	if ( showWeapon )
	{
		x -= gap + weaponWidth;

		const int weaponTint = static_cast<int>( 255 * textAlpha );
		const int weaponY = y + ( rowHeight - weaponHeight ) / 2;

		g_eng->pfnSPR_Set( weapon->hspr, weaponTint, weaponTint, weaponTint );
		g_eng->pfnSPR_DrawAdditive( 0, x, weaponY, &weapon->rc );
	}

	if ( killerWidth > 0 )
	{
		x -= gap + killerWidth;

		if ( useConfigColor )
			KfColor( cfgR, cfgG, cfgB, textAlpha );
		else
		{
			int kr, kg, kb;
			KfTeamColor( item.killerTeam, kr, kg, kb );
			KfColor( kr, kg, kb, textAlpha );
		}

		g_eng->pfnDrawConsoleString( x, y, const_cast<char *>( item.szKiller ) );
	}
}

void OnDeathMsg( void *args, void *user_data )
{
	const auto *death = static_cast<const nexora_deathmsg_args_t *>( args );

	if ( !death )
		return;

	KFInsertNotice( death );
}

void OnPaintHud( void *args, void *user_data )
{
	if ( !KfEnabled() )
		return;

	const auto *redraw = static_cast<const nexora_redraw_args_t *>( args );

	if ( !redraw )
		return;

	const float flTime = redraw->time;

	KFRemoveExpired( flTime );

	int screenWidth = 640;
	int screenHeight = 480;

	if ( g_api->get_screen_size )
		g_api->get_screen_size( &screenWidth, &screenHeight );

	int scale = static_cast<int>( screenHeight / 480.0f );

	if ( scale < 1 )
		scale = 1;

	if ( scale > 2 )
		scale = 2;

	const int rowHeight = 18 * scale;
	int rowSpacing = hud_killfeed_rowspacing ? static_cast<int>( hud_killfeed_rowspacing->value * scale ) : 4 * scale;
	rowSpacing = std::max( 0, std::min( 32 * scale, rowSpacing ) );

	for ( int i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( g_notices[i].szWeapon[0] == '\0' && g_notices[i].szKiller[0] == '\0' && g_notices[i].szVictim[0] == '\0' )
			continue;

		KFDrawNotice( g_notices[i], i, flTime, scale, rowHeight, rowSpacing );
	}
}

void OnHudElementDraw( void *args, void *user_data )
{
	auto *element = static_cast<nexora_hud_element_args_t *>( args );

	if ( !element )
		return;

	if ( KfEnabled() && element->name && std::strcmp( element->name, "deathnotice" ) == 0 )
		element->allow = 0;
}

bool KfEnabled()
{
	return hud_killfeed_enable && hud_killfeed_enable->value >= 0.5f;
}

bool RegisterCVars()
{
	hud_killfeed_enable = g_eng->pfnRegisterVariable( "hud_killfeed_enable", "1", FCVAR_ARCHIVE );

	hud_killfeed_fade = g_eng->pfnRegisterVariable( "hud_killfeed_fade", "1", FCVAR_ARCHIVE );
	hud_killfeed_fadein = g_eng->pfnRegisterVariable( "hud_killfeed_fadein", "1", FCVAR_ARCHIVE );
	hud_killfeed_fadeout = g_eng->pfnRegisterVariable( "hud_killfeed_fadeout", "1", FCVAR_ARCHIVE );
	hud_killfeed_fxtime = g_eng->pfnRegisterVariable( "hud_killfeed_fxtime", "0.25", FCVAR_ARCHIVE );
	hud_killfeed_holdtime = g_eng->pfnRegisterVariable( "hud_killfeed_holdtime", "5", FCVAR_ARCHIVE );
	hud_killfeed_color = g_eng->pfnRegisterVariable( "hud_killfeed_color", "", FCVAR_ARCHIVE );
	hud_killfeed_alpha = g_eng->pfnRegisterVariable( "hud_killfeed_alpha", "235", FCVAR_ARCHIVE );
	hud_killfeed_bgcolor = g_eng->pfnRegisterVariable( "hud_killfeed_bgcolor", "0 0 0", FCVAR_ARCHIVE );
	hud_killfeed_bgalpha = g_eng->pfnRegisterVariable( "hud_killfeed_bgalpha", "110", FCVAR_ARCHIVE );
	hud_killfeed_rowspacing = g_eng->pfnRegisterVariable( "hud_killfeed_rowspacing", "4", FCVAR_ARCHIVE );

	return hud_killfeed_enable && hud_killfeed_fade && hud_killfeed_fadein && hud_killfeed_fadeout &&
		hud_killfeed_fxtime && hud_killfeed_holdtime && hud_killfeed_color &&
		hud_killfeed_alpha && hud_killfeed_bgcolor && hud_killfeed_bgalpha &&
		hud_killfeed_rowspacing;
}

}

const nexora_plugin_info_t *nexora_plugin_query( void )
{
	return &s_info;
}

int nexora_plugin_init( const nexora_plugin_api_t *api )
{
	if ( !api || api->api_version != NEXORA_PLUGIN_API_VERSION )
		return 0;

	g_api = api;
	g_eng = static_cast<cl_enginefunc_t *>( api->engine_funcs );

	if ( !g_eng )
		return 0;

	std::memset( g_notices, 0, sizeof( g_notices ) );

	if ( !RegisterCVars() )
	{
		api->con_printf( "[killfeed] failed to register cvars\n" );
		return 0;
	}

	api->con_printf( "[killfeed] init\n" );

	api->register_hook( NEXORA_HOOK_DEATHMSG, OnDeathMsg, nullptr );
	api->register_hook( NEXORA_HOOK_PAINT_HUD, OnPaintHud, nullptr );
	api->register_hook( NEXORA_HOOK_HUD_ELEMENT_DRAW, OnHudElementDraw, nullptr );

	return 1;
}
