#include "global.h"
#include "LuaManager.h"
#include "LuaFunctions.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "RageFile.h"
#include "arch/Dialog/Dialog.h"
#include "Foreach.h"
#include "ActorCommands.h"

#include <csetjmp>
#include <cassert>

LuaManager *LUA = NULL;
static LuaFunctionList *g_LuaFunctions = NULL;

#if defined(_WINDOWS)
	#pragma comment(lib, "lua-5.0/lib/LibLua.lib")
	#pragma comment(lib, "lua-5.0/lib/LibLuaLib.lib")
#endif
#if defined(_XBOX)
	#pragma comment(lib, "lua-5.0/lib/LibLuaXbox.lib")
	#pragma comment(lib, "lua-5.0/lib/LibLuaLibXbox.lib")
#endif
#if defined(_WINDOWS) || defined (_XBOX)
	/* "interaction between '_setjmp' and C++ object destruction is non-portable"
	 * We don't care; we'll throw a fatal exception immediately anyway. */
	#pragma warning (disable : 4611)
#endif
#if defined (DARWIN)
	extern void NORETURN longjmp(jmp_buf env, int val);
#endif



struct ChunkReaderData
{
	const CString *buf;
	bool done;
	ChunkReaderData() { buf = NULL; done = false; }
};

const char *ChunkReaderString( lua_State *L, void *ptr, size_t *size )
{
	ChunkReaderData *data = (ChunkReaderData *) ptr;
	if( data->done )
		return NULL;

	data->done = true;

	*size = data->buf->size();
	const char *ret = data->buf->data();
	
	return ret;
}

void LuaManager::PushStackNil()
{
	lua_pushnil( L );
}

void LuaManager::PushNopFunction()
{
	lua_rawgeti( LUA->L, LUA_REGISTRYINDEX, m_iNopFunction );

	ASSERT_M( !lua_isnil(L, -1), ssprintf("%i", m_iNopFunction) )
}

void LuaManager::PushStack( int out, lua_State *L )
{
	if( L == NULL )
		L = LUA->L;

	/* XXX: stack bounds */
	lua_pushnumber( L, out );
}

void LuaManager::PushStack( bool out, lua_State *L )
{
	if( L == NULL )
		L = LUA->L;

	/* XXX: stack bounds */
	lua_pushboolean( L, out );
}

void LuaManager::PushStack( float val, lua_State *L )
{
	if( L == NULL )
		L = LUA->L;

	/* XXX: stack bounds */
	lua_pushnumber( L, val );
}

void LuaManager::PushStack( void *out, lua_State *L )
{
	if( L == NULL )
		L = LUA->L;

	if( out )
		lua_pushlightuserdata( L, out );
	else
		lua_pushnil( L );
}

void LuaManager::PushStack( const CString &out, lua_State *L )
{
	if( L == NULL )
		L = LUA->L;
	lua_pushstring( L, out );
}

void LuaManager::PopStack( CString &out )
{
	/* There must be at least one entry on the stack. */
	ASSERT( lua_gettop(L) > 0 );
	ASSERT( lua_isstring(L, -1) );

	out = lua_tostring( L, -1 );
	lua_pop( L, -1 );
}

bool LuaManager::GetStack( int pos, int &out )
{
	if( pos < 0 )
		pos = lua_gettop(L) - pos - 1;
	if( pos < 1 )
		return false;
	
	out = (int) lua_tonumber( L, pos );
	return true;
}

void LuaManager::SetGlobal( const CString &sName )
{
	lua_setglobal( L, sName );
}


static int LuaPanic( lua_State *L )
{
	CString sErr;
	LUA->PopStack( sErr );

	RageException::Throw( "%s", sErr.c_str() );
}



// Actor registration
static vector<RegisterActorFn>	*g_vRegisterActorTypes = NULL;

void LuaManager::Register( RegisterActorFn pfn )
{
	if( g_vRegisterActorTypes == NULL )
		g_vRegisterActorTypes = new vector<RegisterActorFn>;

	g_vRegisterActorTypes->push_back( pfn );
}





LuaManager::LuaManager()
{
	LUA = this;	// so that LUA is available when we call the Register functions

	L = NULL;

	ResetState();
}

LuaManager::~LuaManager()
{
	lua_close( L );
}

void LuaManager::ResetState()
{
	if( L != NULL )
		lua_close( L );

	L = lua_open();
	ASSERT( L );

	lua_atpanic( L, LuaPanic );
	
	luaopen_base( L );
	luaopen_math( L );
	luaopen_string( L );
	lua_settop(L, 0); // luaopen_* pushes stuff onto the stack that we don't need

	/* Set up the NOP function pointer. */
	RunScript( "return function() end", 1 );
	m_iNopFunction = luaL_ref( L, LUA_REGISTRYINDEX );

	for( const LuaFunctionList *p = g_LuaFunctions; p; p=p->next )
		lua_register( L, p->name, p->func );

	if( g_vRegisterActorTypes )
	{
		for( unsigned i=0; i<g_vRegisterActorTypes->size(); i++ )
		{
			RegisterActorFn fn = (*g_vRegisterActorTypes)[i];
			fn( L );
		}
	}

	ActorCommands::ReRegisterAll();
}

void LuaManager::PrepareExpression( CString &sInOut )
{
	// HACK: Many metrics have "//" comments that Lua fails to parse.
	// Replace them with Lua-style comments.
	sInOut.Replace( "//", "--" );
	
	// comment out HTML style color values
	sInOut.Replace( "#", "--" );
	
	// Remove leading +, eg. "+50"; Lua doesn't handle that.
	if( sInOut.size() >= 1 && sInOut[0] == '+' )
		sInOut.erase( 0, 1 );
}

