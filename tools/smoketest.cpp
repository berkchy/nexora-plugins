// smoketest — loads each plugin .so with a stubbed engine and fires the
// scenario hooks to catch crashes/regressions without launching the game.
#include <wrect.h>
#include <cl_dll.h>
#include <cvardef.h>
#include <screenfade.h>
#include <shake.h>
#include <triangleapi.h>
#include <render_api.h>
#include <cdll_dll.h>
#include <nexora/nexora_plugin.h>

#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

static float g_mockTime = 100.0f;
static int g_setScreenFadeCalls = 0;
static int g_fillBlendCalls = 0;
static int g_spriteLoadCalls = 0;

static cvar_t s_cvars[128];
static int s_cvarCount = 0;

static cvar_t *fake_RegisterVariable( const char *name, const char *value, int flags )
{
	if ( s_cvarCount >= 128 )
		return nullptr;

	cvar_t *c = &s_cvars[s_cvarCount++];
	c->name = name;
	c->string = value ? value : "";
	c->value = std::atof( c->string );
	c->flags = flags;
	c->next = nullptr;
	return c;
}

static cvar_t *fake_GetCvarPointer( const char *name )
{
	for ( int i = 0; i < s_cvarCount; i++ )
		if ( std::strcmp( s_cvars[i].name, name ) == 0 )
			return &s_cvars[i];
	return nullptr;
}

static void fake_ConPrintf( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	std::vfprintf( stdout, fmt, args );
	va_end( args );
}

static float fake_GetClientTime( void )
{
	return g_mockTime;
}

static void fake_SetScreenFade( screenfade_t *fade )
{
	( void )fade;
	++g_setScreenFadeCalls;
}

static void fake_FillRGBABlend( int x, int y, int w, int h, int r, int g, int b, int a )
{
	( void )x; ( void )y; ( void )w; ( void )h; ( void )r; ( void )g; ( void )b; ( void )a;
	++g_fillBlendCalls;
}

static void fake_FillRGBA( int x, int y, int w, int h, int r, int g, int b, int a )
{
	fake_FillRGBABlend( x, y, w, h, r, g, b, a );
}

static int fake_DrawConsoleString( int x, int y, char *data )
{
	( void )x; ( void )y; ( void )data;
	return 0;
}

static void fake_DrawSetTextColor( float r, float g, float b )
{
	( void )r; ( void )g; ( void )b;
}

static void fake_DrawConsoleStringLen( const char *string, int *length, int *height )
{
	*length = string ? static_cast<int>( std::strlen( string ) ) * 8 : 0;
	*height = 12;
}

static HSPRITE fake_SPR_Load( const char *name )
{
	( void )name;
	++g_spriteLoadCalls;
	return 1000 + g_spriteLoadCalls;
}

static int fake_SPR_Frames( HSPRITE handle )
{
	( void )handle;
	return 1;
}

static void fake_SPR_Set( HSPRITE handle, int r, int g, int b )
{
	( void )handle; ( void )r; ( void )g; ( void )b;
}

static void fake_SPR_DrawAdditive( int frame, int x, int y, const rect_s *rc )
{
	( void )frame; ( void )x; ( void )y; ( void )rc;
}

static client_sprite_t s_spriteList[] =
{
	{ "d_skull", "skull", 3001, 640, { 0, 32, 0, 32 } },
	{ "d_headshot", "headshot", 3002, 640, { 0, 16, 0, 16 } },
	{ "d_ak47", "ak47", 3003, 640, { 0, 48, 0, 48 } },
};
static const int s_spriteListCount = sizeof( s_spriteList ) / sizeof( s_spriteList[0] );

static client_sprite_t *fake_SPR_GetList( const char *path, int *count )
{
	( void )path;
	*count = s_spriteListCount;
	return s_spriteList;
}

static int fake_IsSpectateOnly( void )
{
	return 0;
}

static void fake_GetPlayerInfo( int ent_num, hud_player_info_t *pinfo )
{
	std::memset( pinfo, 0, sizeof( *pinfo ) );
	pinfo->name = nullptr;
	pinfo->ping = 30;
	pinfo->spectator = 0;

	if ( ent_num == 1 )
	{
		pinfo->name = const_cast<char *>( "local-player" );
		pinfo->thisplayer = 1;
	}
	else if ( ent_num == 2 )
	{
		pinfo->name = const_cast<char *>( "teammate" );
	}
	else if ( ent_num == 3 )
	{
		pinfo->name = const_cast<char *>( "opponent" );
	}
}

