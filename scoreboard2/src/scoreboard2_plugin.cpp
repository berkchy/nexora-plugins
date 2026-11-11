//
// scoreboard2 plugin — CS:GO-style team scoreboard for the Nexora client.
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
#include <cctype>
#include <string>
#include <algorithm>

namespace
{

const int SB_MAX_PLAYERS = 32;
const int SB_CONTAINER = SB_MAX_PLAYERS + 1;
const int SB_OTHER_TEAM = -1;

const nexora_plugin_info_t s_info =
{
	"scoreboard2",
	"1.0.0",
	NEXORA_PLUGIN_API_VERSION,
	45,
};

const nexora_plugin_api_t *g_api = nullptr;
cl_enginefunc_t *g_eng = nullptr;
render_api_t *g_render = nullptr;

cvar_t *cl_scoreboard_style = nullptr;
cvar_t *cl_scoreboard_alpha = nullptr;
cvar_t *cl_scoreboard_show_money = nullptr;
cvar_t *cl_scoreboard_show_hp = nullptr;
cvar_t *cl_scoreboard_show_ping = nullptr;
cvar_t *cl_scoreboard_show_flags = nullptr;
cvar_t *cl_scoreboard_high_contrast = nullptr;
cvar_t *cl_scoreboard_fadein = nullptr;
cvar_t *cl_scoreboard_fadeout = nullptr;
cvar_t *cl_showplayerversion = nullptr;

struct SbNetState
{
	int frags = 0;
	int deaths = 0;
	int health = -1;
	int money = -1;
	int team = 0;
	int playerclass = 0;
	bool valid = false;
	bool dead = false;
	bool hasC4 = false;
	bool vip = false;
	bool defuse = false;
	char teamname[64] = {};
};

SbNetState g_net[SB_CONTAINER];

char g_serverName[64] = {};
int g_roundTime = -1;
float g_roundStart = -1.0f;

bool g_visible = false;
float g_fade = 0.0f;
float g_lastPaintTime = -1.0f;

struct SbConfig
{
	bool showMoney;
	bool showHp;
	bool showPing;
	bool showFlags;
	bool showVersion;
	bool highContrast;
	int alpha;
};

struct SbPlayer
{
	int index;
	char name[64];
	int frags;
	int deaths;
	int ping;
	int health;
	int money;
	int team;
	bool dead;
	bool bot;
	bool hasC4;
	bool vip;
	bool defuse;
	bool self;
};

struct SbTeam
{
	int number;
	int players;
	int alive;
	int frags;
	int pingSum;
};

struct SbColumns
{
	int nameX;
	int nameMax;
	int pingLeft;
	int pingRight;
	int killLeft;
	int killRight;
	int deathLeft;
	int deathRight;
	int hpLeft;
	int hpRight;
	int moneyLeft;
	int moneyRight;
	bool showPing;
	bool showKills;
	bool showDeaths;
	bool showHp;
	bool showMoney;
};

struct SbIcon
{
	const char *path;
	int texture;
	bool attempted;
};

SbIcon s_iconShield = { "gfx/scoreboard/shield.png", 0, false };
SbIcon s_iconBomb = { "gfx/scoreboard/unlit-bomb.png", 0, false };
SbIcon s_iconKit = { "gfx/scoreboard/toolbox.png", 0, false };
SbIcon s_iconCash = { "gfx/scoreboard/dollar.png", 0, false };
SbIcon s_iconSkull = { "gfx/scoreboard/skull-crossed-bones.png", 0, false };
SbIcon s_iconStar = { "gfx/scoreboard/star.png", 0, false };
SbIcon s_iconStopwatch = { "gfx/scoreboard/stopwatch.png", 0, false };
SbIcon s_iconTarget = { "gfx/scoreboard/kill_gun.png", 0, false };

SbIcon s_iconTimerDigits[10] =
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
SbIcon s_iconTimerColon = { "gfx/scoreboard/timer_colon.png", 0, false };
SbIcon s_iconTimerDash = { "gfx/scoreboard/timer_dash.png", 0, false };

int SbClamp( int value, int low, int high )
{
	if ( value < low )
		return low;
	if ( value > high )
		return high;
	return value;
}

int SbFade( int value )
{
	float fade = g_fade;
	fade = std::max( 0.0f, std::min( 1.0f, fade ) );
	return static_cast<int>( value * fade + 0.5f );
}

void SbDrawQuad( float x1, float y1, float x2, float y2 )
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

void SbDrawIconRect( SbIcon &icon, int x, int y, int width, int height, int r, int g, int b, int a )
{
	if ( width <= 0 || height <= 0 || g_fade <= 0.001f )
		return;

	if ( !g_render )
		return;

	if ( !icon.texture && !icon.attempted )
	{
		icon.attempted = true;
		icon.texture = g_render->GL_LoadTexture( icon.path, nullptr, 0, TF_NEAREST | TF_NOMIPMAP | TF_CLAMP );
	}

	if ( !icon.texture )
		return;

	g_render->GL_SelectTexture( 0 );
	g_render->GL_Bind( 0, icon.texture );
	g_eng->pTriAPI->RenderMode( kRenderTransAlpha );
	g_eng->pTriAPI->Color4f( r / 255.0f, g / 255.0f, b / 255.0f, SbFade( a ) / 255.0f );

	SbDrawQuad( (float)x, (float)y, (float)( x + width ), (float)( y + height ) );
}

void SbDrawIcon( SbIcon &icon, int x, int y, int size, int r, int g, int b, int a )
{
	SbDrawIconRect( icon, x, y, size, size, r, g, b, a );
}

int SbTimerGlyphWidth( char c )
{
	if ( c >= '0' && c <= '9' )
		return 18;
	if ( c == ':' )
		return 8;
	if ( c == '-' )
		return 12;
	return 0;
}

SbIcon *SbTimerGlyph( char c )
{
	if ( c >= '0' && c <= '9' )
		return &s_iconTimerDigits[c - '0'];
	if ( c == ':' )
		return &s_iconTimerColon;
	if ( c == '-' )
		return &s_iconTimerDash;

	return nullptr;
}

void SbDrawTimer( const char *text, int right, int top, int height, int r, int g, int b, int a )
{
	if ( !text || g_fade <= 0.001f )
		return;

	const int glyphHeight = 26;
	const int gap = 3;
	int total = 0;

	for ( const char *p = text; *p; ++p )
	{
		const int glyphWidth = SbTimerGlyphWidth( *p );

		if ( glyphWidth <= 0 )
			continue;

		total += height * glyphWidth / glyphHeight;

		if ( p[1] )
			total += gap;
	}

	int x = right - total;

	for ( const char *p = text; *p; ++p )
	{
		SbIcon *icon = SbTimerGlyph( *p );
		const int glyphWidth = SbTimerGlyphWidth( *p );

		if ( !icon || glyphWidth <= 0 )
			continue;

		const int width = height * glyphWidth / glyphHeight;
		SbDrawIconRect( *icon, x, top, width, height, r, g, b, a );
		x += width + gap;
	}
}

int SbTextWidth( const char *text )
{
	if ( !text || !text[0] )
		return 0;

	int width = 0;
	int height = 0;
	g_eng->pfnDrawConsoleStringLen( text, &width, &height );
	return width;
}

int SbTextHeight()
{
	int width = 0;
	int height = 0;
	g_eng->pfnDrawConsoleStringLen( "Ag", &width, &height );
	return height > 0 ? height : 12;
}

void SbColor( int r, int g, int b )
{
	g_eng->pfnDrawSetTextColor( SbFade( r ) / 255.0f, SbFade( g ) / 255.0f, SbFade( b ) / 255.0f );
}

void SbText( int x, int y, const char *text )
{
	if ( g_fade <= 0.001f )
		return;

	if ( text && text[0] )
		g_eng->pfnDrawConsoleString( x, y, const_cast<char *>( text ) );
}

void SbTextRight( int right, int y, const char *text )
{
	SbText( right - SbTextWidth( text ), y, text );
}

void SbTextCenter( int center, int y, const char *text )
{
	SbText( center - SbTextWidth( text ) / 2, y, text );
}

void SbHeaderIcon( SbIcon &icon, int right, int y, const char *label, int size, int r, int g, int b )
{
	const int labelWidth = ( label && label[0] ) ? SbTextWidth( label ) : 0;
	const int gap = labelWidth > 0 ? 3 : 0;
	const int total = size + gap + labelWidth;
	const int iconX = right - total;
	const int iconY = y + ( SbTextHeight() - size ) / 2;

	SbDrawIcon( icon, iconX, iconY, size, r, g, b, 200 );

	if ( labelWidth > 0 )
	{
		SbColor( r, g, b );
		SbText( iconX + size + gap, y, label );
	}
}

void SbTextClipped( int x, int y, int maxWidth, const char *text )
{
	if ( !text || !text[0] || maxWidth <= 0 )
		return;

	char buffer[256];
	int length = 0;

	for ( const char *p = text; *p && length < static_cast<int>( sizeof( buffer ) ) - 1; ++p )
	{
		buffer[length++] = *p;
		buffer[length] = 0;

		if ( SbTextWidth( buffer ) > maxWidth )
		{
			buffer[length - 1] = 0;
			break;
		}
	}

	SbText( x, y, buffer );
}

void SbFill( int x, int y, int width, int height, int r, int g, int b, int a )
{
	if ( width > 0 && height > 0 )
		g_eng->pfnFillRGBABlend( x, y, width, height, r, g, b, SbFade( a ) );
}

void SbTeamColor( int team, int &r, int &g, int &b )
{
	switch ( team )
	{
	case TEAM_CT:
		r = 79; g = 156; b = 240;
		break;
	case TEAM_TERRORIST:
		r = 224; g = 147; b = 74;
		break;
	default:
		r = 150; g = 156; b = 164;
		break;
	}
}

void SbPlayerColor( const SbPlayer &player, int &r, int &g, int &b )
{
	if ( player.dead )
	{
		r = 132; g = 136; b = 142;
		return;
	}

	SbTeamColor( player.team, r, g, b );
}

void SbPingColor( int ping, bool bot, int &r, int &g, int &b )
{
	if ( bot )
	{
		r = 150; g = 156; b = 164;
	}
	else if ( ping < 60 )
	{
		r = 123; g = 224; b = 163;
	}
	else if ( ping < 120 )
	{
		r = 240; g = 196; b = 92;
	}
	else
	{
		r = 235; g = 108; b = 108;
	}
}

void SbHealthColor( int health, int &r, int &g, int &b )
{
	if ( health <= 25 )
	{
		r = 216; g = 124; b = 124;
	}
	else
	{
		r = 226; g = 228; b = 230;
	}
}

void SbRankColor( int rank, bool topThree, int &r, int &g, int &b )
{
	if ( topThree && rank == 1 )
	{
		r = 236; g = 236; b = 232;
	}
	else if ( topThree && rank == 2 )
	{
		r = 198; g = 202; b = 208;
	}
	else if ( topThree && rank == 3 )
	{
		r = 174; g = 168; b = 158;
	}
	else
	{
		r = 132; g = 136; b = 142;
	}
}

bool SbPlayerBefore( const SbPlayer &a, const SbPlayer &b )
{
	if ( a.frags != b.frags )
		return a.frags > b.frags;

	if ( a.deaths != b.deaths )
		return a.deaths < b.deaths;

	return std::strcmp( a.name, b.name ) < 0;
}

void SbSortPlayers( SbPlayer *players, int count )
{
	for ( int i = 1; i < count; ++i )
	{
		SbPlayer key = players[i];
		int j = i - 1;

		while ( j >= 0 && SbPlayerBefore( key, players[j] ) )
		{
			players[j + 1] = players[j];
			--j;
		}

		players[j + 1] = key;
	}
}

int SbCollectPlayers( SbPlayer *players, int maxPlayers )
{
	int count = 0;

	for ( int i = 1; i <= SB_MAX_PLAYERS && count < maxPlayers; ++i )
	{
		hud_player_info_t info;
		g_eng->pfnGetPlayerInfo( i, &info );

		if ( !info.name || !info.name[0] )
			continue;

		const SbNetState &net = g_net[i];
		SbPlayer &player = players[count++];

		player.index = i;
		std::strncpy( player.name, info.name, sizeof( player.name ) - 1 );
		player.name[sizeof( player.name ) - 1] = '\0';

		player.frags = net.frags;
		player.deaths = net.deaths;
		player.ping = info.ping;
		player.health = net.health;
		player.money = net.money;
		player.dead = net.dead;
		player.hasC4 = net.hasC4;
		player.vip = net.vip;
		player.defuse = net.defuse;
		player.self = info.thisplayer != 0;
		player.team = net.team;

		const char *bot = g_eng->PlayerInfo_ValueForKey( i, "*bot" );
		player.bot = player.ping <= 5 && bot && std::atoi( bot ) > 0;
	}

	SbSortPlayers( players, count );
	return count;
}

void SbBuildColumns( SbColumns &columns, int x, int width, const SbConfig &config )
{
	columns.showPing = config.showPing && width >= 190;
	columns.showKills = width >= 120;
	columns.showDeaths = width >= 160;
	columns.showHp = config.showHp && width >= 250;
	columns.showMoney = config.showMoney && width >= 280;

	const int gap = 8;
	const int pingWidth = 46;
	const int deathWidth = 38;
	const int killWidth = 38;
	const int hpWidth = 30;
	const int moneyWidth = 74;

	int right = x + width - 2;

	columns.pingRight = right;
	columns.pingLeft = right - pingWidth;
	if ( columns.showPing )
		right -= pingWidth + gap;

	columns.deathRight = right;
	columns.deathLeft = right - deathWidth;
	if ( columns.showDeaths )
		right -= deathWidth + gap;

	columns.killRight = right;
	columns.killLeft = right - killWidth;
	if ( columns.showKills )
		right -= killWidth + gap;

	columns.hpRight = right;
	columns.hpLeft = right - hpWidth;
	if ( columns.showHp )
		right -= hpWidth + gap;

	columns.moneyRight = right;
	columns.moneyLeft = right - moneyWidth;
	if ( columns.showMoney )
		right -= moneyWidth + gap;

	columns.nameX = x + 6;
	columns.nameMax = right - columns.nameX - 4;
	if ( columns.nameMax < 40 )
		columns.nameMax = 40;
}

void SbDrawColumnHeaders( const SbColumns &columns, int y, int width, int r, int g, int b )
{
	SbColor( r, g, b );
	SbText( columns.nameX, y, "PLAYER" );

	int iconSize = SbTextHeight() - 1;
	if ( iconSize < 10 )
		iconSize = 10;

	if ( columns.showMoney )
		SbHeaderIcon( s_iconCash, columns.moneyRight - 4, y, "MONEY", iconSize, r, g, b );
	if ( columns.showKills )
		SbHeaderIcon( s_iconTarget, columns.killRight - 4, y, "K", iconSize, r, g, b );
	if ( columns.showDeaths )
		SbHeaderIcon( s_iconSkull, columns.deathRight - 4, y, "D", iconSize, r, g, b );
	if ( columns.showHp )
		SbTextRight( columns.hpRight - 4, y, "HP" );
	if ( columns.showPing )
		SbHeaderIcon( s_iconStopwatch, columns.pingRight - 4, y, "PING", iconSize, r, g, b );

	g_eng->pfnFillRGBABlend( columns.nameX - 2, y + SbTextHeight() + 2, width, 1, 255, 255, 255, SbFade( 18 ) );
}

int SbDrawPlayerRows(
	int x,
	int y,
	int width,
	int maxRows,
	int rowHeight,
	const SbPlayer *players,
	int count,
	int teamFilter,
	bool showTopRanks,
	const SbConfig &config )
{
	int drawn = 0;
	int hidden = 0;
	const int textHeight = SbTextHeight();
	SbColumns columns;

	SbBuildColumns( columns, x, width, config );

	int nameCellRight = x + width;
	if ( columns.showPing )
		nameCellRight = std::min( nameCellRight, columns.pingLeft - 2 );
	if ( columns.showDeaths )
		nameCellRight = std::min( nameCellRight, columns.deathLeft - 2 );
	if ( columns.showKills )
		nameCellRight = std::min( nameCellRight, columns.killLeft - 2 );
	if ( columns.showHp )
		nameCellRight = std::min( nameCellRight, columns.hpLeft - 2 );
	if ( columns.showMoney )
		nameCellRight = std::min( nameCellRight, columns.moneyLeft - 2 );

	for ( int i = 0; i < count; ++i )
	{
		const SbPlayer &player = players[i];

		if ( teamFilter == SB_OTHER_TEAM )
		{
			if ( player.team == TEAM_CT || player.team == TEAM_TERRORIST )
				continue;
		}
		else if ( teamFilter != 0 && player.team != teamFilter )
		{
			continue;
		}

		if ( drawn >= maxRows )
		{
			++hidden;
			continue;
		}

		const int rowY = y + drawn * rowHeight;
		const int cellHeight = rowHeight - 1;
		const int textY = rowY + ( cellHeight - textHeight ) / 2;

		SbFill( x, rowY, nameCellRight - x, cellHeight, 255, 255, 255, 6 );
		if ( columns.showMoney )
			SbFill( columns.moneyLeft, rowY, columns.moneyRight - columns.moneyLeft, cellHeight, 255, 255, 255, 5 );
		if ( columns.showKills )
			SbFill( columns.killLeft, rowY, columns.killRight - columns.killLeft, cellHeight, 255, 255, 255, 5 );
		if ( columns.showDeaths )
			SbFill( columns.deathLeft, rowY, columns.deathRight - columns.deathLeft, cellHeight, 255, 255, 255, 5 );
		if ( columns.showHp )
			SbFill( columns.hpLeft, rowY, columns.hpRight - columns.hpLeft, cellHeight, 255, 255, 255, 5 );
		if ( columns.showPing )
			SbFill( columns.pingLeft, rowY, columns.pingRight - columns.pingLeft, cellHeight, 255, 255, 255, 5 );

		if ( player.self )
		{
			int tr, tg, tb;
			SbTeamColor( player.team, tr, tg, tb );
			g_eng->pfnFillRGBABlend( x, rowY, width, cellHeight, tr, tg, tb, SbFade( 18 ) );
		}

		int r = 232, g = 234, b = 236;
		SbPlayerColor( player, r, g, b );

		if ( config.highContrast )
		{
			r = player.dead ? 150 : 250;
			g = player.dead ? 152 : 252;
			b = player.dead ? 156 : 255;
		}

		int nameX = columns.nameX;
		int nameMax = columns.nameMax;
		int iconSize = textHeight - 2;
		if ( iconSize < 8 )
			iconSize = 8;

		const int iconY = rowY + ( cellHeight - iconSize ) / 2;

		if ( drawn == 0 && nameMax > iconSize * 2 + 40 )
		{
			SbDrawIcon( s_iconStar, nameX, iconY, iconSize, 224, 196, 120, 235 );
			nameX += iconSize + 3;
			nameMax -= iconSize + 3;
		}

		if ( player.dead )
		{
			SbDrawIcon( s_iconSkull, nameX, iconY, iconSize, 176, 178, 184, 235 );
			nameX += iconSize + 3;
			nameMax -= iconSize + 3;
		}
		else
		{
			if ( player.hasC4 )
			{
				SbDrawIcon( s_iconBomb, nameX, iconY, iconSize, 224, 196, 132, 235 );
				nameX += iconSize + 3;
				nameMax -= iconSize + 3;
			}
			if ( player.defuse )
			{
				SbDrawIcon( s_iconKit, nameX, iconY, iconSize, 176, 214, 194, 235 );
				nameX += iconSize + 3;
				nameMax -= iconSize + 3;
			}
		}

		SbColor( r, g, b );

		if ( nameMax > 20 )
			SbTextClipped( nameX, textY, nameMax, player.name );

		if ( columns.showKills )
		{
			char buffer[16];
			std::snprintf( buffer, sizeof( buffer ), "%d", player.frags );
			SbColor( player.dead ? 140 : 238, player.dead ? 142 : 242, player.dead ? 146 : 248 );
			SbTextRight( columns.killRight - 4, textY, buffer );
		}

		if ( columns.showDeaths )
		{
			char buffer[16];
			std::snprintf( buffer, sizeof( buffer ), "%d", player.deaths );
			SbColor( 174, 178, 184 );
			SbTextRight( columns.deathRight - 4, textY, buffer );
		}

		if ( columns.showHp && !player.dead && player.health >= 0 )
		{
			char buffer[16];
			int hr, hg, hb;
			std::snprintf( buffer, sizeof( buffer ), "%d", player.health );
			SbHealthColor( player.health, hr, hg, hb );
			SbColor( hr, hg, hb );
			SbTextRight( columns.hpRight - 4, textY, buffer );
		}
		if ( columns.showMoney && player.money >= 0 )
		{
			char buffer[24];
			std::snprintf( buffer, sizeof( buffer ), "$%d", player.money );
			SbColor( 206, 208, 204 );
			SbTextRight( columns.moneyRight - 4, textY, buffer );
		}

		if ( columns.showPing )
		{
			char buffer[16];

			if ( player.bot )
			{
				std::snprintf( buffer, sizeof( buffer ), "BOT" );
				SbColor( 146, 150, 156 );
			}
			else
			{
				std::snprintf( buffer, sizeof( buffer ), "%d", player.ping );
				int pr, pg, pb;
				SbPingColor( player.ping, false, pr, pg, pb );
				SbColor( pr, pg, pb );
			}

			SbTextRight( columns.pingRight - 4, textY, buffer );
		}

		++drawn;
	}

	if ( hidden > 0 )
	{
		char buffer[48];
		std::snprintf( buffer, sizeof( buffer ), "+%d more", hidden );
		SbColor( 145, 150, 158 );
		SbText( x + 6, y + drawn * rowHeight + 4, buffer );
	}

	return drawn;
}

void SbDrawTeam(
	const SbTeam &team,
	int x,
	int y,
	int width,
	int height,
	int rowHeight,
	const SbPlayer *players,
	int count,
	const SbConfig &config )
{
	if ( width <= 0 || height <= 0 )
		return;

	int r, g, b;
	SbTeamColor( team.number, r, g, b );

	const int infoWidth = width < 460 ? 62 : 88;
	const char *name = team.number == TEAM_CT ? "COUNTER-TERRORISTS" : "TERRORISTS";

	int logoSize = infoWidth - 12;
	if ( logoSize > 64 )
		logoSize = 64;

	SbDrawIcon( s_iconShield, x + ( infoWidth - logoSize ) / 2, y + ( height - logoSize ) / 2, logoSize, r, g, b, 36 );

	SbColor( r, g, b );
	SbTextClipped( x + 4, y + 2, infoWidth - 8, name );

	char scoreBuffer[24];
	std::snprintf( scoreBuffer, sizeof( scoreBuffer ), "%d", team.frags );
	SbColor( 238, 239, 240 );
	const int scoreWidth = SbTextWidth( scoreBuffer );
	const int scoreTop = y + SbTextHeight() + 6;
	int scoreY = scoreTop + ( ( y + height ) - scoreTop - SbTextHeight() ) / 2;
	if ( scoreY < scoreTop )
		scoreY = scoreTop;
	SbText( x + ( infoWidth - scoreWidth ) / 2, scoreY, scoreBuffer );

	SbDrawPlayerRows(
		x + infoWidth,
		y + 4,
		width - infoWidth,
		team.players > 0 ? team.players : 1,
		rowHeight,
		players,
		count,
		team.number,
		false,
		config );
}

void SbDrawSpectators(
	const SbPlayer *players,
	int count,
	int x,
	int y,
	int width,
	const SbConfig &config )
{
	char buffer[512];
	buffer[0] = 0;
	int length = 0;

	for ( int i = 0; i < count; ++i )
	{
		const SbPlayer &player = players[i];

		if ( player.team == TEAM_CT || player.team == TEAM_TERRORIST )
			continue;

		if ( length > 0 && length < static_cast<int>( sizeof( buffer ) ) - 3 )
		{
			buffer[length++] = ',';
			buffer[length++] = ' ';
			buffer[length] = '\0';
		}

		const int nameLength = static_cast<int>( std::strlen( player.name ) );

		if ( length + nameLength >= static_cast<int>( sizeof( buffer ) ) - 1 )
			break;

		std::strncpy( buffer + length, player.name, sizeof( buffer ) - length - 1 );
		length += nameLength;
		buffer[length] = '\0';
	}

	if ( length == 0 )
		return;

	SbColor( 145, 150, 158 );
	SbText( x, y, "SPECTATORS:" );
	SbColor( 205, 210, 216 );
	SbTextClipped( x + SbTextWidth( "SPECTATORS:" ) + 6, y, width - SbTextWidth( "SPECTATORS:" ) - 6, buffer );
}

void SbDrawScoreboard()
{
	SbPlayer players[SB_CONTAINER];
	const int playerCount = SbCollectPlayers( players, SB_CONTAINER );

	SbConfig config;
	config.showMoney = cl_scoreboard_show_money && cl_scoreboard_show_money->value != 0.0f;
	config.showHp = cl_scoreboard_show_hp && cl_scoreboard_show_hp->value != 0.0f;
	config.showPing = cl_scoreboard_show_ping && cl_scoreboard_show_ping->value != 0.0f;
	config.showFlags = cl_scoreboard_show_flags && cl_scoreboard_show_flags->value != 0.0f;
	config.showVersion = cl_showplayerversion && cl_showplayerversion->value != 0.0f;
	config.highContrast = cl_scoreboard_high_contrast && cl_scoreboard_high_contrast->value != 0.0f;
	config.alpha = cl_scoreboard_alpha ? SbClamp( static_cast<int>( cl_scoreboard_alpha->value ), 170, 235 ) : 220;

	int screenWidth = 640;
	int screenHeight = 480;

	if ( g_api && g_api->get_screen_size )
		g_api->get_screen_size( &screenWidth, &screenHeight );

	SbTeam ctTeam;
	SbTeam tTeam;
	std::memset( &ctTeam, 0, sizeof( ctTeam ) );
	std::memset( &tTeam, 0, sizeof( tTeam ) );
	ctTeam.number = TEAM_CT;
	tTeam.number = TEAM_TERRORIST;

	int otherPlayers = 0;

	for ( int i = 0; i < playerCount; ++i )
	{
		const SbPlayer &player = players[i];

		if ( player.team == TEAM_CT )
		{
			++ctTeam.players;

			if ( !player.dead )
				++ctTeam.alive;

			ctTeam.frags += player.frags;
			ctTeam.pingSum += player.ping;
		}
		else if ( player.team == TEAM_TERRORIST )
		{
			++tTeam.players;

			if ( !player.dead )
				++tTeam.alive;

			tTeam.frags += player.frags;
			tTeam.pingSum += player.ping;
		}
		else
		{
			++otherPlayers;
		}
	}

	const bool teamMode = ctTeam.players > 0 || tTeam.players > 0;

	const int headerHeight = 52;
	const int labelHeight = SbTextHeight() + 6;
	const int footerHeight = 20;
	const int padding = 14;
	const int teamGap = 8;
	const int spectatorHeight = otherPlayers > 0 ? SbTextHeight() + 4 : 0;

	int maxPanelHeight = screenHeight - 32;
	if ( maxPanelHeight < 180 )
		maxPanelHeight = screenHeight - 8;

	int rowHeight;
	int ctHeight = 0;
	int tHeight = 0;

	if ( teamMode )
	{
		const int rowCount = ( ctTeam.players > 0 ? ctTeam.players : 1 ) + ( tTeam.players > 0 ? tTeam.players : 1 );
		const int fixedHeight = headerHeight + labelHeight + footerHeight + padding * 2 + teamGap + spectatorHeight + 16;
		rowHeight = SbClamp( ( maxPanelHeight - fixedHeight ) / ( rowCount > 0 ? rowCount : 1 ), 14, 26 );
		ctHeight = ( ctTeam.players > 0 ? ctTeam.players : 1 ) * rowHeight + 10;
		tHeight = ( tTeam.players > 0 ? tTeam.players : 1 ) * rowHeight + 10;

		const int infoMinHeight = SbTextHeight() + 6 + static_cast<int>( SbTextHeight() * 1.35f ) + 8;
		if ( ctHeight < infoMinHeight )
			ctHeight = infoMinHeight;

		if ( tHeight < infoMinHeight )
			tHeight = infoMinHeight;
	}
	else
	{
		rowHeight = SbClamp( ( maxPanelHeight - headerHeight - labelHeight - footerHeight - padding * 2 ) / ( playerCount > 0 ? playerCount : 1 ), 15, 24 );
	}

	int panelWidth = static_cast<int>( screenWidth * 0.94f );
	if ( panelWidth > 1000 )
		panelWidth = 1000;
	if ( panelWidth < 280 )
		panelWidth = screenWidth - 16;

	int panelHeight = maxPanelHeight;

	if ( teamMode )
	{
		int minTeamPanelHeight = static_cast<int>( screenHeight * 0.72f );

		if ( minTeamPanelHeight > maxPanelHeight )
			minTeamPanelHeight = maxPanelHeight;

		const int contentHeight = headerHeight + labelHeight + spectatorHeight + padding * 2 + ctHeight + tHeight + teamGap + footerHeight;

		if ( contentHeight > panelHeight )
			panelHeight = std::min( contentHeight, maxPanelHeight );
		else
			panelHeight = std::max( contentHeight, minTeamPanelHeight );
	}

	const int panelX = ( screenWidth - panelWidth ) / 2;
	const int panelY = ( screenHeight - panelHeight ) / 2;

	const int panelRight = panelX + panelWidth;
	const int panelBottom = panelY + panelHeight;

	SbFill( 0, 0, screenWidth, screenHeight, 0, 0, 0, config.highContrast ? 170 : 110 );
	SbFill( panelX, panelY, panelWidth, panelHeight, 0, 0, 0, config.alpha );
	SbFill( panelX, panelY, panelWidth, 1, 255, 255, 255, 16 );
	SbFill( panelX, panelBottom - 1, panelWidth, 1, 0, 0, 0, 140 );

	const int mutedR = config.highContrast ? 210 : 150;
	const int mutedG = config.highContrast ? 212 : 154;
	const int mutedB = config.highContrast ? 214 : 160;

	const char *serverName = g_serverName[0] ? g_serverName : "NEXORA";
	SbColor( 238, 239, 240 );
	SbTextClipped( panelX + padding, panelY + 9, panelWidth - 130, serverName );

	int remaining = -1;
	if ( g_roundTime >= 0 && g_roundStart >= 0.0f )
	{
		remaining = std::max( 0, static_cast<int>( g_roundTime + g_roundStart - g_eng->GetClientTime() ) );
	}

	char timerBuffer[16];
	if ( remaining >= 0 )
		std::snprintf( timerBuffer, sizeof( timerBuffer ), "%02d:%02d", remaining / 60, remaining % 60 );
	else
		std::snprintf( timerBuffer, sizeof( timerBuffer ), "--:--" );

	SbDrawTimer( timerBuffer, panelRight - padding, panelY + 2, 26, 255, 206, 74, 255 );

	char countBuffer[48];
	std::snprintf( countBuffer, sizeof( countBuffer ), "%d PLAYERS", playerCount );
	SbColor( mutedR, mutedG, mutedB );
	SbTextRight( panelRight - padding, panelY + 31, countBuffer );

	SbFill( panelX + padding, panelY + 45, panelWidth - padding * 2, 1, 255, 255, 255, 26 );

	const int contentTop = panelY + headerHeight;
	const int footerY = panelBottom - footerHeight + 2;

	const int infoWidth = ( panelWidth - padding * 2 ) < 460 ? 62 : 88;
	const int tableX = panelX + padding + infoWidth;
	const int tableWidth = panelWidth - padding * 2 - infoWidth;

	SbColumns columns;
	SbBuildColumns( columns, tableX, tableWidth, config );

	if ( teamMode )
	{
		SbDrawColumnHeaders( columns, contentTop, tableWidth, mutedR, mutedG, mutedB );

		const int blockTop = contentTop + labelHeight;
		const int blockBottom = panelBottom - footerHeight;
		const int blockHeight = ctHeight + tHeight + teamGap + spectatorHeight;
		int freeSpace = blockBottom - blockTop - blockHeight;
		int ctY = blockTop + ( freeSpace > 0 ? freeSpace / 2 : 0 );

		SbDrawTeam( ctTeam, panelX + padding, ctY, panelWidth - padding * 2, ctHeight, rowHeight, players, playerCount, config );

		const int tY = ctY + ctHeight + teamGap;
		SbDrawTeam( tTeam, panelX + padding, tY, panelWidth - padding * 2, tHeight, rowHeight, players, playerCount, config );

		if ( otherPlayers > 0 )
			SbDrawSpectators( players, playerCount, panelX + padding, tY + tHeight + 4, panelWidth - padding * 2, config );
	}
	else
	{
		SbDrawColumnHeaders( columns, contentTop, tableWidth, mutedR, mutedG, mutedB );

		const int maxRows = ( footerY - contentTop - labelHeight - 8 ) / rowHeight;
		SbDrawPlayerRows( tableX, contentTop + labelHeight, tableWidth, maxRows, rowHeight, players, playerCount, 0, false, config );
	}

	SbColor( mutedR, mutedG, mutedB );
	SbText( panelX + padding, footerY, "TAB / TAP SCOREBOARD TO CLOSE" );
}

int SbStricmp( const char *a, const char *b )
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

void OnNetMsg( void *args, void *user_data )
{
	const auto *msg = static_cast<const nexora_netmsg_args_t *>( args );

	if ( !msg || !msg->name || !msg->buf )
		return;

	BufferReader reader( msg->name, const_cast<void *>( msg->buf ), msg->size );

	if ( std::strcmp( msg->name, "ScoreInfo" ) == 0 )
	{
		const int cl = reader.ReadByte();
		const int frags = reader.ReadShort();
		const int deaths = reader.ReadShort();
		const int playerclass = reader.ReadShort();

		reader.ReadShort();

		if ( cl >= 1 && cl <= SB_MAX_PLAYERS )
		{
			auto &state = g_net[cl];

			state.frags = frags;
			state.deaths = deaths;
			state.playerclass = playerclass;
			state.valid = true;
		}

		return;
	}

	if ( std::strcmp( msg->name, "TeamInfo" ) == 0 )
	{
		const int cl = reader.ReadByte();
		const char *teamName = reader.ReadString();

		if ( cl >= 1 && cl <= SB_MAX_PLAYERS )
		{
			std::strncpy( g_net[cl].teamname, teamName ? teamName : "", sizeof( g_net[cl].teamname ) - 1 );
			g_net[cl].teamname[sizeof( g_net[cl].teamname ) - 1] = '\0';

			int teamNumber = TEAM_UNASSIGNED;

			if ( teamName )
			{
				if ( SbStricmp( teamName, "TERRORIST" ) == 0 )
					teamNumber = TEAM_TERRORIST;
				else if ( SbStricmp( teamName, "CT" ) == 0 )
					teamNumber = TEAM_CT;
				else if ( SbStricmp( teamName, "SPECTATOR" ) == 0 )
					teamNumber = TEAM_SPECTATOR;
				else if ( SbStricmp( teamName, "UNASSIGNED" ) == 0 )
					teamNumber = TEAM_UNASSIGNED;
			}

			g_net[cl].team = teamNumber;
		}

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

	if ( std::strcmp( msg->name, "Account" ) == 0 )
	{
		const int idx = reader.ReadByte();
		const int account = reader.ReadLong();

		if ( idx >= 1 && idx <= SB_MAX_PLAYERS )
			g_net[idx].money = account;

		return;
	}

	if ( std::strcmp( msg->name, "ScoreAttrib" ) == 0 )
	{
		const int index = reader.ReadByte();
		const unsigned char flags = static_cast<unsigned char>( reader.ReadByte() );

		if ( index >= 1 && index <= SB_MAX_PLAYERS )
		{
			auto &state = g_net[index];

			state.dead = ( flags & PLAYER_DEAD ) != 0;
			state.hasC4 = ( flags & PLAYER_HAS_C4 ) != 0;
			state.vip = ( flags & PLAYER_VIP ) != 0;
			state.defuse = ( flags & PLAYER_HAS_DEFUSER ) != 0;
		}

		return;
	}

	if ( std::strcmp( msg->name, "ServerName" ) == 0 )
	{
		const char *name = reader.ReadString();

		if ( name )
		{
			std::strncpy( g_serverName, name, sizeof( g_serverName ) - 1 );
			g_serverName[sizeof( g_serverName ) - 1] = '\0';
		}

		return;
	}

	if ( std::strcmp( msg->name, "RoundTime" ) == 0 )
	{
		g_roundTime = reader.ReadShort();
		g_roundStart = g_eng->GetClientTime();
		return;
	}
}

void OnScoreboardToggle( void *args, void *user_data )
{
	const auto *toggle = static_cast<const nexora_scoreboard_args_t *>( args );

	if ( !toggle )
		return;

	g_visible = toggle->visible != 0;
}

void OnHudElementDraw( void *args, void *user_data )
{
	auto *element = static_cast<nexora_hud_element_args_t *>( args );

	if ( !element )
		return;

	if ( element->name && std::strcmp( element->name, "scoreboard" ) == 0 &&
		cl_scoreboard_style && cl_scoreboard_style->value >= 2.0f )
	{
		element->allow = 0;
	}
}

void OnPaintHud( void *args, void *user_data )
{
	const auto *redraw = static_cast<const nexora_redraw_args_t *>( args );

	if ( !redraw )
		return;

	const float flTime = redraw->time;

	float dt = g_lastPaintTime >= 0.0f ? flTime - g_lastPaintTime : 0.0f;
	g_lastPaintTime = flTime;

	if ( dt < 0.0f || dt > 0.25f )
		dt = 0.0f;

	const float target = g_visible ? 1.0f : 0.0f;

	if ( g_fade < target )
	{
		const float fx = cl_scoreboard_fadein ? cl_scoreboard_fadein->value : 0.15f;
		g_fade = std::min( target, g_fade + ( fx > 0.001f ? dt / fx : 1.0f ) );
	}
	else if ( g_fade > target )
	{
		const float fx = cl_scoreboard_fadeout ? cl_scoreboard_fadeout->value : 0.18f;
		g_fade = std::max( target, g_fade - ( fx > 0.001f ? dt / fx : 1.0f ) );
	}

	if ( !cl_scoreboard_style || cl_scoreboard_style->value < 2.0f )
		return;

	if ( g_fade <= 0.001f )
		return;

	SbDrawScoreboard();
}

bool RegisterCVars()
{
	cl_scoreboard_style = g_eng->pfnRegisterVariable( "cl_scoreboard_style", "2", FCVAR_ARCHIVE );
	cl_scoreboard_alpha = g_eng->pfnRegisterVariable( "cl_scoreboard_alpha", "220", FCVAR_ARCHIVE );
	cl_scoreboard_show_money = g_eng->pfnRegisterVariable( "cl_scoreboard_show_money", "1", FCVAR_ARCHIVE );
	cl_scoreboard_show_hp = g_eng->pfnRegisterVariable( "cl_scoreboard_show_hp", "1", FCVAR_ARCHIVE );
	cl_scoreboard_show_ping = g_eng->pfnRegisterVariable( "cl_scoreboard_show_ping", "1", FCVAR_ARCHIVE );
	cl_scoreboard_show_flags = g_eng->pfnRegisterVariable( "cl_scoreboard_show_flags", "1", FCVAR_ARCHIVE );
	cl_scoreboard_high_contrast = g_eng->pfnRegisterVariable( "cl_scoreboard_high_contrast", "0", FCVAR_ARCHIVE );
	cl_scoreboard_fadein = g_eng->pfnRegisterVariable( "cl_scoreboard_fadein", "0.15", FCVAR_ARCHIVE );
	cl_scoreboard_fadeout = g_eng->pfnRegisterVariable( "cl_scoreboard_fadeout", "0.18", FCVAR_ARCHIVE );
	cl_showplayerversion = g_eng->pfnRegisterVariable( "cl_showplayerversion", "0", FCVAR_ARCHIVE );

	return cl_scoreboard_style && cl_scoreboard_alpha && cl_scoreboard_show_money &&
		cl_scoreboard_show_hp && cl_scoreboard_show_ping && cl_scoreboard_show_flags &&
		cl_scoreboard_high_contrast && cl_scoreboard_fadein && cl_scoreboard_fadeout &&
		cl_showplayerversion;
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
		api->con_printf( "[scoreboard2] failed to register cvars\n" );
		return 0;
	}

	api->con_printf( "[scoreboard2] init\n" );

	api->register_hook( NEXORA_HOOK_NETMSG, OnNetMsg, nullptr );
	api->register_hook( NEXORA_HOOK_SCOREBOARD_TOGGLE, OnScoreboardToggle, nullptr );
	api->register_hook( NEXORA_HOOK_HUD_ELEMENT_DRAW, OnHudElementDraw, nullptr );
	api->register_hook( NEXORA_HOOK_PAINT_HUD, OnPaintHud, nullptr );

	return 1;
}