bool LuaManager::RunScriptFile( const CString &sFile )
{
	RageFile f;
	if( !f.Open( sFile ) )
	{
		CString sError = ssprintf( "Couldn't open Lua script \"%s\": %s", sFile.c_str(), f.GetError().c_str() );
		Dialog::OK( sError, "LUA_ERROR" );
		return false;
	}

	CString sScript;
	if( f.Read( sScript ) == -1 )
	{
		CString sError = ssprintf( "Error reading Lua script \"%s\": %s", sFile.c_str(), f.GetError().c_str() );
		Dialog::OK( sError, "LUA_ERROR" );
		return false;
	}

	return RunScript( sScript );
}

bool LuaManager::RunScript( const CString &sScript, int iReturnValues )
{
	// load string
	{
		ChunkReaderData data;
		data.buf = &sScript;
		int ret = lua_load( L, ChunkReaderString, &data, "in" );

		if( ret )
		{
			CString err;
			LuaManager::PopStack( err );
			CString sError = ssprintf( "Lua runtime error parsing \"%s\": %s", sScript.c_str(), err.c_str() );
			Dialog::OK( sError, "LUA_ERROR" );
			return false;
		}

		ASSERT_M( lua_gettop(L) == 1, ssprintf("%i", lua_gettop(L)) );
	}

	// evaluate
	{
		int ret = lua_pcall( L, 0, iReturnValues, 0 );
		if( ret )
		{
			CString err;
			LuaManager::PopStack( err );
			CString sError = ssprintf( "Lua runtime error evaluating \"%s\": %s", sScript.c_str(), err.c_str() );
			Dialog::OK( sError, "LUA_ERROR" );
			return false;
		}
	}

	return true;
}


bool LuaManager::RunExpression( const CString &str )
{
	// load string
	{
		ChunkReaderData data;
		CString sStatement = "return " + str;
		data.buf = &sStatement;
		int ret = lua_load( L, ChunkReaderString, &data, "in" );

		if( ret )
		{
			CString err;
			LuaManager::PopStack( err );
			CString sError = ssprintf( "Lua runtime error parsing \"%s\": %s", str.c_str(), err.c_str() );
			Dialog::OK( sError, "LUA_ERROR" );
			return false;
		}

		ASSERT_M( lua_gettop(L) == 1, ssprintf("%i", lua_gettop(L)) );
	}

	// evaluate
	{
		int ret = lua_pcall(L, 0, 1, 0);
		if( ret )
		{
			CString err;
			LuaManager::PopStack( err );
			CString sError = ssprintf( "Lua runtime error evaluating \"%s\": %s", str.c_str(), err.c_str() );
			Dialog::OK( sError, "LUA_ERROR" );
			return false;
		}

		ASSERT_M( lua_gettop(L) == 1, ssprintf("%i", lua_gettop(L)) );

		/* Don't accept a function as a return value; if you really want to use a function
		 * as a boolean, convert it before returning. */
		if( lua_isfunction( L, -1 ) )
			RageException::Throw( "result is a function; did you forget \"()\"?" );
	}

	return true;
}

bool LuaManager::RunExpressionB( const CString &str )
{
	if( !RunExpression( str ) )
		return false;

	bool result = !!lua_toboolean( L, -1 );
	lua_pop( L, -1 );

	return result;
}

float LuaManager::RunExpressionF( const CString &str )
{
	if( !RunExpression( str ) )
		return 0;

	float result = (float) lua_tonumber( L, -1 );
	lua_pop( L, -1 );

	return result;
}

bool LuaManager::RunExpressionS( const CString &str, CString &sOut )
{
	if( !RunExpression( str ) )
		return false;

	ASSERT( lua_gettop(L) > 0 );

	sOut = lua_tostring( L, -1 );
	lua_pop( L, -1 );

	return true;
}

bool LuaManager::RunAtExpression( CString &sStr )
{
	if( sStr.size() == 0 || sStr[0] != '@' )
		return false;

	/* Erase "@". */
	sStr.erase( 0, 1 );

	CString sOut;
	RunExpressionS( sStr, sOut );
	sStr = sOut;
	return true;
}

void LuaManager::Fail( const CString &err )
{
	lua_pushstring( L, err );
	lua_error( L );
}


LuaFunctionList::LuaFunctionList( CString name_, lua_CFunction func_ )
{
	name = name_;
	func = func_;
	next = g_LuaFunctions;
	g_LuaFunctions = this;
}


LuaFunction_NoArgs( MonthOfYear, GetLocalTime().tm_mon+1 );
LuaFunction_NoArgs( DayOfMonth, GetLocalTime().tm_mday );
LuaFunction_NoArgs( Hour, GetLocalTime().tm_hour );
LuaFunction_NoArgs( Minute, GetLocalTime().tm_min );
LuaFunction_NoArgs( Second, GetLocalTime().tm_sec );
LuaFunction_NoArgs( Year, GetLocalTime().tm_year+1900 );
LuaFunction_NoArgs( Weekday, GetLocalTime().tm_wday );
LuaFunction_NoArgs( DayOfYear, GetLocalTime().tm_yday );

LuaFunction_Str( Trace, (LOG->Trace("%s", str.c_str()),true) );

/*
 * (c) 2004 Glenn Maynard
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
