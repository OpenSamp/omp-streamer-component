// Pawn AMX function-table indices.
//
// Historically this enum lived in SA-MP's plugincommon.h and identified slots in the
// pAMXFunctions pointer table that the server handed to each plugin. Under open.mp we get
// the same-layout table from IPawnComponent::getAmxFunctions(), so the indices below still
// apply unchanged. amxplugin.cpp uses them to dispatch every amx_* wrapper.
//
// The enum values must match what open.mp's Pawn component exposes in its amx-functions
// array (the first 44 entries are identical to SA-MP's; open.mp adds extras after index 43).

#pragma once

enum PLUGIN_AMX_EXPORT
{
	PLUGIN_AMX_EXPORT_Align16     = 0,
	PLUGIN_AMX_EXPORT_Align32     = 1,
	PLUGIN_AMX_EXPORT_Align64     = 2,
	PLUGIN_AMX_EXPORT_Allot       = 3,
	PLUGIN_AMX_EXPORT_Callback    = 4,
	PLUGIN_AMX_EXPORT_Cleanup     = 5,
	PLUGIN_AMX_EXPORT_Clone       = 6,
	PLUGIN_AMX_EXPORT_Exec        = 7,
	PLUGIN_AMX_EXPORT_FindNative  = 8,
	PLUGIN_AMX_EXPORT_FindPublic  = 9,
	PLUGIN_AMX_EXPORT_FindPubVar  = 10,
	PLUGIN_AMX_EXPORT_FindTagId   = 11,
	PLUGIN_AMX_EXPORT_Flags       = 12,
	PLUGIN_AMX_EXPORT_GetAddr     = 13,
	PLUGIN_AMX_EXPORT_GetNative   = 14,
	PLUGIN_AMX_EXPORT_GetPublic   = 15,
	PLUGIN_AMX_EXPORT_GetPubVar   = 16,
	PLUGIN_AMX_EXPORT_GetString   = 17,
	PLUGIN_AMX_EXPORT_GetTag      = 18,
	PLUGIN_AMX_EXPORT_GetUserData = 19,
	PLUGIN_AMX_EXPORT_Init        = 20,
	PLUGIN_AMX_EXPORT_InitJIT     = 21,
	PLUGIN_AMX_EXPORT_MemInfo     = 22,
	PLUGIN_AMX_EXPORT_NameLength  = 23,
	PLUGIN_AMX_EXPORT_NativeInfo  = 24,
	PLUGIN_AMX_EXPORT_NumNatives  = 25,
	PLUGIN_AMX_EXPORT_NumPublics  = 26,
	PLUGIN_AMX_EXPORT_NumPubVars  = 27,
	PLUGIN_AMX_EXPORT_NumTags     = 28,
	PLUGIN_AMX_EXPORT_Push        = 29,
	PLUGIN_AMX_EXPORT_PushArray   = 30,
	PLUGIN_AMX_EXPORT_PushString  = 31,
	PLUGIN_AMX_EXPORT_RaiseError  = 32,
	PLUGIN_AMX_EXPORT_Register    = 33,
	PLUGIN_AMX_EXPORT_Release     = 34,
	PLUGIN_AMX_EXPORT_SetCallback = 35,
	PLUGIN_AMX_EXPORT_SetDebugHook = 36,
	PLUGIN_AMX_EXPORT_SetString   = 37,
	PLUGIN_AMX_EXPORT_SetUserData = 38,
	PLUGIN_AMX_EXPORT_StrLen      = 39,
	PLUGIN_AMX_EXPORT_UTF8Check   = 40,
	PLUGIN_AMX_EXPORT_UTF8Get     = 41,
	PLUGIN_AMX_EXPORT_UTF8Len     = 42,
	PLUGIN_AMX_EXPORT_UTF8Put     = 43
};
