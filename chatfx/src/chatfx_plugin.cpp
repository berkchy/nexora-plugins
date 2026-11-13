#include <nexora/nexora_plugin.h>
#include <wrect.h>
#include <cl_dll.h>
#include <cvardef.h>
#include <parsemsg.h>
#include <triangleapi.h>
#include <render_api.h>
#include <cdll_dll.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <algorithm>

namespace
{

const nexora_plugin_info_t s_info =
{
	"chatfx",
	"1.0.0",
	NEXORA_PLUGIN_API_VERSION,
	30,
};

const nexora_plugin_api_t *g_api = nullptr;
cl_enginefunc_t *g_eng = nullptr;
render_api_t *g_render = nullptr;

cvar_t *hud_chatfx_enable = nullptr;
cvar_t *hud_chatfx_x = nullptr;
cvar_t *hud_chatfx_y = nullptr;
cvar_t *hud_chatfx_scale = nullptr;
cvar_t *hud_chatfx_anim = nullptr;
cvar_t *hud_chatfx_anim_open = nullptr;
cvar_t *hud_chatfx_anim_close = nullptr;
cvar_t *hud_chatfx_lines = nullptr;
cvar_t *hud_chatfx_holdtime = nullptr;
cvar_t *hud_chatfx_linespacing = nullptr;
cvar_t *hud_chatfx_pad = nullptr;
cvar_t *hud_chatfx_bg = nullptr;
cvar_t *hud_chatfx_bgcolor = nullptr;
cvar_t *hud_chatfx_bgalpha = nullptr;
cvar_t *hud_chatfx_color = nullptr;
cvar_t *hud_chatfx_text_alpha = nullptr;
cvar_t *hud_chatfx_font = nullptr;

int s_hideFlags = 0;

// ---------------------------------------------------------------- font

const int CH_ATLAS_W = 256;
const int CH_ATLAS_H = 256;
const int CH_BAKE_PX = 48;

unsigned char s_fontBuffer[ 1 << 20 ] = { 0 };
size_t s_fontBufferSize = 0;
stbtt_bakedchar s_baked[ 96 ] = {};
int s_fontTex = 0;
bool s_fontReady = false;
bool s_fontFailed = false;
float s_ascent = 0.0f;

bool ChatLoadFile( const char *path, unsigned char *buffer, size_t bufferSize, size_t &outSize )
{
	FILE *f = std::fopen( path, "rb" );

	if ( !f )
		return false;

	std::fseek( f, 0, SEEK_END );
	const long len = std::ftell( f );
	std::fseek( f, 0, SEEK_SET );

	if ( len <= 0 || ( size_t )len > bufferSize )
	{
		std::fclose( f );
		return false;
	}

	const size_t read = std::fread( buffer, 1, ( size_t )len, f );
	std::fclose( f );

	outSize = read;

	return read == ( size_t )len && read > 0;
}

bool ChatEnsureFont()
{
	if ( s_fontReady )
		return true;

	if ( s_fontFailed )
		return false;

	const char *candidates[ 5 ] =
	{
		hud_chatfx_font ? hud_chatfx_font->string : nullptr,
		"/system/fonts/RobotoMono-Regular.ttf",
		"/system/fonts/Roboto-Regular.ttf",
		"/system/fonts/DroidSans.ttf",
		"/system/fonts/NotoSansMono-Regular.ttf",
	};

	bool loaded = false;

	for ( int i = 0; i < 5; ++i )
	{
		if ( !candidates[ i ] || !candidates[ i ][ 0 ] )
			continue;

		if ( ChatLoadFile( candidates[ i ], s_fontBuffer, sizeof( s_fontBuffer ), s_fontBufferSize ) )
		{
			loaded = true;
			break;
		}
	}

	if ( !loaded )
	{
		s_fontFailed = true;
		return false;
	}

	stbtt_fontinfo info;

	if ( !stbtt_InitFont( &info, s_fontBuffer, 0 ) )
	{
		s_fontFailed = true;
		return false;
	}

	int ascent = 0;
	stbtt_GetFontVMetrics( &info, &ascent, nullptr, nullptr );
	s_ascent = ascent * stbtt_ScaleForPixelHeight( &info, ( float )CH_BAKE_PX );

	unsigned char bitmap[ CH_ATLAS_W * CH_ATLAS_H ] = { 0 };

	const int result = stbtt_BakeFontBitmap( s_fontBuffer, 0, ( float )CH_BAKE_PX, bitmap,
		CH_ATLAS_W, CH_ATLAS_H, 32, 96, s_baked );

	if ( result <= 0 )
	{
		s_fontFailed = true;
		return false;
	}

	static unsigned char rgba[ CH_ATLAS_W * CH_ATLAS_H * 4 ];

	for ( int i = 0; i < CH_ATLAS_W * CH_ATLAS_H; ++i )
	{
		rgba[ i * 4 + 0 ] = 255;
		rgba[ i * 4 + 1 ] = 255;
		rgba[ i * 4 + 2 ] = 255;
		rgba[ i * 4 + 3 ] = bitmap[ i ];
	}

	s_fontTex = 0;

	if ( g_render && g_render->GL_CreateTexture )
	{
		s_fontTex = g_render->GL_CreateTexture( "*chatfx_font", CH_ATLAS_W, CH_ATLAS_H, rgba,
			( texFlags_t )( TF_NOMIPMAP | TF_NEAREST | TF_CLAMP | TF_HAS_ALPHA ) );
	}

	if ( s_fontTex == 0 )
	{
		s_fontFailed = true;
		return false;
	}

	s_fontReady = true;
	return true;
}

// ------------------------------------------------------------- lines

constexpr int CHAT_MAX_LINES = 32;
constexpr int CHAT_MAX_CHARS = 256;

struct ChatLine
{
	char text[ CHAT_MAX_CHARS ];
	float arrive;
	int speaker;
};

ChatLine s_lines[ CHAT_MAX_LINES ];
int s_lineCount = 0;
int s_team[ 64 ] = { 0 };

void ChatPush( const char *text, int speaker, float time )
{
	if ( !text || !text[ 0 ] )
		return;

	if ( s_lineCount >= CHAT_MAX_LINES )
	{
		std::memmove( s_lines, s_lines + 1, sizeof( ChatLine ) * ( CHAT_MAX_LINES - 1 ) );
		s_lineCount = CHAT_MAX_LINES - 1;
	}

	ChatLine &slot = s_lines[ s_lineCount ];

	std::strncpy( slot.text, text, sizeof( slot.text ) - 1 );
	slot.text[ sizeof( slot.text ) - 1 ] = '\0';
	slot.arrive = time;
	slot.speaker = speaker;

	++s_lineCount;
}

void OnChat( void *args, void *user_data )
{
	auto *chat = static_cast<nexora_chat_args_t *>( args );

	if ( !chat )
		return;

	if ( !hud_chatfx_enable || hud_chatfx_enable->value < 0.5f )
		return;

	chat->allow = 0;

	float now = chat->time;

	if ( now <= 0.0f && g_eng )
		now = g_eng->GetClientTime();

	ChatPush( chat->text, chat->client_index, now );
}

void OnNetMsg( void *args, void *user_data )
{
	const auto *msg = static_cast<const nexora_netmsg_args_t *>( args );

	if ( !msg || !msg->name || !msg->buf )
		return;

	BufferReader reader( msg->name, const_cast<void *>( msg->buf ), msg->size );

	if ( std::strcmp( msg->name, "TeamInfo" ) == 0 )
	{
		const int cl = reader.ReadByte();
		const char *teamName = reader.ReadString();

		if ( cl < 0 || cl >= 64 )
			return;

		int team = TEAM_UNASSIGNED;

		if ( teamName )
		{
			if ( std::strstr( teamName, "CT" ) || std::strstr( teamName, "Counter" ) )
				team = TEAM_CT;
			else if ( std::strstr( teamName, "Terror" ) )
				team = TEAM_TERRORIST;
			else if ( std::strstr( teamName, "Spec" ) )
				team = TEAM_SPECTATOR;
		}

		s_team[ cl ] = team;
	}
}

void OnUpdateClientData( void *args, void *user_data )
{
	const auto *update = static_cast<const nexora_update_client_data_args_t *>( args );

	if ( update )
		s_hideFlags = update->hide_hud;
}

bool ChatEnabled()
{
	return hud_chatfx_enable && hud_chatfx_enable->value >= 0.5f;
}

void CpParseColor( const char *value, int defR, int defG, int defB, int &r, int &g, int &b )
{
	r = defR;
	g = defG;
	b = defB;

	if ( value && value[ 0 ] )
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

enum AxisKind
{
	AX_AUTO,
	AX_VALUE,
};

AxisKind ParseAxis( const char *s, int screenSize, int &out )
{
	out = 0;

	if ( !s || !s[ 0 ] )
		return AX_AUTO;

	const char *a = s;
	int ai = 0;

	while ( a[ ai ] )
	{
		if ( std::tolower( ( unsigned char )a[ ai ] ) != "auto"[ ai ] )
			break;
		++ai;
	}

	if ( a[ ai ] == '\0' && ai == 4 )
		return AX_AUTO;

	size_t len = std::strlen( s );

	if ( len > 1 && s[ len - 1 ] == '%' )
	{
		char *end = nullptr;
		float pct = std::strtof( s, &end );

		if ( end && end == s + len - 1 )
		{
			float fr = pct / 100.0f;

			if ( fr < 0.0f )
				fr += 1.0f;

			out = ( int )std::lround( fr * ( float )screenSize );
			return AX_VALUE;
		}

		return AX_AUTO;
	}

	char *end = nullptr;
	long v = std::strtol( s, &end, 10 );

	if ( end == s )
		return AX_AUTO;

	if ( v < 0 )
		v += screenSize;

	out = ( int )v;
	return AX_VALUE;
}

// ---------------------------------------------------------------- drawing

struct SegColor
{
	int r, g, b;
};

void SegTeamColor( int speaker, int &r, int &g, int &b )
{
	int team = TEAM_UNASSIGNED;

	if ( speaker >= 0 && speaker < 64 )
		team = s_team[ speaker ];

	switch ( team )
	{
	case TEAM_CT: r = 153; g = 204; b = 255; return;
	case TEAM_TERRORIST: r = 255; g = 64; b = 64; return;
	case TEAM_SPECTATOR: r = 204; g = 204; b = 204; return;
	default: r = 204; g = 204; b = 204; return;
	}
}

void SegColorOf( char code, int speaker, int &r, int &g, int &b )
{
	int defR = 255, defG = 178, defB = 0;

	CpParseColor( hud_chatfx_color ? hud_chatfx_color->string : nullptr, 255, 178, 0, defR, defG, defB );

	switch ( code )
	{
	case '\x01':
		r = defR; g = defG; b = defB;
		break;
	case '\x03':
		SegTeamColor( speaker, r, g, b );
		break;
	case '\x04':
	default:
		r = 0; g = 255; b = 0;
		break;
	}
}

float ChatSmooth( float t )
{
	t = std::max( 0.0f, std::min( 1.0f, t ) );
	return t * t * ( 3.0f - 2.0f * t );
}

float ChatEaseOutCubic( float t )
{
	t = std::max( 0.0f, std::min( 1.0f, t ) );
	const float u = 1.0f - t;
	return 1.0f - u * u * u;
}

float ChatEaseOutBack( float t )
{
	t = std::max( 0.0f, std::min( 1.0f, t ) );
	const float c1 = 1.70158f;
	const float c3 = c1 + 1.0f;
	const float u = t - 1.0f;
	return 1.0f + c3 * u * u * u + c1 * u * u;
}

enum AnimMode
{
	ANIM_FADE,
	ANIM_SLIDE,
	ANIM_SCALE,
	ANIM_BOUNCE,
};

AnimMode ChatAnimMode()
{
	const char *s = hud_chatfx_anim ? hud_chatfx_anim->string : nullptr;

	if ( !s )
		return ANIM_FADE;

	if ( std::strstr( s, "slide" ) )
		return ANIM_SLIDE;
	if ( std::strstr( s, "scale" ) )
		return ANIM_SCALE;
	if ( std::strstr( s, "bounce" ) )
		return ANIM_BOUNCE;

	return ANIM_FADE;
}

struct LineStyle
{
	float alpha;
	float xOffset;
	float scaleMul;
};

LineStyle ChatLineStyle( float now, float arrive, AnimMode mode, float openT, float closeT, float holdT )
{
	const float t = now - arrive;
	const float openP = openT > 0.0f ? std::max( 0.0f, std::min( 1.0f, t / openT ) ) : 1.0f;
	const float closeStart = openT + holdT;
	const float closeP = t > closeStart
		? ( closeT > 0.0f ? std::max( 0.0f, std::min( 1.0f, ( t - closeStart ) / closeT ) ) : 1.0f )
		: 0.0f;

	LineStyle style;

	switch ( mode )
	{
	case ANIM_FADE:
		style.alpha = ChatSmooth( openP ) * ( 1.0f - ChatSmooth( closeP ) );
		style.xOffset = 0.0f;
		style.scaleMul = 1.0f;
		break;

	case ANIM_SLIDE:
		style.alpha = ChatSmooth( std::min( 1.0f, openP * 1.25f ) ) * ( 1.0f - ChatSmooth( closeP ) );
		style.xOffset = -( 1.0f - ChatEaseOutCubic( openP ) ) * 160.0f
			+ ChatEaseOutCubic( closeP ) * 120.0f;
		style.scaleMul = 1.0f;
		break;

	case ANIM_SCALE:
		style.alpha = ChatSmooth( openP ) * ( 1.0f - ChatSmooth( closeP ) );
		style.xOffset = 0.0f;
		style.scaleMul = ( 0.35f + 0.65f * ChatEaseOutCubic( openP ) )
			* ( 1.0f - 0.45f * ChatEaseOutCubic( closeP ) );
		break;

	case ANIM_BOUNCE:
	default:
		style.alpha = ChatSmooth( openP ) * ( 1.0f - ChatSmooth( closeP ) );
		style.xOffset = 0.0f;
		style.scaleMul = openP < 1.0f ? ChatEaseOutBack( openP ) : 1.0f - 0.3f * ChatEaseOutCubic( closeP );
		break;
	}

	return style;
}

bool ChatLineAlive( float now, float arrive, float openT, float closeT, float holdT )
{
	return now - arrive < openT + holdT + closeT;
}

// Draw one segment of a chat line. Returns the advance width.
float ChatDrawSTB( const char *seg, float x, float yTop, float fontPx, int r, int g, int b, float alpha )
{
	if ( !s_fontReady )
		return 0.0f;

	const float k = fontPx / ( float )CH_BAKE_PX;
	const float baseline = yTop + s_ascent * k;

	float pen = 0.0f;

	for ( const unsigned char *p = ( const unsigned char * )seg; *p; ++p )
	{
		const unsigned char c = *p;

		if ( c < 32 || c > 126 )
		{
			pen += 4.0f;
			continue;
		}

		stbtt_aligned_quad q;
		float cx = pen;
		float cy = 0.0f;

		stbtt_GetBakedQuad( s_baked, CH_ATLAS_W, CH_ATLAS_H, ( int )c - 32, &cx, &cy, &q, 1 );

		const float gx0 = x + q.x0 * k;
		const float gx1 = x + q.x1 * k;
		const float gy0 = baseline + q.y0 * k;
		const float gy1 = baseline + q.y1 * k;

		if ( g_render && g_render->GL_Bind && g_eng && g_eng->pTriAPI )
		{
			g_render->GL_SelectTexture( 0 );
			g_render->GL_Bind( 0, ( unsigned int )s_fontTex );
			g_eng->pTriAPI->RenderMode( kRenderTransAlpha );
			g_eng->pTriAPI->Color4f( r / 255.0f, g / 255.0f, b / 255.0f, alpha );

			g_eng->pTriAPI->Begin( TRI_QUADS );
			g_eng->pTriAPI->TexCoord2f( q.s0, q.t1 );
			g_eng->pTriAPI->Vertex3f( gx0, gy1, 0 );
			g_eng->pTriAPI->TexCoord2f( q.s1, q.t1 );
			g_eng->pTriAPI->Vertex3f( gx1, gy1, 0 );
			g_eng->pTriAPI->TexCoord2f( q.s1, q.t0 );
			g_eng->pTriAPI->Vertex3f( gx1, gy0, 0 );
			g_eng->pTriAPI->TexCoord2f( q.s0, q.t0 );
			g_eng->pTriAPI->Vertex3f( gx0, gy0, 0 );
			g_eng->pTriAPI->End( );
		}

		pen = cx;
	}

	return pen * k;
}

float ChatDrawConsole( const char *seg, float x, float yTop, int r, int g, int b, float alpha )
{
	if ( !g_eng || !g_eng->pfnDrawConsoleString )
		return 0.0f;

	if ( g_eng->pfnDrawSetTextColor )
		g_eng->pfnDrawSetTextColor( ( r / 255.0f ) * alpha, ( g / 255.0f ) * alpha, ( b / 255.0f ) * alpha );

	g_eng->pfnDrawConsoleString( ( int )x, ( int )yTop, const_cast<char *>( seg ) );

	int width = 0;

	if ( g_eng->pfnDrawConsoleStringLen )
	{
		int height = 0;
		g_eng->pfnDrawConsoleStringLen( seg, &width, &height );
	}

	return ( float )width;
}

float ChatLineWidthSTB( const char *line, float fontPx )
{
	const float k = fontPx / ( float )CH_BAKE_PX;
	float pen = 0.0f;

	for ( const unsigned char *p = ( const unsigned char * )line; *p; ++p )
	{
		const unsigned char c = *p;

		if ( c < 32 || c > 126 )
		{
			pen += 4.0f;
			continue;
		}

		stbtt_aligned_quad q;
		float cx = pen;
		float cy = 0.0f;

		stbtt_GetBakedQuad( s_baked, CH_ATLAS_W, CH_ATLAS_H, ( int )c - 32, &cx, &cy, &q, 1 );

		pen = cx;
	}

	return pen * k;
}

float ChatLineWidth( const char *line, float fontPx )
{
	if ( s_fontReady )
		return ChatLineWidthSTB( line, fontPx );

	float width = 0.0f;

	if ( g_eng && g_eng->pfnDrawConsoleStringLen )
	{
		int w = 0, h = 0;
		g_eng->pfnDrawConsoleStringLen( line, &w, &h );
		width = ( float )w;
	}

	return width;
}

float ChatMeasure( const char *line, float fontPx )
{
	// sum of per-segment widths, tracking color codes (they take zero width)
	float width = 0.0f;
	const char *start = line;

	for ( const char *p = line; ; ++p )
	{
		const char c = *p;

		if ( c == '\x01' || c == '\x03' || c == '\x04' || c == '\0' )
		{
			const size_t len = ( size_t )( p - start );

			if ( len > 0 )
			{
				char seg[ CHAT_MAX_CHARS ];
				const size_t copy = std::min( len, sizeof( seg ) - 1 );
				std::memcpy( seg, start, copy );
				seg[ copy ] = '\0';
				width += ChatLineWidth( seg, fontPx );
			}

			if ( c == '\0' )
				break;

			start = p + 1;
		}
	}

	return width;
}

void ChatDrawLine( const char *line, float x, float yTop, float fontPx, int speaker, float alpha )
{
	float penX = x;
	const char *start = line;
	char code = '\x01';

	int r = 255, g = 178, b = 0;

	for ( const char *p = line; ; ++p )
	{
		const char c = *p;

		if ( c == '\x01' || c == '\x03' || c == '\x04' || c == '\0' )
		{
			const size_t len = ( size_t )( p - start );

			if ( len > 0 )
			{
				char seg[ CHAT_MAX_CHARS ];
				const size_t copy = std::min( len, sizeof( seg ) - 1 );
				std::memcpy( seg, start, copy );
				seg[ copy ] = '\0';

				SegColorOf( code, speaker, r, g, b );

				const float advance = s_fontReady
					? ChatDrawSTB( seg, penX, yTop, fontPx, r, g, b, alpha )
					: ChatDrawConsole( seg, penX, yTop, r, g, b, alpha );

				penX += advance;
			}

			if ( c == '\0' )
				break;

			code = c;
			start = p + 1;
		}
	}
}

// ------------------------------------------------------- HUD rendering

void OnPaintHud( void *args, void *user_data )
{
	if ( s_hideFlags & HIDEHUD_ALL )
		return;

	if ( !ChatEnabled() )
		return;

	float now = 0.0f;

	if ( g_eng )
		now = g_eng->GetClientTime();

	const auto *redraw = static_cast<const nexora_redraw_args_t *>( args );

	if ( redraw )
		now = redraw->time;

	const int visible = std::min( s_lineCount, hud_chatfx_lines ? ( int )hud_chatfx_lines->value : 5 );

	if ( visible <= 0 )
		return;

	const float scale = hud_chatfx_scale ? std::max( 0.5f, std::min( 3.0f, hud_chatfx_scale->value ) ) : 1.0f;
	const float fontPx = 13.0f * scale;
	const int lineSpacing = hud_chatfx_linespacing ? ( int )hud_chatfx_linespacing->value : 0;

	// drop lines whose animation has fully finished
	const float openT = hud_chatfx_anim_open ? std::max( 0.0f, std::min( 2.0f, hud_chatfx_anim_open->value ) ) : 0.25f;
	const float closeT = hud_chatfx_anim_close ? std::max( 0.0f, std::min( 2.0f, hud_chatfx_anim_close->value ) ) : 0.2f;
	const float holdT = hud_chatfx_holdtime ? std::max( 0.0f, std::min( 60.0f, hud_chatfx_holdtime->value ) ) : 5.0f;

	while ( s_lineCount > 0 && !ChatLineAlive( now, s_lines[ 0 ].arrive, openT, closeT, holdT ) )
	{
		std::memmove( s_lines, s_lines + 1, sizeof( ChatLine ) * ( s_lineCount - 1 ) );
		--s_lineCount;
	}

	const int lineCount = std::min( s_lineCount, visible );

	if ( lineCount <= 0 )
		return;

	int screenWidth = 1280;
	int screenHeight = 720;

	if ( g_api && g_api->get_screen_size )
		g_api->get_screen_size( &screenWidth, &screenHeight );

	ChatEnsureFont();

	const int pad = hud_chatfx_pad ? std::max( 0, ( int )hud_chatfx_pad->value ) : 6;
	const float lineH = fontPx + lineSpacing;

	// measure the widest visible line
	float maxWidth = 0.0f;

	for ( int i = s_lineCount - lineCount; i < s_lineCount; ++i )
	{
		const float w = ChatMeasure( s_lines[ i ].text, fontPx );

		if ( w > maxWidth )
			maxWidth = w;
	}

	const int blockW = ( int )std::ceil( maxWidth ) + pad * 2;
	const int blockH = ( int )std::ceil( lineH * lineCount ) + pad * 2;

	int x = 8;
	int y = screenHeight - 24 - blockH;

	if ( hud_chatfx_x )
	{
		int v = 0;
		if ( ParseAxis( hud_chatfx_x->string, screenWidth, v ) == AX_VALUE )
			x = v;
	}

	if ( hud_chatfx_y )
	{
		int v = 0;
		if ( ParseAxis( hud_chatfx_y->string, screenHeight, v ) == AX_VALUE )
			y = v;
	}

	const bool drawBg = hud_chatfx_bg && hud_chatfx_bg->value >= 0.5f;
	int bgR = 0, bgG = 0, bgB = 0;

	if ( drawBg )
		CpParseColor( hud_chatfx_bgcolor ? hud_chatfx_bgcolor->string : nullptr, 0, 0, 0, bgR, bgG, bgB );

	const float bgAlphaBase = hud_chatfx_bgalpha ? hud_chatfx_bgalpha->value : 140.0f;
	const int bgAlpha = std::max( 0, std::min( 255, ( int )bgAlphaBase ) );

	if ( drawBg && bgAlpha > 0 && g_eng && g_eng->pfnFillRGBABlend )
		g_eng->pfnFillRGBABlend( x, y, blockW, blockH, bgR, bgG, bgB, bgAlpha );

	// lines, oldest at top
	float yy = ( float )y + pad;
	const float userAlpha = hud_chatfx_text_alpha ? hud_chatfx_text_alpha->value : 235.0f;
	const float textAlpha = std::max( 0.0f, std::min( 255.0f, userAlpha ) ) / 255.0f;

	for ( int i = s_lineCount - lineCount; i < s_lineCount; ++i )
	{
		const ChatLine &line = s_lines[ i ];
		const LineStyle style = ChatLineStyle( now, line.arrive, ChatAnimMode(), openT, closeT, holdT );

		if ( style.alpha <= 0.01f )
		{
			yy += lineH;
			continue;
		}

		const float alpha = style.alpha * textAlpha;
		const float lineScale = std::max( 0.1f, style.scaleMul );
		const float lineFontPx = fontPx * lineScale;

		// vertical centering of scaled text inside its band
		const float scaleOffsetY = ( lineH - lineFontPx ) * 0.5f;

		ChatDrawLine( line.text, ( float )x + pad + style.xOffset, yy + scaleOffsetY, lineFontPx, line.speaker, alpha );

		yy += lineH;
	}
}

// ------------------------------------------------------------ cvars/init

bool RegisterCVars()
{
	hud_chatfx_enable = g_eng->pfnRegisterVariable( "hud_chatfx_enable", "1", FCVAR_ARCHIVE );
	hud_chatfx_x = g_eng->pfnRegisterVariable( "hud_chatfx_x", "auto", FCVAR_ARCHIVE );
	hud_chatfx_y = g_eng->pfnRegisterVariable( "hud_chatfx_y", "auto", FCVAR_ARCHIVE );
	hud_chatfx_scale = g_eng->pfnRegisterVariable( "hud_chatfx_scale", "1", FCVAR_ARCHIVE );
	hud_chatfx_anim = g_eng->pfnRegisterVariable( "hud_chatfx_anim", "fade", FCVAR_ARCHIVE );
	hud_chatfx_anim_open = g_eng->pfnRegisterVariable( "hud_chatfx_anim_open", "0.25", FCVAR_ARCHIVE );
	hud_chatfx_anim_close = g_eng->pfnRegisterVariable( "hud_chatfx_anim_close", "0.2", FCVAR_ARCHIVE );
	hud_chatfx_lines = g_eng->pfnRegisterVariable( "hud_chatfx_lines", "5", FCVAR_ARCHIVE );
	hud_chatfx_holdtime = g_eng->pfnRegisterVariable( "hud_chatfx_holdtime", "5", FCVAR_ARCHIVE );
	hud_chatfx_linespacing = g_eng->pfnRegisterVariable( "hud_chatfx_linespacing", "0", FCVAR_ARCHIVE );
	hud_chatfx_pad = g_eng->pfnRegisterVariable( "hud_chatfx_pad", "6", FCVAR_ARCHIVE );
	hud_chatfx_bg = g_eng->pfnRegisterVariable( "hud_chatfx_bg", "1", FCVAR_ARCHIVE );
	hud_chatfx_bgcolor = g_eng->pfnRegisterVariable( "hud_chatfx_bgcolor", "0 0 0", FCVAR_ARCHIVE );
	hud_chatfx_bgalpha = g_eng->pfnRegisterVariable( "hud_chatfx_bgalpha", "140", FCVAR_ARCHIVE );
	hud_chatfx_color = g_eng->pfnRegisterVariable( "hud_chatfx_color", "255 178 0", FCVAR_ARCHIVE );
	hud_chatfx_text_alpha = g_eng->pfnRegisterVariable( "hud_chatfx_text_alpha", "235", FCVAR_ARCHIVE );
	hud_chatfx_font = g_eng->pfnRegisterVariable( "hud_chatfx_font", "/system/fonts/RobotoMono-Regular.ttf", FCVAR_ARCHIVE );

	return hud_chatfx_enable && hud_chatfx_x && hud_chatfx_y && hud_chatfx_scale &&
		hud_chatfx_anim && hud_chatfx_anim_open && hud_chatfx_anim_close && hud_chatfx_lines &&
		hud_chatfx_holdtime && hud_chatfx_linespacing && hud_chatfx_pad && hud_chatfx_bg &&
		hud_chatfx_bgcolor && hud_chatfx_bgalpha && hud_chatfx_color && hud_chatfx_text_alpha &&
		hud_chatfx_font;
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

	if ( !RegisterCVars() )
	{
		api->con_printf( "[chatfx] failed to register cvars\n" );
		return 0;
	}

	api->con_printf( "[chatfx] init\n" );

	api->register_hook( NEXORA_HOOK_CHAT, OnChat, nullptr );
	api->register_hook( NEXORA_HOOK_NETMSG, OnNetMsg, nullptr );
	api->register_hook( NEXORA_HOOK_PAINT_HUD, OnPaintHud, nullptr );
	api->register_hook( NEXORA_HOOK_UPDATE_CLIENT_DATA, OnUpdateClientData, nullptr );

	return 1;
}