static const char *fake_PlayerInfo_ValueForKey( int playerNum, const char *key )
{
	( void )playerNum; ( void )key;
	return nullptr;
}



static struct triangleapi_s s_fakeTriApi = {};
static cl_enginefunc_t s_fakeEngine = {};
static render_api_t s_fakeRender = {};

static void fake_TriRenderMode( int mode ) { ( void )mode; }
static void fake_TriBegin( int primitiveCode ) { ( void )primitiveCode; }
static void fake_TriEnd( void ) { }
static void fake_TriColor4f( float r, float g, float b, float a ) { ( void )r; ( void )g; ( void )b; ( void )a; }
static void fake_TriTexCoord2f( float u, float v ) { ( void )u; ( void )v; }
static void fake_TriVertex3f( float x, float y, float z ) { ( void )x; ( void )y; ( void )z; }
static void fake_TriVertex3fv( float *worldPnt ) { ( void )worldPnt; }
static int fake_GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags )
{
	( void )name; ( void )buf; ( void )size; ( void )flags;
	return 500;
}
static void fake_GL_Bind( int tmu, unsigned int texnum )
{
	( void )tmu; ( void )texnum;
}
static void fake_GL_SelectTexture( int tmu )
{
	( void )tmu;
}

struct FakeEngineInit
{
	FakeEngineInit()
	{
		s_fakeTriApi.RenderMode = fake_TriRenderMode;
		s_fakeTriApi.Begin = fake_TriBegin;
		s_fakeTriApi.End = fake_TriEnd;
		s_fakeTriApi.Color4f = fake_TriColor4f;
		s_fakeTriApi.TexCoord2f = fake_TriTexCoord2f;
		s_fakeTriApi.Vertex3f = fake_TriVertex3f;
		s_fakeTriApi.Vertex3fv = fake_TriVertex3fv;

		s_fakeEngine.pTriAPI = &s_fakeTriApi;

		s_fakeEngine.pfnSPR_Load = fake_SPR_Load;
		s_fakeEngine.pfnSPR_Frames = fake_SPR_Frames;
		s_fakeEngine.pfnSPR_Set = fake_SPR_Set;
		s_fakeEngine.pfnSPR_DrawAdditive = fake_SPR_DrawAdditive;
		s_fakeEngine.pfnSPR_GetList = fake_SPR_GetList;
		s_fakeEngine.pfnFillRGBA = fake_FillRGBA;
		s_fakeEngine.pfnFillRGBABlend = fake_FillRGBABlend;
		s_fakeEngine.pfnRegisterVariable = fake_RegisterVariable;
		s_fakeEngine.pfnGetCvarPointer = fake_GetCvarPointer;
		s_fakeEngine.pfnGetPlayerInfo = fake_GetPlayerInfo;
		s_fakeEngine.pfnDrawConsoleString = fake_DrawConsoleString;
		s_fakeEngine.pfnDrawSetTextColor = fake_DrawSetTextColor;
		s_fakeEngine.pfnDrawConsoleStringLen = fake_DrawConsoleStringLen;
		s_fakeEngine.GetClientTime = fake_GetClientTime;
		s_fakeEngine.pfnSetScreenFade = fake_SetScreenFade;
		s_fakeEngine.IsSpectateOnly = fake_IsSpectateOnly;
		s_fakeEngine.PlayerInfo_ValueForKey = fake_PlayerInfo_ValueForKey;

		s_fakeRender.GL_LoadTexture = fake_GL_LoadTexture;
		s_fakeRender.GL_Bind = fake_GL_Bind;
		s_fakeRender.GL_SelectTexture = fake_GL_SelectTexture;
	}
};
static FakeEngineInit s_fakeInit;

static void fake_GetScreenSize( int *width, int *height )
{
	*width = 1280;
	*height = 720;
}

static nexora_hook_fn s_hookFns[NEXORA_HOOK_COUNT][8] = {};
static void *s_hookUsers[NEXORA_HOOK_COUNT][8] = {};

static void fake_RegisterHook( int hook_id, nexora_hook_fn fn, void *user_data )
{
	if ( hook_id < 0 || hook_id >= NEXORA_HOOK_COUNT || !fn )
		return;

	for ( int i = 0; i < 8; i++ )
	{
		if ( !s_hookFns[hook_id][i] )
		{
			s_hookFns[hook_id][i] = fn;
			s_hookUsers[hook_id][i] = user_data;
			return;
		}
	}
}

