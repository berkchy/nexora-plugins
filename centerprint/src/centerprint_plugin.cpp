#include <nexora/nexora_plugin.h>
#include <wrect.h>
#include <cl_dll.h>
#include <cvardef.h>
#include <cdll_dll.h>

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace
{

const nexora_plugin_info_t s_info =
{
	"centerprint",
	"1.0.0",
	NEXORA_PLUGIN_API_VERSION,
	30,
};

const nexora_plugin_api_t *g_api = nullptr;
cl_enginefunc_t *g_eng = nullptr;

cvar_t *hud_centerprint = nullptr;
cvar_t *hud_centerprint_fadein = nullptr;
cvar_t *hud_centerprint_fadeout = nullptr;
cvar_t *hud_centerprint_holdtime = nullptr;
cvar_t *hud_centerprint_color = nullptr;
cvar_t *hud_centerprint_alpha = nullptr;
cvar_t *hud_centerprint_yfract = nullptr;
cvar_t *hud_centerprint_bg = nullptr;
cvar_t *hud_centerprint_bgcolor = nullptr;
cvar_t *hud_centerprint_bgalpha = nullptr;

char s_text[256] = "";
float s_startTime = -1.0f;
int g_hideFlags = 0;

void CpParseColor( const char *value, int defR, int defG, int defB, int &r, int &g, int &b )
{
	r = defR;
	g = defG;
	b = defB;

	if ( value && value[0] )
	{
		if ( std::sscanf( value, "%d %d %d", &r, &g, &b ) != 3 )
		{
			r = defR;
			g = defG;
			b = defB;
		}
	}

	r = std::max( 0, std::min( 255, r ) );
	g = std::max( 0, std::min( 255, g ) );
	b = std::max( 0, std::min( 255, b ) );
}

bool CpEnabled()
{
	return hud_centerprint && hud_centerprint->value >= 0.5f;
}

void CpResetState()
{
	s_text[0] = '\0';
	s_startTime = -1.0f;
}

void OnCenterPrint( void *args, void *user_data )
{
	auto *centerprint = static_cast<nexora_centerprint_args_t *>( args );

	if ( !centerprint || !CpEnabled() )
		return;

	if ( centerprint->text && centerprint->text[0] )
	{
		std::strncpy( s_text, centerprint->text, sizeof( s_text ) - 1 );
		s_text[sizeof( s_text ) - 1] = '\0';
		s_startTime = static_cast<float>( g_eng->GetClientTime() );
	}

	centerprint->allow = 0;
}

void OnUpdateClientData( void *args, void *user_data )
{
	const auto *update = static_cast<const nexora_update_client_data_args_t *>( args );

	if ( update )
		g_hideFlags = update->hide_hud;
}

float CpEnvelope( float now )
{
	float fadeIn = hud_centerprint_fadein ? hud_centerprint_fadein->value : 0.25f;
	float fadeOut = hud_centerprint_fadeout ? hud_centerprint_fadeout->value : 0.5f;
	float hold = hud_centerprint_holdtime ? hud_centerprint_holdtime->value : 4.0f;

	fadeIn = std::max( 0.0f, std::min( 2.0f, fadeIn ) );
	fadeOut = std::max( 0.0f, std::min( 8.0f, fadeOut ) );
	hold = std::max( 0.0f, std::min( 30.0f, hold ) );

	const float t = now - s_startTime;

	if ( t < fadeIn )
		return fadeIn > 0.0f ? t / fadeIn : 1.0f;

	if ( t < fadeIn + hold )
		return 1.0f;

	if ( t < fadeIn + hold + fadeOut )
		return fadeOut > 0.0f ? 1.0f - ( t - fadeIn - hold ) / fadeOut : 0.0f;

	return -1.0f;
}

float CpSmooth( float x )
{
	x = std::max( 0.0f, std::min( 1.0f, x ) );
	return x * x * ( 3.0f - 2.0f * x );
}

void OnPaintHud( void *args, void *user_data )
{
	if ( g_hideFlags & HIDEHUD_ALL )
		return;

	if ( s_startTime < 0.0f || !CpEnabled() )
		return;

	float now = static_cast<float>( g_eng->GetClientTime() );

	const auto *redraw = static_cast<const nexora_redraw_args_t *>( args );

	if ( redraw )
		now = redraw->time;

	const float envelope = CpEnvelope( now );

	if ( envelope < 0.0f )
	{
		CpResetState();
		return;
	}

	float alpha = CpSmooth( envelope );

	const float userAlpha = hud_centerprint_alpha ? hud_centerprint_alpha->value : 255.0f;
	alpha *= std::max( 0.0f, std::min( 255.0f, userAlpha ) ) / 255.0f;

	if ( alpha <= 0.0f )
		return;

	int screenWidth = 640;
	int screenHeight = 480;

	if ( g_api && g_api->get_screen_size )
		g_api->get_screen_size( &screenWidth, &screenHeight );

	int r = 255, g = 214, b = 0;
	CpParseColor( hud_centerprint_color ? hud_centerprint_color->string : nullptr, 255, 214, 0, r, g, b );

	float yFract = hud_centerprint_yfract ? hud_centerprint_yfract->value : 0.333f;
	yFract = std::max( 0.0f, std::min( 1.0f, yFract ) );

	char lines[4][256];
	int lineCount = 0;
	const char *cursor = s_text;

	while ( *cursor && lineCount < 4 )
	{
		const char *nl = std::strchr( cursor, '\n' );
		const size_t len = nl ? (size_t)( nl - cursor ) : std::strlen( cursor );
		const size_t copy = std::min( len, sizeof( lines[0] ) - 1 );
		std::memcpy( lines[lineCount], cursor, copy );
		lines[lineCount][copy] = '\0';
		++lineCount;
		cursor = nl ? nl + 1 : cursor + len;
	}

	int height = 16;

	if ( g_eng->pfnDrawConsoleStringLen )
	{
		int probeWidth = 0;
		int probeHeight = 0;

		g_eng->pfnDrawConsoleStringLen( (char *)"Mg", &probeWidth, &probeHeight );

		if ( probeHeight > 0 )
			height = probeHeight;
	}

	int len[4] = { 0, 0, 0, 0 };

	for ( int i = 0; i < lineCount; ++i )
	{
		int lineHeight = height;

		if ( g_eng->pfnDrawConsoleStringLen )
			g_eng->pfnDrawConsoleStringLen( lines[i], &len[i], &lineHeight );
		else
			len[i] = (int)std::strlen( lines[i] ) * 8;
	}

	const int blockHeight = lineCount * height;
	int y = (int)( screenHeight * yFract );

	if ( y + blockHeight > screenHeight - 8 )
		y = screenHeight - 8 - blockHeight;

	const bool drawBg = hud_centerprint_bg && hud_centerprint_bg->value >= 0.5f;
	int bgR = 0, bgG = 0, bgB = 0;

	if ( drawBg )
		CpParseColor( hud_centerprint_bgcolor ? hud_centerprint_bgcolor->string : nullptr, 0, 0, 0, bgR, bgG, bgB );

	const float bgAlphaBase = hud_centerprint_bgalpha ? hud_centerprint_bgalpha->value : 140.0f;
	const int bgAlpha = std::max( 0, std::min( 255, (int)( bgAlphaBase * alpha ) ) );

	if ( drawBg && bgAlpha > 0 && g_eng->pfnFillRGBABlend )
	{
		const int pad = 12;

		for ( int i = 0; i < lineCount; ++i )
		{
			const int x = ( screenWidth - len[i] ) / 2;

			g_eng->pfnFillRGBABlend( x - pad, y + i * height - 2,
				len[i] + pad * 2, height + 4, bgR, bgG, bgB, bgAlpha );
		}
	}

	if ( g_eng->pfnDrawSetTextColor )
		g_eng->pfnDrawSetTextColor( ( r / 255.0f ) * alpha, ( g / 255.0f ) * alpha, ( b / 255.0f ) * alpha );

	for ( int i = 0; i < lineCount; ++i )
	{
		const int x = ( screenWidth - len[i] ) / 2;

		g_eng->pfnDrawConsoleString( x, y + i * height, lines[i] );
	}
}

bool RegisterCVars()
{
	hud_centerprint = g_eng->pfnRegisterVariable( "hud_centerprint", "1", FCVAR_ARCHIVE );
	hud_centerprint_fadein = g_eng->pfnRegisterVariable( "hud_centerprint_fadein", "0.25", FCVAR_ARCHIVE );
	hud_centerprint_fadeout = g_eng->pfnRegisterVariable( "hud_centerprint_fadeout", "0.5", FCVAR_ARCHIVE );
	hud_centerprint_holdtime = g_eng->pfnRegisterVariable( "hud_centerprint_holdtime", "4", FCVAR_ARCHIVE );
	hud_centerprint_color = g_eng->pfnRegisterVariable( "hud_centerprint_color", "255 214 0", FCVAR_ARCHIVE );
	hud_centerprint_alpha = g_eng->pfnRegisterVariable( "hud_centerprint_alpha", "255", FCVAR_ARCHIVE );
	hud_centerprint_yfract = g_eng->pfnRegisterVariable( "hud_centerprint_yfract", "0.333", FCVAR_ARCHIVE );
	hud_centerprint_bg = g_eng->pfnRegisterVariable( "hud_centerprint_bg", "0", FCVAR_ARCHIVE );
	hud_centerprint_bgcolor = g_eng->pfnRegisterVariable( "hud_centerprint_bgcolor", "0 0 0", FCVAR_ARCHIVE );
	hud_centerprint_bgalpha = g_eng->pfnRegisterVariable( "hud_centerprint_bgalpha", "140", FCVAR_ARCHIVE );

	return hud_centerprint && hud_centerprint_fadein && hud_centerprint_fadeout &&
		hud_centerprint_holdtime && hud_centerprint_color && hud_centerprint_alpha &&
		hud_centerprint_yfract && hud_centerprint_bg && hud_centerprint_bgcolor &&
		hud_centerprint_bgalpha;
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
		api->con_printf( "[centerprint] failed to register cvars\n" );
		return 0;
	}

	api->con_printf( "[centerprint] init\n" );

	api->register_hook( NEXORA_HOOK_CENTERPRINT, OnCenterPrint, nullptr );
	api->register_hook( NEXORA_HOOK_PAINT_HUD, OnPaintHud, nullptr );
	api->register_hook( NEXORA_HOOK_UPDATE_CLIENT_DATA, OnUpdateClientData, nullptr );

	return 1;
}
