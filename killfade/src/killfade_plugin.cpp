#include <nexora/nexora_plugin.h>
#include <wrect.h>
#include <cl_dll.h>
#include <cvardef.h>
#include <screenfade.h>
#include <shake.h>

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace
{

const nexora_plugin_info_t s_info =
{
	"killfade",
	"1.0.0",
	NEXORA_PLUGIN_API_VERSION,
	50,
};

const nexora_plugin_api_t *g_api = nullptr;
cl_enginefunc_t *g_eng = nullptr;

cvar_t *hud_killfade = nullptr;
cvar_t *hud_killfade_fadein = nullptr;
cvar_t *hud_killfade_fadeout = nullptr;
cvar_t *hud_killfade_fxtime = nullptr;
cvar_t *hud_killfade_holdtime = nullptr;
cvar_t *hud_killfade_color = nullptr;
cvar_t *hud_killfade_alpha = nullptr;

float kfScreenFadeOutAt = -1.0f;

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

void KfApplyScreenFade( float duration, float hold, int flags, int r, int g, int b, int a )
{
	if ( !g_eng->pfnSetScreenFade || duration <= 0.0f || a <= 0 )
		return;

	screenfade_t fade;
	std::memset( &fade, 0, sizeof( fade ) );

	const float now = g_eng->GetClientTime();

	fade.fader = static_cast<byte>( r );
	fade.fadeg = static_cast<byte>( g );
	fade.fadeb = static_cast<byte>( b );
	fade.fadealpha = static_cast<byte>( a );
	fade.fadeFlags = flags;
	fade.fadeEnd = duration;
	fade.fadeReset = hold;

	if ( flags & FFADE_OUT )
	{
		fade.fadeSpeed = -static_cast<float>( a ) / fade.fadeEnd;
		fade.fadeEnd += now;
		fade.fadeReset += fade.fadeEnd;
	}
	else
	{
		fade.fadeSpeed = static_cast<float>( a ) / fade.fadeEnd;
		fade.fadeReset += now;
		fade.fadeEnd += fade.fadeReset;
	}

	g_eng->pfnSetScreenFade( &fade );
}

void KfStartScreenFade()
{
	if ( !hud_killfade || hud_killfade->value < 0.5f || !g_eng->pfnSetScreenFade )
		return;

	int r, g, b;
	KfParseColor( hud_killfade_color ? hud_killfade_color->string : nullptr, r, g, b );

	int a = hud_killfade_alpha ? static_cast<int>( hud_killfade_alpha->value ) : 70;
	a = std::max( 0, std::min( 255, a ) );
	if ( a <= 0 )
		return;

	float fxTime = hud_killfade_fxtime ? hud_killfade_fxtime->value : 0.25f;
	fxTime = std::max( 0.02f, std::min( 2.0f, fxTime ) );

	float hold = hud_killfade_holdtime ? hud_killfade_holdtime->value : 0.12f;
	hold = std::max( 0.0f, std::min( 2.0f, hold ) );

	const bool fadeIn = hud_killfade_fadein && hud_killfade_fadein->value >= 0.5f;
	const bool fadeOut = hud_killfade_fadeout && hud_killfade_fadeout->value >= 0.5f;

	kfScreenFadeOutAt = -1.0f;

	if ( fadeIn )
	{
		KfApplyScreenFade( fxTime, hold, FFADE_OUT, r, g, b, a );
		if ( fadeOut )
			kfScreenFadeOutAt = g_eng->GetClientTime() + fxTime + hold - 0.02f;
	}
	else if ( fadeOut )
	{
		KfApplyScreenFade( fxTime, hold, FFADE_IN, r, g, b, a );
	}
}

void KfUpdateScreenFade()
{
	if ( kfScreenFadeOutAt < 0.0f )
		return;

	if ( g_eng->GetClientTime() < kfScreenFadeOutAt )
		return;

	kfScreenFadeOutAt = -1.0f;

	int r, g, b;
	KfParseColor( hud_killfade_color ? hud_killfade_color->string : nullptr, r, g, b );

	int a = hud_killfade_alpha ? static_cast<int>( hud_killfade_alpha->value ) : 70;
	a = std::max( 0, std::min( 255, a ) );

	float fxTime = hud_killfade_fxtime ? hud_killfade_fxtime->value : 0.25f;
	fxTime = std::max( 0.02f, std::min( 2.0f, fxTime ) );

	KfApplyScreenFade( fxTime, 0.0f, FFADE_IN, r, g, b, a );
}

void OnDeathMsg( void *args, void *user_data )
{
	const auto *death = static_cast<const nexora_deathmsg_args_t *>( args );

	if ( !death )
		return;

	if ( death->killer_is_local &&
		!death->is_nonplayer_kill &&
		!death->is_suicide &&
		!death->is_teamkill )
	{
		KfStartScreenFade();
	}
}

void OnFrame( void *args, void *user_data )
{
	KfUpdateScreenFade();
}

bool RegisterCVars()
{
	hud_killfade = g_eng->pfnRegisterVariable( "hud_killfade", "1", FCVAR_ARCHIVE );
	hud_killfade_fadein = g_eng->pfnRegisterVariable( "hud_killfade_fadein", "1", FCVAR_ARCHIVE );
	hud_killfade_fadeout = g_eng->pfnRegisterVariable( "hud_killfade_fadeout", "1", FCVAR_ARCHIVE );
	hud_killfade_fxtime = g_eng->pfnRegisterVariable( "hud_killfade_fxtime", "0.25", FCVAR_ARCHIVE );
	hud_killfade_holdtime = g_eng->pfnRegisterVariable( "hud_killfade_holdtime", "0.12", FCVAR_ARCHIVE );
	hud_killfade_color = g_eng->pfnRegisterVariable( "hud_killfade_color", "255 255 255", FCVAR_ARCHIVE );
	hud_killfade_alpha = g_eng->pfnRegisterVariable( "hud_killfade_alpha", "70", FCVAR_ARCHIVE );

	return hud_killfade && hud_killfade_fadein && hud_killfade_fadeout &&
		hud_killfade_fxtime && hud_killfade_holdtime &&
		hud_killfade_color && hud_killfade_alpha;
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
		api->con_printf( "[killfade] failed to register cvars\n" );
		return 0;
	}

	api->con_printf( "[killfade] init\n" );

	api->register_hook( NEXORA_HOOK_DEATHMSG, OnDeathMsg, nullptr );
	api->register_hook( NEXORA_HOOK_FRAME, OnFrame, nullptr );

	return 1;
}