static void fake_UnregisterHook( int hook_id, nexora_hook_fn fn, void *user_data )
{
	if ( hook_id < 0 || hook_id >= NEXORA_HOOK_COUNT )
		return;

	for ( int i = 0; i < 8; i++ )
	{
		if ( s_hookFns[hook_id][i] == fn && s_hookUsers[hook_id][i] == user_data )
		{
			s_hookFns[hook_id][i] = nullptr;
			s_hookUsers[hook_id][i] = nullptr;
			return;
		}
	}
}

static nexora_plugin_api_t s_fakeApi =
{
	NEXORA_PLUGIN_API_VERSION,
	0,
	{ 0, 0 },
	fake_RegisterHook,
	fake_UnregisterHook,
	fake_ConPrintf,
	&s_fakeEngine,
	fake_GetScreenSize,
	&s_fakeRender,
};

static void FireHook( int hook_id, void *args )
{
	for ( int i = 0; i < 8; i++ )
	{
		if ( s_hookFns[hook_id][i] )
			s_hookFns[hook_id][i]( args, s_hookUsers[hook_id][i] );
	}
}

static int CountHooks( int hook_id )
{
	int count = 0;

	for ( int i = 0; i < 8; i++ )
		if ( s_hookFns[hook_id][i] )
			++count;

	return count;
}

static const char *HookName( int id )
{
	static const char *const names[NEXORA_HOOK_COUNT] =
	{
		"CLIENT_INIT", "CLIENT_SHUTDOWN", "CLIENT_CONNECT", "CLIENT_RESET",
		"PLAYER_MOVE_INIT", "PLAYER_MOVE", "CREATE_MOVE", "FRAME",
		"PAINT_HUD", "UPDATE_CLIENT_DATA", "PROCESS_PLAYER_STATE", "ADD_ENTITY",
		"CREATE_ENTITIES", "IN_COMMANDS", "DEATHMSG", "HUD_ELEMENT_DRAW",
		"NETMSG", "SCOREBOARD_TOGGLE", "AMMO_INFO", "CENTERPRINT", "HUD_ELEMENT_POS", "THIRDPERSON",
	};

	return ( id >= 0 && id < NEXORA_HOOK_COUNT ) ? names[id] : "?";
}

struct MsgWriter
{
	uint8_t buf[256];
	int size = 0;

	void Byte( uint8_t v )
	{
		buf[size++] = v;
	}

	void Short( int16_t v )
	{
		buf[size++] = static_cast<uint8_t>( v & 0xff );
		buf[size++] = static_cast<uint8_t>( ( v >> 8 ) & 0xff );
	}

	void Long( int32_t v )
	{
		for ( int i = 0; i < 4; i++ )
			buf[size++] = static_cast<uint8_t>( ( v >> ( i * 8 ) ) & 0xff );
	}

	void String( const char *s )
	{
		const size_t len = std::strlen( s ) + 1;

		std::memcpy( buf + size, s, len );
		size += static_cast<int>( len );
	}
};

static void FireNetMsg( const char *name, MsgWriter &w )
{
	nexora_netmsg_args_t args;

	args.name = name;
	args.size = w.size;
	args.buf = w.buf;

	FireHook( NEXORA_HOOK_NETMSG, &args );
}

