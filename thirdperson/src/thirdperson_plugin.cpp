#include <nexora/nexora_plugin.h>
#include <wrect.h>
#include <cl_dll.h>
#include <cvardef.h>

#include <cstring>

namespace
{

const nexora_plugin_info_t s_info =
{
	"thirdperson",
	"1.0.0",
	NEXORA_PLUGIN_API_VERSION,
	40,
};

const nexora_plugin_api_t *g_api = nullptr;
cl_enginefunc_t *g_eng = nullptr;

cvar_t *hud_thirdperson = nullptr;

void OnThirdPerson( void *args, void *user_data )
{
	auto *thirdperson = static_cast<nexora_thirdperson_args_t *>( args );

	if ( !thirdperson )
		return;

	if ( hud_thirdperson && hud_thirdperson->value >= 0.5f )
		thirdperson->allow = 1;
}

bool RegisterCVars()
{
	hud_thirdperson = g_eng->pfnRegisterVariable( "hud_thirdperson", "1", FCVAR_ARCHIVE );

	return hud_thirdperson != nullptr;
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
		api->con_printf( "[thirdperson] failed to register cvars\n" );
		return 0;
	}

	api->con_printf( "[thirdperson] init\n" );

	api->register_hook( NEXORA_HOOK_THIRDPERSON, OnThirdPerson, nullptr );

	return 1;
}
