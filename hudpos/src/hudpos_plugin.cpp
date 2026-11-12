#include <nexora/nexora_plugin.h>
#include <wrect.h>
#include <cl_dll.h>
#include <cvardef.h>
#include <cdll_dll.h>

#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace
{

const nexora_plugin_info_t s_info =
{
	"hudpos",
	"1.0.0",
	NEXORA_PLUGIN_API_VERSION,
	35,
};

const nexora_plugin_api_t *g_api = nullptr;
cl_enginefunc_t *g_eng = nullptr;

cvar_t *hud_ammo_x = nullptr;
cvar_t *hud_ammo_y = nullptr;
cvar_t *hud_timer_x = nullptr;
cvar_t *hud_timer_y = nullptr;
cvar_t *hud_health_x = nullptr;
cvar_t *hud_health_y = nullptr;
cvar_t *hud_armor_x = nullptr;
cvar_t *hud_armor_y = nullptr;
cvar_t *hud_money_x = nullptr;
cvar_t *hud_money_y = nullptr;
cvar_t *hud_radar_x = nullptr;
cvar_t *hud_radar_y = nullptr;

struct HpElement
{
	const char *name;
	cvar_t **x;
	cvar_t **y;
};

const HpElement s_elements[] =
{
	{ "ammo", &hud_ammo_x, &hud_ammo_y },
	{ "ammosecondary", &hud_ammo_x, &hud_ammo_y },
	{ "timer", &hud_timer_x, &hud_timer_y },
	{ "health", &hud_health_x, &hud_health_y },
	{ "battery", &hud_armor_x, &hud_armor_y },
	{ "money", &hud_money_x, &hud_money_y },
	{ "radar", &hud_radar_x, &hud_radar_y },
};

enum HpAxisKind
{
	HP_AUTO,
	HP_VALUE,
	HP_HIDE,
};

bool HpEq( const char *a, const char *b )
{
	while ( a && b && *a && *b )
	{
		if ( std::tolower( (unsigned char)*a ) != std::tolower( (unsigned char)*b ) )
			return false;
		++a;
		++b;
	}
	return a && b && *a == '\0' && *b == '\0';
}

HpAxisKind HpParseAxis( const char *s, int screenSize, int &out )
{
	out = 0;

	if ( !s || !s[0] || HpEq( s, "auto" ) )
		return HP_AUTO;

	const size_t sLen = std::strlen( s );

	if ( sLen > 1 && s[sLen - 1] == '%' )
	{
		char *end = nullptr;
		float pct = std::strtof( s, &end );

		if ( end && end == s + sLen - 1 )
		{
			if ( pct == -99999.0f )
				return HP_HIDE;

			float fr = pct / 100.0f;

			if ( fr < 0.0f )
				fr += 1.0f;

			out = (int)std::lround( fr * (float)screenSize );
			return HP_VALUE;
		}

		return HP_AUTO;
	}

	char *end = nullptr;
	long v = std::strtol( s, &end, 10 );

	if ( end == s )
		return HP_AUTO;

	if ( v == -99999 )
		return HP_HIDE;

	if ( v < 0 )
		v += screenSize;

	out = (int)v;
	return HP_VALUE;
}

const HpElement *HpFind( const char *name )
{
	for ( size_t i = 0; i < sizeof( s_elements ) / sizeof( s_elements[0] ); ++i )
	{
		if ( name && HpEq( name, s_elements[i].name ) )
			return &s_elements[i];
	}
	return nullptr;
}

void HpScreenSize( int &width, int &height )
{
	width = 640;
	height = 480;

	if ( g_api && g_api->get_screen_size )
		g_api->get_screen_size( &width, &height );
}

void OnHudElementDraw( void *args, void *user_data )
{
	auto *element = static_cast<nexora_hud_element_args_t *>( args );

	if ( !element || !element->name )
		return;

	const HpElement *entry = HpFind( element->name );

	if ( !entry )
		return;

	int dummy = 0;

	if ( *entry->x && ( *entry->x )->string )
	{
		int sw, sh;
		HpScreenSize( sw, sh );

		if ( HpParseAxis( ( *entry->x )->string, sw, dummy ) == HP_HIDE )
		{
			element->allow = 0;
			return;
		}
	}

	if ( *entry->y && ( *entry->y )->string )
	{
		int sw, sh;
		HpScreenSize( sw, sh );

		if ( HpParseAxis( ( *entry->y )->string, sh, dummy ) == HP_HIDE )
			element->allow = 0;
	}
}

void OnHudElementPos( void *args, void *user_data )
{
	auto *pos = static_cast<nexora_hud_element_pos_args_t *>( args );

	if ( !pos || !pos->name )
		return;

	const HpElement *entry = HpFind( pos->name );

	if ( !entry )
		return;

	int sw, sh;
	HpScreenSize( sw, sh );

	if ( *entry->x )
	{
		int value = 0;
		HpAxisKind kind = HpParseAxis( ( *entry->x )->string, sw, value );

		if ( kind == HP_VALUE )
			pos->x = value;
	}

	if ( *entry->y )
	{
		int value = 0;
		HpAxisKind kind = HpParseAxis( ( *entry->y )->string, sh, value );

		if ( kind == HP_VALUE )
			pos->y = value;
	}
}

bool RegisterCVars()
{
	hud_ammo_x = g_eng->pfnRegisterVariable( "hud_ammo_x", "auto", FCVAR_ARCHIVE );
	hud_ammo_y = g_eng->pfnRegisterVariable( "hud_ammo_y", "auto", FCVAR_ARCHIVE );
	hud_timer_x = g_eng->pfnRegisterVariable( "hud_timer_x", "auto", FCVAR_ARCHIVE );
	hud_timer_y = g_eng->pfnRegisterVariable( "hud_timer_y", "auto", FCVAR_ARCHIVE );
	hud_health_x = g_eng->pfnRegisterVariable( "hud_health_x", "auto", FCVAR_ARCHIVE );
	hud_health_y = g_eng->pfnRegisterVariable( "hud_health_y", "auto", FCVAR_ARCHIVE );
	hud_armor_x = g_eng->pfnRegisterVariable( "hud_armor_x", "auto", FCVAR_ARCHIVE );
	hud_armor_y = g_eng->pfnRegisterVariable( "hud_armor_y", "auto", FCVAR_ARCHIVE );
	hud_money_x = g_eng->pfnRegisterVariable( "hud_money_x", "auto", FCVAR_ARCHIVE );
	hud_money_y = g_eng->pfnRegisterVariable( "hud_money_y", "auto", FCVAR_ARCHIVE );
	hud_radar_x = g_eng->pfnRegisterVariable( "hud_radar_x", "auto", FCVAR_ARCHIVE );
	hud_radar_y = g_eng->pfnRegisterVariable( "hud_radar_y", "auto", FCVAR_ARCHIVE );

	return hud_ammo_x && hud_ammo_y && hud_timer_x && hud_timer_y &&
		hud_health_x && hud_health_y && hud_armor_x && hud_armor_y &&
		hud_money_x && hud_money_y && hud_radar_x && hud_radar_y;
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

	if ( !RegisterCVars() )
	{
		api->con_printf( "[hudpos] failed to register cvars\n" );
		return 0;
	}

	api->con_printf( "[hudpos] init\n" );

	api->register_hook( NEXORA_HOOK_HUD_ELEMENT_DRAW, OnHudElementDraw, nullptr );
	api->register_hook( NEXORA_HOOK_HUD_ELEMENT_POS, OnHudElementPos, nullptr );

	return 1;
}