static void TestPlugin( const char *path )
{
	std::memset( s_hookFns, 0, sizeof( s_hookFns ) );
	std::memset( s_hookUsers, 0, sizeof( s_hookUsers ) );

	g_mockTime = 100.0f;
	g_setScreenFadeCalls = 0;
	g_fillBlendCalls = 0;
	g_spriteLoadCalls = 0;

	void *handle = dlopen( path, RTLD_NOW | RTLD_NODELETE );

	if ( !handle )
	{
		std::printf( "FAIL: %s: %s\n", path, dlerror() );
		std::exit( 1 );
	}

	auto *query = reinterpret_cast<const nexora_plugin_info_t *(*)( void )>( dlsym( handle, "nexora_plugin_query" ) );
	auto *init = reinterpret_cast<int (*)( const nexora_plugin_api_t * )>( dlsym( handle, "nexora_plugin_init" ) );

	if ( !query || !init )
	{
		std::printf( "FAIL: %s: missing exports\n", path );
		std::exit( 1 );
	}

	const nexora_plugin_info_t *info = query();

	if ( !info || info->api_version != NEXORA_PLUGIN_API_VERSION )
	{
		std::printf( "FAIL: %s: bad plugin info\n", path );
		std::exit( 1 );
	}

	std::printf( "load %s: %s %s (priority %d)\n", path, info->name, info->version, info->priority );

	if ( !init( &s_fakeApi ) )
	{
		std::printf( "FAIL: %s: nexora_plugin_init returned 0\n", path );
		std::exit( 1 );
	}

	for ( int id = 0; id < NEXORA_HOOK_COUNT; id++ )
		if ( CountHooks( id ) )
			std::printf( "  hook %-18s x%d\n", HookName( id ), CountHooks( id ) );

	// advance time and draw one HUD frame
	g_mockTime = 100.5f;
	nexora_frame_args_t frameArgs { g_mockTime };
	FireHook( NEXORA_HOOK_FRAME, &frameArgs );

	nexora_redraw_args_t redrawArgs { 100.5f, 0 };
	FireHook( NEXORA_HOOK_PAINT_HUD, &redrawArgs );

	// baseline HUD state: nothing hidden
	nexora_update_client_data_args_t updateArgs { nullptr, 100.5f, 0 };
	FireHook( NEXORA_HOOK_UPDATE_CLIENT_DATA, &updateArgs );

	// server identity
	MsgWriter w;
	w.String( "Nexora Test Server" );
	FireNetMsg( "ServerName", w );

	w = {};
	w.Short( 180 );
	FireNetMsg( "RoundTime", w );

	// two CT on team 2, one T on team 1
	w = {};
	w.Byte( 1 ); w.Short( 12 ); w.Short( 3 ); w.Short( 0 ); w.Short( 0 );
	FireNetMsg( "ScoreInfo", w );

	w = {};
	w.Byte( 2 ); w.Short( 8 ); w.Short( 7 ); w.Short( 0 ); w.Short( 0 );
	FireNetMsg( "ScoreInfo", w );

	w = {};
	w.Byte( 3 ); w.Short( 15 ); w.Short( 4 ); w.Short( 0 ); w.Short( 0 );
	FireNetMsg( "ScoreInfo", w );

	w = {};
	w.Byte( 1 ); w.String( "CT" );
	FireNetMsg( "TeamInfo", w );

	w = {};
	w.Byte( 2 ); w.String( "CT" );
	FireNetMsg( "TeamInfo", w );

	w = {};
	w.Byte( 3 ); w.String( "TERRORIST" );
	FireNetMsg( "TeamInfo", w );

	w = {};
	w.String( "CT" ); w.Short( 20 ); w.Short( 10 );
	FireNetMsg( "TeamScore", w );

	w = {};
	w.String( "TERRORIST" ); w.Short( 12 ); w.Short( 14 );
	FireNetMsg( "TeamScore", w );

	w = {};
	w.Byte( 1 ); w.Long( 100 );
	FireNetMsg( "HealthInfo", w );

	w = {};
	w.Byte( 2 ); w.Long( 87 );
	FireNetMsg( "HealthInfo", w );

	w = {};
	w.Byte( 3 ); w.Long( 0 );
	FireNetMsg( "HealthInfo", w );

	w = {};
	w.Byte( 1 ); w.Long( 16000 );
	FireNetMsg( "Account", w );

	w = {};
	w.Byte( 2 ); w.Byte( 0x02 );
	FireNetMsg( "ScoreAttrib", w );

	w = {};
	w.Byte( 3 ); w.Byte( 0x01 );
	FireNetMsg( "ScoreAttrib", w );

	// fire a local-player kill
	nexora_deathmsg_args_t deathArgs {};

	deathArgs.killer = 1;
	deathArgs.victim = 3;
	deathArgs.headshot = 1;
	deathArgs.weapon = const_cast<char *>( "d_ak47" );
	deathArgs.killer_name = const_cast<char *>( "local-player" );
	deathArgs.victim_name = const_cast<char *>( "opponent" );
	deathArgs.killer_team = 2;
	deathArgs.victim_team = 1;
	deathArgs.killer_is_local = 1;
	deathArgs.victim_is_local = 0;
	deathArgs.is_suicide = 0;
	deathArgs.is_teamkill = 0;
	deathArgs.is_nonplayer_kill = 0;
	deathArgs.time = g_mockTime;

	FireHook( NEXORA_HOOK_DEATHMSG, &deathArgs );

	// ammo state
	nexora_ammo_args_t ammoArgs {};

	ammoArgs.clip = 30;
	ammoArgs.reserve = 90;
	ammoArgs.weapon_name = const_cast<char *>( "ak47" );

	FireHook( NEXORA_HOOK_AMMO_INFO, &ammoArgs );

	nexora_hud_element_pos_args_t posArgs { const_cast<char *>( "ammo" ), 320, 240 };
	FireHook( NEXORA_HOOK_HUD_ELEMENT_POS, &posArgs );

	nexora_hud_element_pos_args_t radarArgs { const_cast<char *>( "radar" ), 1433, 40 };
	FireHook( NEXORA_HOOK_HUD_ELEMENT_POS, &radarArgs );

	nexora_thirdperson_args_t thirdpersonArgs { 0 };
	FireHook( NEXORA_HOOK_THIRDPERSON, &thirdpersonArgs );
	std::printf( "  thirdperson unlock: allow=%d\n", thirdpersonArgs.allow );

	// center print message with veto
	nexora_centerprint_args_t centerprintArgs { const_cast<char *>( "TERRORISTS WIN" ), 1 };
	FireHook( NEXORA_HOOK_CENTERPRINT, &centerprintArgs );
	std::printf( "  centerprint veto: allow=%d\n", centerprintArgs.allow );

	nexora_redraw_args_t cpRedraw { g_mockTime + 0.1f, 0 };
	FireHook( NEXORA_HOOK_PAINT_HUD, &cpRedraw );
	FireHook( NEXORA_HOOK_PAINT_HUD, &cpRedraw );

	// scoreboard visible + hidden
	nexora_scoreboard_args_t toggleArgs { 1 };
	FireHook( NEXORA_HOOK_SCOREBOARD_TOGGLE, &toggleArgs );

	nexora_redraw_args_t redrawArgs2 { 100.6f, 0 };
	FireHook( NEXORA_HOOK_PAINT_HUD, &redrawArgs2 );
	FireHook( NEXORA_HOOK_PAINT_HUD, &redrawArgs2 );
	FireHook( NEXORA_HOOK_PAINT_HUD, &redrawArgs2 );

	toggleArgs.visible = 0;
	FireHook( NEXORA_HOOK_SCOREBOARD_TOGGLE, &toggleArgs );

	// advance time past kill feed hold time and fade out
	g_mockTime = 130.0f;
	frameArgs.time = g_mockTime;
	FireHook( NEXORA_HOOK_FRAME, &frameArgs );

	redrawArgs2.time = g_mockTime;
	FireHook( NEXORA_HOOK_PAINT_HUD, &redrawArgs2 );
	FireHook( NEXORA_HOOK_PAINT_HUD, &redrawArgs2 );

	// hide flags are respected (HIDEHUD_ALL = 1<<2)
	nexora_update_client_data_args_t hideArgs { nullptr, g_mockTime, 4 };
	FireHook( NEXORA_HOOK_UPDATE_CLIENT_DATA, &hideArgs );
	FireHook( NEXORA_HOOK_PAINT_HUD, &redrawArgs2 );

	hideArgs.hide_hud = 0;
	FireHook( NEXORA_HOOK_UPDATE_CLIENT_DATA, &hideArgs );
	FireHook( NEXORA_HOOK_PAINT_HUD, &redrawArgs2 );

	// veto checks
	nexora_hud_element_args_t elementArgs { nullptr, const_cast<char *>( "deathnotice" ), 1 };
	FireHook( NEXORA_HOOK_HUD_ELEMENT_DRAW, &elementArgs );
	std::printf( "  veto deathnotice: allow=%d\n", elementArgs.allow );

	elementArgs.name = const_cast<char *>( "scoreboard" );
	elementArgs.allow = 1;
	FireHook( NEXORA_HOOK_HUD_ELEMENT_DRAW, &elementArgs );
	std::printf( "  veto scoreboard:  allow=%d\n", elementArgs.allow );

	elementArgs.name = const_cast<char *>( "health" );
	elementArgs.allow = 1;
	FireHook( NEXORA_HOOK_HUD_ELEMENT_DRAW, &elementArgs );
	std::printf( "  veto health:      allow=%d\n", elementArgs.allow );

	std::printf( "PASS: %s (fillblend calls=%d, screenfade calls=%d, sprite loads=%d)\n\n",
		path, g_fillBlendCalls, g_setScreenFadeCalls, g_spriteLoadCalls );
}

int main( int argc, char **argv )
{
	if ( argc < 2 )
	{
		std::fprintf( stderr, "usage: %s <plugin.so>...\n", argv[0] );
		return 2;
	}

	for ( int i = 1; i < argc; i++ )
		TestPlugin( argv[i] );

	std::printf( "all plugins loaded and ticked\n" );
	return 0;
}
