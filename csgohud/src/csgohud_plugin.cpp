//
// csgohud plugin — CS:GO-style HUD for the Nexora client.
//
#include <nexora/nexora_plugin.h>
#include <wrect.h>
#include <cl_dll.h>
#include <cvardef.h>
#include <parsemsg.h>
#include <triangleapi.h>
#include <render_api.h>
#include <cdll_dll.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <algorithm>

namespace
{

const int SB_MAX_PLAYERS = 32;
const int SB_CONTAINER = SB_MAX_PLAYERS + 1;

const nexora_plugin_info_t s_info =
{
	"csgohud",
	"1.0.0",
	NEXORA_PLUGIN_API_VERSION,
	50,
};

const nexora_plugin_api_t *g_api = nullptr;
cl_enginefunc_t *g_eng = nullptr;
render_api_t *g_render = nullptr;

cvar_t *hud_style = nullptr;
cvar_t *hud_csgo_scale = nullptr;

struct CsNetState
{
	int health = 100;
	int team = TEAM_UNASSIGNED;
	bool dead = false;
};

CsNetState g_net[SB_CONTAINER];

int g_health = 100;
int g_armor = 100;
int g_money = 0;
int g_localIdx = 0;

int g_ammoClip = -1;
int g_ammoReserve = -1;
char g_weaponName[64] = {};

int g_roundTime = -1;
float g_roundStart = -1.0f;

int g_ctScore = 0;
int g_ttScore = 0;

bool g_scoreboardVisible = false;
int g_hideFlags = 0;

struct CsIcon
{
	const char *path;
	int texture;
	bool attempted;
};

CsIcon s_iconCash = { "gfx/scoreboard/dollar.png", 0, false };
CsIcon s_iconAmmo = { "gfx/scoreboard/ammo.png", 0, false };
CsIcon s_iconTeamCT = { "gfx/scoreboard/ct_team.png", 0, false };
CsIcon s_iconTeamT = { "gfx/scoreboard/t_team.png", 0, false };

CsIcon s_iconDigits[10] =
{
	{ "gfx/scoreboard/timer_0.png", 0, false },
	{ "gfx/scoreboard/timer_1.png", 0, false },
	{ "gfx/scoreboard/timer_2.png", 0, false },
	{ "gfx/scoreboard/timer_3.png", 0, false },
	{ "gfx/scoreboard/timer_4.png", 0, false },
	{ "gfx/scoreboard/timer_5.png", 0, false },
	{ "gfx/scoreboard/timer_6.png", 0, false },
	{ "gfx/scoreboard/timer_7.png", 0, false },
	{ "gfx/scoreboard/timer_8.png", 0, false },
	{ "gfx/scoreboard/timer_9.png", 0, false },
};
CsIcon s_iconColon = { "gfx/scoreboard/timer_colon.png", 0, false };
CsIcon s_iconDash = { "gfx/scoreboard/timer_dash.png", 0, false };

int CsStricmp( const char *a, const char *b )
{
	while ( *a && *b )
	{
		const int ca = std::tolower( static_cast<unsigned char>( *a ) );
		const int cb = std::tolower( static_cast<unsigned char>( *b ) );

		if ( ca != cb )
			return ca - cb;

		++a;
		++b;
	}

	return std::tolower( static_cast<unsigned char>( *a ) ) - std::tolower( static_cast<unsigned char>( *b ) );
}

int CsLoad( CsIcon &icon )
{
	if ( !icon.texture && !icon.attempted )
	{
		icon.attempted = true;
		icon.texture = g_render ? g_render->GL_LoadTexture( icon.path, nullptr, 0, TF_NEAREST | TF_NOMIPMAP | TF_CLAMP ) : 0;
	}

	return icon.texture;
}

void CsDrawQuad( float x1, float y1, float x2, float y2 )
{
	g_eng->pTriAPI->Begin( TRI_QUADS );
	g_eng->pTriAPI->TexCoord2f( 0, 0 );
	g_eng->pTriAPI->Vertex3f( x1, y1, 0 );
	g_eng->pTriAPI->TexCoord2f( 0, 1 );
	g_eng->pTriAPI->Vertex3f( x1, y2, 0 );
	g_eng->pTriAPI->TexCoord2f( 1, 1 );
	g_eng->pTriAPI->Vertex3f( x2, y2, 0 );
	g_eng->pTriAPI->TexCoord2f( 1, 0 );
	g_eng->pTriAPI->Vertex3f( x2, y1, 0 );
	g_eng->pTriAPI->End( );
}

void CsDrawIconRect( CsIcon &icon, int x, int y, int width, int height, int r, int g, int b, int a )
{
	if ( width <= 0 || height <= 0 )
		return;

	const int texture = CsLoad( icon );

	if ( !texture )
		return;

	g_render->GL_SelectTexture( 0 );
	g_render->GL_Bind( 0, texture );
	g_eng->pTriAPI->RenderMode( kRenderTransAlpha );
	g_eng->pTriAPI->Color4f( r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f );

	CsDrawQuad( (float)x, (float)y, (float)( x + width ), (float)( y + height ) );
}

void CsDrawIcon( CsIcon &icon, int x, int y, int size, int r, int g, int b, int a )
{
	CsDrawIconRect( icon, x, y, size, size, r, g, b, a );
}

void CsDrawHealthCross( int x, int y, int size, int r, int g, int b, int a )
{
	int thickness = size / 3;

	if ( thickness < 1 )
		thickness = 1;

	const int offset = ( size - thickness ) / 2;

	g_eng->pfnFillRGBABlend( x + offset, y, thickness, size, r, g, b, a );
	g_eng->pfnFillRGBABlend( x, y + offset, size, thickness, r, g, b, a );
}

void CsDrawArmorShield( int x, int y, int size, int r, int g, int b, int a )
{
	if ( size <= 0 )
		return;

	const int topHeight = size / 5;

	for ( int row = 0; row < size; ++row )
	{
		int width = size;

		if ( row > topHeight )
		{
			const float t = static_cast<float>( row - topHeight ) / static_cast<float>( size - topHeight );
			width = static_cast<int>( size * ( 1.0f - t * t ) );
		}

		const int inset = ( size - width ) / 2;
		g_eng->pfnFillRGBABlend( x + inset, y + row, width, 1, r, g, b, a );
	}
}

int CsGlyphBaseWidth( char c )
{
	if ( c >= '0' && c <= '9' )
		return 18;

	if ( c == ':' )
		return 8;

	if ( c == '-' )
		return 12;

	return 0;
}

int CsGlyphWidth( char c, int height )
{
	const int base = CsGlyphBaseWidth( c );

	if ( base <= 0 )
		return 0;

	return ( base * height + 13 ) / 26;
}

int CsTextWidth( const char *text, int height )
{
	int width = 0;
	const int gap = ( 2 * height + 13 ) / 26;

	for ( const char *p = text; *p; ++p )
	{
		const int glyph = CsGlyphWidth( *p, height );

		if ( glyph <= 0 )
			continue;

		if ( width > 0 )
			width += gap;

		width += glyph;
	}

	return width;
}

void CsDrawText( const char *text, int x, int y, int height, int r, int g, int b, int a )
{
	const int gap = ( 2 * height + 13 ) / 26;

	for ( const char *p = text; *p; ++p )
	{
		CsIcon *icon = nullptr;

		if ( *p >= '0' && *p <= '9' )
			icon = &s_iconDigits[*p - '0'];
		else if ( *p == ':' )
			icon = &s_iconColon;
		else if ( *p == '-' )
			icon = &s_iconDash;

		const int width = CsGlyphWidth( *p, height );

		if ( !icon || width <= 0 )
			continue;

		CsDrawIconRect( *icon, x, y, width, height, r, g, b, a );
		x += width + gap;
	}
}

int FindLocalIdx()
{
	if ( g_localIdx )
		return g_localIdx;

	if ( !g_eng->pfnGetPlayerInfo )
		return 0;

	for ( int i = 1; i <= SB_MAX_PLAYERS; ++i )
	{
		hud_player_info_t info;
		g_eng->pfnGetPlayerInfo( i, &info );

		if ( info.thisplayer )
		{
			g_localIdx = i;
			return g_localIdx;
		}
	}

	return 0;
}

void CsDrawHud()
{
	const bool hideAll = ( g_hideFlags & HIDEHUD_ALL ) != 0;

	int screenWidth = 640;
	int screenHeight = 480;

	if ( g_api && g_api->get_screen_size )
		g_api->get_screen_size( &screenWidth, &screenHeight );

	float scale = screenHeight / 480.0f;

	if ( scale < 1.0f )
		scale = 1.0f;

	if ( scale > 1.8f )
		scale = 1.8f;

	const float userScale = hud_csgo_scale ? hud_csgo_scale->value : 1.0f;

	scale *= std::max( 0.5f, std::min( 2.0f, userScale ) );

	if ( scale < 0.5f )
		scale = 0.5f;

	const int pad = (int)( 10 * scale );
	const int numberHeight = (int)( 19 * scale );
	const int iconSize = (int)( 14 * scale );
	const int textGap = (int)( 4 * scale );
	const int panelPadX = (int)( 10 * scale );
	const int panelPadY = (int)( 4 * scale );
	const int panelHeight = numberHeight + panelPadY * 2;
	const int panelTop = screenHeight - panelHeight;
	const int contentBottom = screenHeight - panelPadY;

	char healthBuffer[16];
	char armorBuffer[16];
	char moneyBuffer[16];

	const bool showHealthArmor = !hideAll && !( g_hideFlags & HIDEHUD_HEALTH );

	std::snprintf( healthBuffer, sizeof( healthBuffer ), "%d", g_health );
	std::snprintf( armorBuffer, sizeof( armorBuffer ), "%d", g_armor );

	if ( showHealthArmor )
	{
	int red = 238, green = 239, blue = 240;

	if ( g_health <= 25 )
	{
		red = 232;
		green = 104;
		blue = 104;
	}

	const int healthX = pad;
	const int healthTextX = healthX + iconSize + textGap;
	const int healthWidth = CsTextWidth( healthBuffer, numberHeight );
	const int armorX = healthTextX + healthWidth + (int)( 18 * scale );
	const int armorTextX = armorX + iconSize + textGap;
	const int armorWidth = CsTextWidth( armorBuffer, numberHeight );
	const int armorEnd = armorTextX + armorWidth;
	const int panelRight = armorEnd + panelPadX;

	g_eng->pfnFillRGBABlend( 0, panelTop, panelRight, panelHeight, 0, 0, 0, 165 );

	const int iconY = contentBottom - numberHeight + ( numberHeight - iconSize ) / 2;

	CsDrawHealthCross( healthX, iconY, iconSize, red, green, blue, 245 );
	CsDrawText( healthBuffer, healthTextX, contentBottom - numberHeight, numberHeight, red, green, blue, 245 );
	CsDrawArmorShield( armorX, iconY, iconSize, 238, 239, 240, 240 );
	CsDrawText( armorBuffer, armorTextX, contentBottom - numberHeight, numberHeight, 238, 239, 240, 240 );
	}

	const bool showMoney = !hideAll && !( g_hideFlags & HIDEHUD_MONEY );

	std::snprintf( moneyBuffer, sizeof( moneyBuffer ), "%d", g_money );

	if ( showMoney )
	{
	const int moneyTextX = pad + iconSize + textGap;
	const int moneyWidth = CsTextWidth( moneyBuffer, numberHeight );
	const int moneyPanelRight = moneyTextX + moneyWidth + panelPadX;
	const int moneyPanelTop = panelTop - panelHeight - (int)( 6 * scale );
	const int moneyTextY = moneyPanelTop + panelPadY;
	const int moneyIconY = moneyTextY + ( numberHeight - iconSize ) / 2;

	g_eng->pfnFillRGBABlend( 0, moneyPanelTop, moneyPanelRight, panelHeight, 0, 0, 0, 165 );
	CsDrawIcon( s_iconCash, pad, moneyIconY, iconSize, 238, 239, 240, 245 );
	CsDrawText( moneyBuffer, moneyTextX, moneyTextY, numberHeight, 238, 239, 240, 245 );
	}

	if ( !hideAll && !( g_hideFlags & HIDEHUD_WEAPONS ) && ( g_ammoClip >= 0 || g_ammoReserve >= 0 ) )
	{
		char clipBuffer[16] = {};
		char reserveBuffer[16] = {};

		if ( g_ammoClip >= 0 )
			std::snprintf( clipBuffer, sizeof( clipBuffer ), "%d", g_ammoClip );

		if ( g_ammoReserve >= 0 )
			std::snprintf( reserveBuffer, sizeof( reserveBuffer ), "%d", g_ammoReserve );

		const int gap = (int)( 5 * scale );
		const int dividerGap = (int)( 6 * scale );
		const int dividerWidth = std::max( 1, (int)( 1 * scale ) );

		const int clipWidth = clipBuffer[0] ? CsTextWidth( clipBuffer, numberHeight ) : 0;
		const int reserveWidth = reserveBuffer[0] ? CsTextWidth( reserveBuffer, numberHeight ) : 0;

		int totalWidth = 0;

		if ( clipWidth > 0 )
			totalWidth += clipWidth;

		if ( clipWidth > 0 && reserveWidth > 0 )
			totalWidth += dividerWidth + dividerGap * 2;

		if ( reserveWidth > 0 )
			totalWidth += reserveWidth;

		if ( totalWidth > 0 )
		{
			const int panelWidth = totalWidth + iconSize + textGap + gap + panelPadX;
			g_eng->pfnFillRGBABlend( screenWidth - panelWidth, panelTop, panelWidth, panelHeight, 0, 0, 0, 165 );

			int x = screenWidth - panelPadX - iconSize;
			CsDrawIcon( s_iconAmmo, x, contentBottom - numberHeight + ( numberHeight - iconSize ) / 2, iconSize, 238, 239, 240, 245 );

			x -= gap;

			if ( reserveBuffer[0] )
			{
				x -= reserveWidth;
				CsDrawText( reserveBuffer, x, contentBottom - numberHeight, numberHeight, 210, 214, 220, 235 );
				x -= dividerGap;
			}

			if ( clipBuffer[0] && reserveBuffer[0] )
			{
				const int dividerHeight = (int)( 14 * scale );
				const int dividerY = contentBottom - numberHeight + ( numberHeight - dividerHeight ) / 2;

				x -= dividerWidth;
				g_eng->pfnFillRGBABlend( x, dividerY, dividerWidth, dividerHeight, 180, 184, 190, 150 );
				x -= dividerGap;
			}

			if ( clipBuffer[0] )
			{
				x -= clipWidth;
				CsDrawText( clipBuffer, x, contentBottom - numberHeight, numberHeight, 238, 239, 240, 245 );
			}
		}
	}

	if ( !hideAll && !( g_hideFlags & HIDEHUD_TIMER ) )
	{
	const int timerHeight = (int)( 14 * scale );
	const int timerY = (int)( 6 * scale );

	int remaining = -1;

	if ( g_roundTime >= 0 && g_roundStart >= 0.0f )
		remaining = std::max( 0, static_cast<int>( g_roundTime + g_roundStart - g_eng->GetClientTime() ) );

	if ( remaining >= 0 )
	{
		char timerBuffer[16];
		std::snprintf( timerBuffer, sizeof( timerBuffer ), "%02d:%02d", remaining / 60, remaining % 60 );

		const int timerWidth = CsTextWidth( timerBuffer, timerHeight );
		const int timerX = ( screenWidth - timerWidth ) / 2;
		const int timerPadX = (int)( 8 * scale );
		const int timerPadY = (int)( 4 * scale );

		g_eng->pfnFillRGBABlend( timerX - timerPadX, timerY - timerPadY, timerWidth + timerPadX * 2, timerHeight + timerPadY * 2, 0, 0, 0, 165 );
		CsDrawText( timerBuffer, timerX, timerY, timerHeight, 238, 239, 240, 245 );
	}

	int ctPlayers = 0, tPlayers = 0, ctAlive = 0, tAlive = 0;

	for ( int i = 1; i <= SB_MAX_PLAYERS; ++i )
	{
		hud_player_info_t info;

		if ( g_eng->pfnGetPlayerInfo )
			g_eng->pfnGetPlayerInfo( i, &info );
		else
			std::memset( &info, 0, sizeof( info ) );

		if ( !info.name || !info.name[0] )
			continue;

		bool alive = !g_net[i].dead && g_net[i].health != 0;

		if ( i == g_localIdx )
			alive = alive && g_health > 0;

		if ( g_net[i].team == TEAM_CT )
		{
			++ctPlayers;

			if ( alive )
				++ctAlive;
		}
		else if ( g_net[i].team == TEAM_TERRORIST )
		{
			++tPlayers;

			if ( alive )
				++tAlive;
		}
	}

	if ( remaining >= 0 && ( ctPlayers > 0 || tPlayers > 0 ) )
	{
		const int teamIconSize = (int)( 17 * scale );
		const int sideGap = (int)( 12 * scale );
		const int teamGap = (int)( 4 * scale );
		const int scoreHeight = (int)( 13 * scale );
		const int aliveHeight = (int)( 7 * scale );
		const int teamY = timerY + timerHeight + (int)( 10 * scale );
		const int scoreY = teamY + ( teamIconSize - scoreHeight ) / 2;
		const int aliveY = teamY + teamIconSize + (int)( 1 * scale );

		char ctScoreBuffer[16];
		char tScoreBuffer[16];
		char ctAliveBuffer[16];
		char tAliveBuffer[16];

		std::snprintf( ctScoreBuffer, sizeof( ctScoreBuffer ), "%d", g_ctScore );
		std::snprintf( tScoreBuffer, sizeof( tScoreBuffer ), "%d", g_ttScore );
		std::snprintf( ctAliveBuffer, sizeof( ctAliveBuffer ), "%d", ctAlive );
		std::snprintf( tAliveBuffer, sizeof( tAliveBuffer ), "%d", tAlive );

		const int ctScoreWidth = CsTextWidth( ctScoreBuffer, scoreHeight );
		const int tScoreWidth = CsTextWidth( tScoreBuffer, scoreHeight );
		const int ctAliveWidth = CsTextWidth( ctAliveBuffer, aliveHeight );
		const int tAliveWidth = CsTextWidth( tAliveBuffer, aliveHeight );

		const int totalWidth = teamIconSize + teamGap + ctScoreWidth + sideGap + tScoreWidth + teamGap + teamIconSize;
		const int ctIconX = ( screenWidth - totalWidth ) / 2;
		const int ctScoreX = ctIconX + teamIconSize + teamGap;
		const int tIconX = screenWidth - ctIconX - teamIconSize;
		const int tScoreX = tIconX - teamGap - tScoreWidth;

		CsDrawIcon( s_iconTeamCT, ctIconX, teamY, teamIconSize, 255, 255, 255, 255 );
		CsDrawIcon( s_iconTeamT, tIconX, teamY, teamIconSize, 255, 255, 255, 255 );
		CsDrawText( ctScoreBuffer, ctScoreX, scoreY, scoreHeight, 131, 165, 222, 240 );
		CsDrawText( tScoreBuffer, tScoreX, scoreY, scoreHeight, 232, 197, 111, 240 );
		CsDrawText( ctAliveBuffer, ctIconX + ( teamIconSize - ctAliveWidth ) / 2, aliveY, aliveHeight, 220, 220, 220, 200 );
		CsDrawText( tAliveBuffer, tIconX + ( teamIconSize - tAliveWidth ) / 2, aliveY, aliveHeight, 220, 220, 220, 200 );
	}
	}
}

void OnNetMsg( void *args, void *user_data )
{
	const auto *msg = static_cast<const nexora_netmsg_args_t *>( args );

	if ( !msg || !msg->name || !msg->buf )
		return;

	BufferReader reader( msg->name, const_cast<void *>( msg->buf ), msg->size );

	if ( std::strcmp( msg->name, "Health" ) == 0 )
	{
		g_health = reader.ReadByte();
		g_localIdx = FindLocalIdx();
		return;
	}

	if ( std::strcmp( msg->name, "Battery" ) == 0 )
	{
		g_armor = reader.ReadShort();
		return;
	}

	if ( std::strcmp( msg->name, "Account" ) == 0 )
	{
		const int idx = reader.ReadByte();
		const int account = reader.ReadLong();

		if ( idx == FindLocalIdx() )
			g_money = account;

		return;
	}

	if ( std::strcmp( msg->name, "HealthInfo" ) == 0 )
	{
		const int idx = reader.ReadByte();
		const int health = reader.ReadLong();

		if ( idx >= 1 && idx <= SB_MAX_PLAYERS )
			g_net[idx].health = health;

		return;
	}

	if ( std::strcmp( msg->name, "ScoreAttrib" ) == 0 )
	{
		const int index = reader.ReadByte();
		const unsigned char flags = static_cast<unsigned char>( reader.ReadByte() );

		if ( index >= 1 && index <= SB_MAX_PLAYERS )
			g_net[index].dead = ( flags & PLAYER_DEAD ) != 0;

		return;
	}

	if ( std::strcmp( msg->name, "TeamInfo" ) == 0 )
	{
		const int cl = reader.ReadByte();
		const char *teamName = reader.ReadString();

		if ( cl >= 1 && cl <= SB_MAX_PLAYERS )
		{
			int teamNumber = TEAM_UNASSIGNED;

			if ( teamName )
			{
				if ( CsStricmp( teamName, "TERRORIST" ) == 0 )
					teamNumber = TEAM_TERRORIST;
				else if ( CsStricmp( teamName, "CT" ) == 0 )
					teamNumber = TEAM_CT;
				else if ( CsStricmp( teamName, "SPECTATOR" ) == 0 )
					teamNumber = TEAM_SPECTATOR;
			}

			g_net[cl].team = teamNumber;
		}

		return;
	}

	if ( std::strcmp( msg->name, "TeamScore" ) == 0 )
	{
		const char *name = reader.ReadString();
		const int frags = reader.ReadShort();

		reader.ReadShort();

		if ( !name )
			return;

		if ( CsStricmp( name, "CT" ) == 0 )
			g_ctScore = frags;
		else if ( CsStricmp( name, "TERRORIST" ) == 0 )
			g_ttScore = frags;

		return;
	}

	if ( std::strcmp( msg->name, "RoundTime" ) == 0 )
	{
		g_roundTime = reader.ReadShort();
		g_roundStart = g_eng->GetClientTime();
		return;
	}
}

void OnAmmoInfo( void *args, void *user_data )
{
	const auto *ammo = static_cast<const nexora_ammo_args_t *>( args );

	if ( !ammo )
		return;

	g_ammoClip = ammo->clip;
	g_ammoReserve = ammo->reserve;

	const char *name = ammo->weapon_name ? ammo->weapon_name : "";

	if ( std::strlen( name ) >= sizeof( g_weaponName ) )
		name = "";

	std::strcpy( g_weaponName, name );
}

void OnScoreboardToggle( void *args, void *user_data )
{
	const auto *toggle = static_cast<const nexora_scoreboard_args_t *>( args );

	if ( !toggle )
		return;

	g_scoreboardVisible = toggle->visible != 0;
}

void OnHudElementDraw( void *args, void *user_data )
{
	auto *element = static_cast<nexora_hud_element_args_t *>( args );

	if ( !element || !element->name )
		return;

	if ( !hud_style || hud_style->value < 0.5f )
		return;

	static const char *const kVetoed[] =
	{
		"health",
		"battery",
		"money",
		"timer",
		"ammo",
		"ammosecondary",
	};

	for ( const char *name : kVetoed )
	{
		if ( std::strcmp( element->name, name ) == 0 )
		{
			element->allow = 0;
			return;
		}
	}
}

void OnPaintHud( void *args, void *user_data )
{
	if ( !hud_style || hud_style->value < 0.5f )
		return;

	if ( g_scoreboardVisible )
		return;

	if ( g_eng->IsSpectateOnly && g_eng->IsSpectateOnly() )
		return;

	CsDrawHud();
}

bool RegisterCVars()
{
	hud_style = g_eng->pfnRegisterVariable( "hud_style", "0", FCVAR_ARCHIVE );
	hud_csgo_scale = g_eng->pfnRegisterVariable( "hud_csgo_scale", "1", FCVAR_ARCHIVE );

	return hud_style && hud_csgo_scale;
}

void OnUpdateClientData( void *args, void *user_data )
{
	const auto *update = static_cast<const nexora_update_client_data_args_t *>( args );

	if ( !update )
		return;

	g_hideFlags = update->hide_hud;
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
	g_render = static_cast<render_api_t *>( api->render_api );

	if ( !g_eng )
		return 0;

	std::memset( g_net, 0, sizeof( g_net ) );

	if ( !RegisterCVars() )
	{
		api->con_printf( "[csgohud] failed to register cvars\n" );
		return 0;
	}

	api->con_printf( "[csgohud] init\n" );

	api->register_hook( NEXORA_HOOK_NETMSG, OnNetMsg, nullptr );
	api->register_hook( NEXORA_HOOK_AMMO_INFO, OnAmmoInfo, nullptr );
	api->register_hook( NEXORA_HOOK_UPDATE_CLIENT_DATA, OnUpdateClientData, nullptr );
	api->register_hook( NEXORA_HOOK_SCOREBOARD_TOGGLE, OnScoreboardToggle, nullptr );
	api->register_hook( NEXORA_HOOK_HUD_ELEMENT_DRAW, OnHudElementDraw, nullptr );
	api->register_hook( NEXORA_HOOK_PAINT_HUD, OnPaintHud, nullptr );

	return 1;
}

