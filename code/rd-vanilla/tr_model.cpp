/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// tr_models.c -- model loading and caching

#include "../server/exe_headers.h"

#include "tr_common.h"
#include "tr_local.h"
#include "qcommon/matcomp.h"
#include "../qcommon/sstring.h"

#define	LL(x) x=LittleLong(x)
#define	LS(x) x=LittleShort(x)
#define	LF(x) x=LittleFloat(x)

void RE_LoadWorldMap_Actual(const char* name, world_t& world_data, int index); //should only be called for sub-bsp instances

static qboolean R_LoadMD3(model_t* mod, int lod, void* buffer, const char* mod_name, qboolean& b_already_cached);

/*
Ghoul2 Insert Start
*/

using modelHash_t = struct modelHash_s
{
	char		name[MAX_QPATH];
	qhandle_t	handle;
	modelHash_s* next;
};

#define FILE_HASH_SIZE		1024
modelHash_t* mhHashTable[FILE_HASH_SIZE];

/*
Ghoul2 Insert End
*/

// This stuff looks a bit messy, but it's kept here as black box, and nothing appears in any .H files for other
//	modules to worry about. I may make another module for this sometime.
//
using StringOffsetAndShaderIndexDest_t = std::pair<int, int>;
using ShaderRegisterData_t = std::vector <StringOffsetAndShaderIndexDest_t>;
struct CachedEndianedModelBinary_s
{
	void* pModelDiskImage;
	int		iAllocSize;		// may be useful for mem-query, but I don't actually need it
	ShaderRegisterData_t ShaderRegisterData;

	int		iLastLevelUsedOn;

	CachedEndianedModelBinary_s()
	{
		pModelDiskImage = nullptr;
		iLastLevelUsedOn = -1;
		iAllocSize = 0;
		ShaderRegisterData.clear();
	}
};
using CachedEndianedModelBinary_t = CachedEndianedModelBinary_s;
using CachedModels_t = std::map <sstring_t, CachedEndianedModelBinary_t>;
CachedModels_t* CachedModels = nullptr;	// the important cache item.

void RE_RegisterModels_StoreShaderRequest(const char* ps_model_file_name, const char* ps_shader_name, const int* pi_shader_index_poke)
{
	char s_model_name[MAX_QPATH];

	Q_strncpyz(s_model_name, ps_model_file_name, sizeof s_model_name);
	Q_strlwr(s_model_name);

	CachedEndianedModelBinary_t& model_bin = (*CachedModels)[s_model_name];

	if (model_bin.pModelDiskImage == nullptr)
	{
		assert(0);	// should never happen, means that we're being called on a model that wasn't loaded
	}
	else
	{
		const int i_name_offset = ps_shader_name - static_cast<char*>(model_bin.pModelDiskImage);
		const int i_poke_offset = (char*)pi_shader_index_poke - static_cast<char*>(model_bin.pModelDiskImage);

		model_bin.ShaderRegisterData.emplace_back(i_name_offset, i_poke_offset);
	}
}

static const byte FakeGLAFile[] =
{
0x32, 0x4C, 0x47, 0x41, 0x06, 0x00, 0x00, 0x00, 0x2A, 0x64, 0x65, 0x66, 0x61, 0x75, 0x6C, 0x74,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x01, 0x00, 0x00, 0x00,
0x14, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00,
0x26, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x4D, 0x6F, 0x64, 0x56, 0x69, 0x65, 0x77, 0x20,
0x69, 0x6E, 0x74, 0x65, 0x72, 0x6E, 0x61, 0x6C, 0x20, 0x64, 0x65, 0x66, 0x61, 0x75, 0x6C, 0x74,
0x00, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD,
0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD,
0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFD, 0xBF, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE, 0x7F,
0x00, 0x80, 0x00, 0x80, 0x00, 0x80
};

// returns qtrue if loaded, and sets the supplied qbool to true if it was from cache (instead of disk)
//   (which we need to know to avoid LittleLong()ing everything again (well, the Mac needs to know anyway)...
//
qboolean RE_RegisterModels_GetDiskFile(const char* ps_model_file_name, void** ppv_buffer, qboolean* pqb_already_cached)
{
	char s_model_name[MAX_QPATH];

	Q_strncpyz(s_model_name, ps_model_file_name, sizeof s_model_name);
	Q_strlwr(s_model_name);

	const CachedEndianedModelBinary_t& ModelBin = (*CachedModels)[s_model_name];

	if (ModelBin.pModelDiskImage == nullptr)
	{
		// didn't have it cached, so try the disk...
		//

			// special case intercept first...
			//
		if (strcmp(sDEFAULT_GLA_NAME ".gla", ps_model_file_name) == 0)
		{
			// return fake params as though it was found on disk...
			//
			void* pv_fake_gla_file = R_Malloc(sizeof FakeGLAFile, TAG_FILESYS, qfalse);
			memcpy(pv_fake_gla_file, &FakeGLAFile[0], sizeof FakeGLAFile);
			*ppv_buffer = pv_fake_gla_file;
			*pqb_already_cached = qfalse;	// faking it like this should mean that it works fine on the Mac as well
			return qtrue;
		}

		ri.FS_ReadFile(s_model_name, ppv_buffer);
		*pqb_already_cached = qfalse;

		return static_cast<qboolean>(*ppv_buffer != nullptr);
	}
	*ppv_buffer = ModelBin.pModelDiskImage;
	*pqb_already_cached = qtrue;
	return qtrue;
}

// if return == true, no further action needed by the caller...
//
void* RE_RegisterModels_Malloc(const int i_size, void* pv_disk_buffer_if_just_loaded, const char* ps_model_file_name, qboolean* pqb_already_found, const memtag_t e_tag)
{
	char s_model_name[MAX_QPATH];

	Q_strncpyz(s_model_name, ps_model_file_name, sizeof s_model_name);
	Q_strlwr(s_model_name);

	CachedEndianedModelBinary_t& model_bin = (*CachedModels)[s_model_name];

	if (model_bin.pModelDiskImage == nullptr)
	{
		// ... then this entry has only just been created, ie we need to load it fully...
		//
		// new, instead of doing a R_Malloc and assigning that we just morph the disk buffer alloc
		//	then don't thrown it away on return - cuts down on mem overhead
		//
		// ... groan, but not if doing a limb hierarchy creation (some VV stuff?), in which case it's NULL
		//
		if (pv_disk_buffer_if_just_loaded)
		{
			R_MorphMallocTag(pv_disk_buffer_if_just_loaded, e_tag);
		}
		else
		{
			pv_disk_buffer_if_just_loaded = R_Malloc(i_size, e_tag, qfalse);
		}

		model_bin.pModelDiskImage = pv_disk_buffer_if_just_loaded;
		model_bin.iAllocSize = i_size;
		*pqb_already_found = qfalse;
	}
	else
	{
		// if we already had this model entry, then re-register all the shaders it wanted...
		//
		const int i_entries = model_bin.ShaderRegisterData.size();
		for (int i = 0; i < i_entries; i++)
		{
			const int i_shader_name_offset = model_bin.ShaderRegisterData[i].first;
			const int i_shader_poke_offset = model_bin.ShaderRegisterData[i].second;

			const char* const ps_shader_name = &static_cast<char*>(model_bin.pModelDiskImage)[i_shader_name_offset];
			const auto pi_shader_poke_ptr = reinterpret_cast<int*>(&static_cast<char*>(model_bin.pModelDiskImage)[i_shader_poke_offset]);

			const shader_t* sh = R_FindShader(ps_shader_name, lightmapsNone, stylesDefault, qtrue);

			if (sh->defaultShader)
			{
				*pi_shader_poke_ptr = 0;
			}
			else {
				*pi_shader_poke_ptr = sh->index;
			}
		}
		*pqb_already_found = qtrue;	// tell caller not to re-Endian or re-Shader this binary
	}

	model_bin.iLastLevelUsedOn = RE_RegisterMedia_GetLevel();

	return model_bin.pModelDiskImage;
}

// dump any models not being used by this level if we're running low on memory...
//
static int GetModelDataAllocSize()
{
	return	R_MemSize(TAG_MODEL_MD3) +
		R_MemSize(TAG_MODEL_GLM) +
		R_MemSize(TAG_MODEL_GLA);
}
extern cvar_t* r_modelpoolmegs;
//
// return qtrue if at least one cached model was freed (which tells z_malloc()-fail recovery code to try again)
//
extern qboolean gbInsideRegisterModel;
qboolean RE_RegisterModels_LevelLoadEnd(const qboolean b_delete_everything_not_used_this_level /* = qfalse */)
{
	qboolean bAtLeastoneModelFreed = qfalse;

	if (gbInsideRegisterModel)
	{
		Com_DPrintf("(Inside RE_RegisterModel (z_malloc recovery?), exiting...\n");
	}
	else
	{
		int i_loaded_model_bytes = GetModelDataAllocSize();
		const int i_max_model_bytes = r_modelpoolmegs->integer * 1024 * 1024;

		for (auto it_model = CachedModels->begin(); it_model != CachedModels->end() && (b_delete_everything_not_used_this_level || i_loaded_model_bytes > i_max_model_bytes); )
		{
			const CachedEndianedModelBinary_t& cached_model = (*it_model).second;

			qboolean b_delete_this;

			if (b_delete_everything_not_used_this_level)
			{
				b_delete_this = static_cast<qboolean>(cached_model.iLastLevelUsedOn != RE_RegisterMedia_GetLevel());
			}
			else
			{
				b_delete_this = static_cast<qboolean>(cached_model.iLastLevelUsedOn < RE_RegisterMedia_GetLevel());
			}

			// if it wasn't used on this level, dump it...
			//
			if (b_delete_this)
			{
#ifdef _DEBUG
				//				LPCSTR psModelName = (*itModel).first.c_str();
				//				ri.Printf( PRINT_DEVELOPER, "Dumping \"%s\"", psModelName);
				//				ri.Printf( PRINT_DEVELOPER, ", used on lvl %d\n",CachedModel.iLastLevelUsedOn);
#endif

				if (cached_model.pModelDiskImage) {
					R_Free(cached_model.pModelDiskImage);
					//CachedModel.pModelDiskImage = NULL;	// REM for reference, erase() call below negates the need for it.
					bAtLeastoneModelFreed = qtrue;
				}
				CachedModels->erase(it_model++);

				i_loaded_model_bytes = GetModelDataAllocSize();
			}
			else
			{
				++it_model;
			}
		}
	}

	//ri.Printf( PRINT_DEVELOPER, "RE_RegisterModels_LevelLoadEnd(): Ok\n");

	return bAtLeastoneModelFreed;
}

void RE_RegisterModels_Info_f()
{
	int i_total_bytes = 0;
	if (!CachedModels) {
		Com_Printf("%d bytes total (%.2fMB)\n", i_total_bytes, static_cast<float>(i_total_bytes) / 1024.0f / 1024.0f);
		return;
	}

	const int i_models = CachedModels->size();
	int i_model = 0;

	for (auto it_model = CachedModels->begin(); it_model != CachedModels->end(); ++it_model, i_model++)
	{
		const CachedEndianedModelBinary_t& CachedModel = (*it_model).second;

		ri.Printf(PRINT_ALL, "%d/%d: \"%s\" (%d bytes)", i_model, i_models, (*it_model).first.c_str(), CachedModel.iAllocSize);

#ifdef _DEBUG
		ri.Printf(PRINT_ALL, ", lvl %d\n", CachedModel.iLastLevelUsedOn);
#endif

		i_total_bytes += CachedModel.iAllocSize;
	}
	ri.Printf(PRINT_ALL, "%d bytes total (%.2fMB)\n", i_total_bytes, static_cast<float>(i_total_bytes) / 1024.0f / 1024.0f);
}

static void RE_RegisterModels_DeleteAll()
{
	if (!CachedModels) {
		return;	//argh!
	}

	for (auto it_model = CachedModels->begin(); it_model != CachedModels->end(); )
	{
		const CachedEndianedModelBinary_t& CachedModel = (*it_model).second;

		if (CachedModel.pModelDiskImage) {
			R_Free(CachedModel.pModelDiskImage);
		}

		CachedModels->erase(it_model++);
	}

	extern void RE_AnimationCFGs_DeleteAll();
	RE_AnimationCFGs_DeleteAll();
}

static int giRegisterMedia_CurrentLevel = 0;
static qboolean gbAllowScreenDissolve = qtrue;
//
// param "bAllowScreenDissolve" is just a convenient way of getting hold of a bool which can be checked by the code that
//	issues the InitDissolve command later in RE_RegisterMedia_LevelLoadEnd()
//
void RE_RegisterMedia_LevelLoadBegin(const char* ps_map_name, const ForceReload_e e_force_reload, const qboolean b_allow_screen_dissolve)
{
	gbAllowScreenDissolve = b_allow_screen_dissolve;

	tr.numBSPModels = 0;

	// for development purposes we may want to ditch certain media just before loading a map...
	//
	switch (e_force_reload)
	{
	case eForceReload_BSP:

		ri.CM_DeleteCachedMap(qtrue);
		R_Images_DeleteLightMaps();
		break;

	case eForceReload_MODELS:

		RE_RegisterModels_DeleteAll();
		break;

	case eForceReload_ALL:

		// BSP...
		//
		ri.CM_DeleteCachedMap(qtrue);
		R_Images_DeleteLightMaps();
		//
		// models...
		//
		RE_RegisterModels_DeleteAll();
		break;
	default:
		break;
	}

	// at some stage I'll probably want to put some special logic here, like not incrementing the level number
	//	when going into a map like "brig" or something, so returning to the previous level doesn't require an
	//	asset reload etc, but for now...
	//
	// only bump level number if we're not on the same level.
	//	Note that this will hide uncached models, which is perhaps a bad thing?...
	//
	static char s_prev_map_name[MAX_QPATH] = { 0 };
	if (Q_stricmp(ps_map_name, s_prev_map_name))
	{
		Q_strncpyz(s_prev_map_name, ps_map_name, sizeof s_prev_map_name);
		giRegisterMedia_CurrentLevel++;
	}
}

int RE_RegisterMedia_GetLevel()
{
	return giRegisterMedia_CurrentLevel;
}

void RE_RegisterMedia_LevelLoadEnd()
{
	RE_RegisterModels_LevelLoadEnd(qfalse);
	RE_RegisterImages_LevelLoadEnd();
	ri.SND_RegisterAudio_LevelLoadEnd(qfalse);

	if (gbAllowScreenDissolve)
	{
		RE_InitDissolve(qfalse);
	}

	ri.S_RestartMusic();

	*ri.gbAlreadyDoingLoad() = qfalse;
}

/*
** R_GetModelByHandle
*/
model_t* R_GetModelByHandle(const qhandle_t index) {
	// out of range gets the defualt model
	if (index < 1 || index >= tr.numModels) {
		return tr.models[0];
	}

	model_t* mod = tr.models[index];

	return mod;
}

/*
** R_GetAnimModelByHandle
*/
model_t* R_GetAnimModelByHandle(const CGhoul2Info* ghl_info, qhandle_t index)
{
	// out of range gets the defualt model
	if (index < 1 || index > tr.numModels)
	{
		return tr.models[0];
	}

	model_t* mod;

	if (ghl_info->animModelIndexOffset)
	{
		// Have to recalculate offset to get map animations for JKA Campaign
		index -= ghl_info->animModelIndexOffset;
		int map_index = 0;
		constexpr int len = std::size(tr.models);

		for (int i = 0; i < len; i++)
		{
			if (!Q_stricmp(va("models/players/_humanoid/_humanoid.gla"), tr.models[i]->name))
			{
				map_index = i + 1;
				break;
			}
		}

		// Custom skeletons will be further along than the base _humanoid, don't modify for normal JKA skeletons
		if (index > map_index)
		{
			const int off_set = index - map_index;
			mod = tr.models[index - off_set];
		}
		else
		{
			mod = tr.models[index + ghl_info->animModelIndexOffset];
		}
	}
	else
	{
		mod = tr.models[index];
	}

	return mod;
}

//===============================================================================

/*
** R_AllocModel
*/
model_t* R_AllocModel() {
	if (tr.numModels == MAX_MOD_KNOWN) {
		return nullptr;
	}

	const auto mod = static_cast<model_t*>(R_Hunk_Alloc(sizeof * tr.models[tr.numModels], qtrue));
	mod->index = tr.numModels;
	tr.models[tr.numModels] = mod;
	tr.numModels++;

	return mod;
}

/*
Ghoul2 Insert Start
*/

/*
================
return a hash value for the filename
================
*/
static long generateHashValue(const char* fname, const int size) {
	long hash = 0;
	int i = 0;
	while (fname[i] != '\0') {
		char letter = tolower(fname[i]);
		if (letter == '.') break;				// don't include extension
		if (letter == '\\') letter = '/';		// damn path names
		hash += static_cast<long>(letter) * (i + 119);
		i++;
	}
	hash &= size - 1;
	return hash;
}

void RE_InsertModelIntoHash(const char* name, const model_t* mod)
{
	const int hash = generateHashValue(name, FILE_HASH_SIZE);

	// insert this file into the hash table so we can look it up faster later
	const auto mh = static_cast<modelHash_t*>(R_Hunk_Alloc(sizeof(modelHash_t), qtrue));

	mh->next = mhHashTable[hash];	// I have the breakpoint triggered here where mhHashTable[986] would be assigned
	mh->handle = mod->index;
	strcpy(mh->name, name);
	mhHashTable[hash] = mh;
}
/*
Ghoul2 Insert End
*/

/*
====================
RE_RegisterModel

Loads in a model for the given name

Zero will be returned if the model fails to load.
An entry will be retained for failed models as an
optimization to prevent disk rescanning if they are
asked for again.
====================
*/
static qhandle_t RE_RegisterModel_Actual(const char* name)
{
	model_t* mod;
	unsigned* buf = nullptr;
	int			lod;
	qboolean	loaded;
	modelHash_t* mh;
	/*
	Ghoul2 Insert End
	*/

	if (!name || !name[0]) {
		ri.Printf(PRINT_WARNING, "RE_RegisterModel: NULL name\n");
		return 0;
	}

	if (strlen(name) >= MAX_QPATH) {
		ri.Printf(PRINT_DEVELOPER, "Model name exceeds MAX_QPATH\n");
		return 0;
	}

	/*
	Ghoul2 Insert Start
	*/
	//	if (!tr.registered) {
	//		ri.Printf( PRINT_WARNING, "RE_RegisterModel (%s) called before ready!\n",name );
	//		return 0;
	//	}
		//
		// search the currently loaded models
		//

	int hash = generateHashValue(name, FILE_HASH_SIZE);

	//
	// see if the model is already loaded
	//_
	for (mh = mhHashTable[hash]; mh; mh = mh->next) {
		if (Q_stricmp(mh->name, name) == 0) {
			if (tr.models[mh->handle]->type == MOD_BAD)
			{
				return 0;
			}
			return mh->handle;
		}
	}

	/*
	Ghoul2 Insert End
	*/

	if (name[0] == '#')
	{
		char		temp[MAX_QPATH];

		tr.numBSPModels++;
#ifndef DEDICATED
		RE_LoadWorldMap_Actual(va("maps/%s.bsp", name + 1), tr.bspModels[tr.numBSPModels - 1], tr.numBSPModels);	//this calls R_LoadSubmodels which will put them into the Hash
#endif
		Com_sprintf(temp, MAX_QPATH, "*%d-0", tr.numBSPModels);
		hash = generateHashValue(temp, FILE_HASH_SIZE);
		for (mh = mhHashTable[hash]; mh; mh = mh->next)
		{
			if (Q_stricmp(mh->name, temp) == 0)
			{
				return mh->handle;
			}
		}

		return 0;
	}

	// allocate a new model_t

	if ((mod = R_AllocModel()) == nullptr) {
		ri.Printf(PRINT_WARNING, "RE_RegisterModel: R_AllocModel() failed for '%s'\n", name);
		return 0;
	}

	// only set the name after the model has been successfully loaded
	Q_strncpyz(mod->name, name, sizeof mod->name);

	// make sure the render thread is stopped
	R_IssuePendingRenderCommands(); //

	int i_lod_start = 0;
	if (strstr(name, ".md3")) {
		i_lod_start = MD3_MAX_LODS - 1;	//this loads the md3s in reverse so they can be biased
	}
	mod->numLods = 0;

	//
	// load the files
	//
	int num_loaded = 0;

	for (lod = i_lod_start; lod >= 0; lod--) {
		char filename[1024];

		strcpy(filename, name);

		if (lod != 0) {
			char namebuf[80];

			if (strrchr(filename, '.')) {
				*strrchr(filename, '.') = 0;
			}
			sprintf(namebuf, "_%d.md3", lod);
			strcat(filename, namebuf);
		}

		qboolean b_already_cached = qfalse;
		if (!RE_RegisterModels_GetDiskFile(filename, reinterpret_cast<void**>(&buf), &b_already_cached))
		{
			if (num_loaded)	//we loaded one already, but a higher LOD is missing!
			{
				Com_Error(ERR_DROP, "R_LoadMD3: %s has LOD %d but is missing LOD %d ('%s')!", mod->name, lod + 1, lod, filename);
			}
			continue;
		}

		//loadmodel = mod;	// this seems to be fairly pointless

		// important that from now on we pass 'filename' instead of 'name' to all model load functions,
		//	because 'filename' accounts for any LOD mangling etc so guarantees unique lookups for yet more
		//	internal caching...
		//
		int ident = *buf;
		if (!b_already_cached)
		{
			ident = LittleLong ident;
		}

		switch (ident)
		{
			// if you add any new types of model load in this switch-case, tell me,
			//	or copy what I've done with the cache scheme (-ste).
			//
		case MDXA_IDENT:

			loaded = R_LoadMDXA(mod, buf, filename, b_already_cached);
			break;

		case MDXM_IDENT:

			loaded = R_LoadMDXM(mod, buf, filename, b_already_cached);
			break;

		case MD3_IDENT:

			loaded = R_LoadMD3(mod, lod, buf, filename, b_already_cached);
			break;

		default:

			ri.Printf(PRINT_WARNING, "RE_RegisterModel: unknown fileid for %s\n", filename);
			goto fail;
		}

		if (!b_already_cached) {	// important to check!!
			ri.FS_FreeFile(buf);
		}

		if (!loaded) {
			if (lod == 0) {
				ri.Printf(PRINT_WARNING, "RE_RegisterModel: cannot load %s\n", filename);
				goto fail;
			}
			break;
		}
		mod->numLods++;
		num_loaded++;
		// if we have a valid model and are biased
		// so that we won't see any higher detail ones,
		// stop loading them
		if (lod <= r_lodbias->integer) {
			break;
		}
	}

	if (num_loaded) {
		// duplicate into higher lod spots that weren't
		// loaded, in case the user changes r_lodbias on the fly
		for (lod--; lod >= 0; lod--) {
			mod->numLods++;
			mod->md3[lod] = mod->md3[lod + 1];
		}
		/*
		Ghoul2 Insert Start
		*/

		RE_InsertModelIntoHash(name, mod);
		return mod->index;
		/*
		Ghoul2 Insert End
		*/
	}

fail:
	// we still keep the model_t around, so if the model name is asked for
	// again, we won't bother scanning the filesystem
	mod->type = MOD_BAD;
	RE_InsertModelIntoHash(name, mod);
	return 0;
}

// wrapper function needed to avoid problems with mid-function returns so I can safely use this bool to tell the
//	z_malloc-fail recovery code whether it's safe to ditch any model caches...
//
qboolean gbInsideRegisterModel = qfalse;
qhandle_t RE_RegisterModel(const char* name)
{
	gbInsideRegisterModel = qtrue;	// !!!!!!!!!!!!!!

	const qhandle_t q = RE_RegisterModel_Actual(name);

	if (Q_stricmp(&name[strlen(name) - 4], ".gla")) {
		gbInsideRegisterModel = qfalse;		// GLA files recursively call this, so don't turn off half way. A reference count would be nice, but if any ERR_DROP ever occurs within the load then the refcount will be knackered from then on
	}

	return q;
}

/*
=================
R_LoadMD3
=================
*/
static qboolean R_LoadMD3(model_t* mod, int lod, void* buffer, const char* mod_name, qboolean& b_already_cached) {
	int j;
	md3Header_t* pinmodel;
	md3Surface_t* surf;
	md3Shader_t* shader;
	int					version;
	int					size;

#ifdef Q3_BIG_ENDIAN
	md3Frame_t* frame;
	md3Triangle_t* tri;
	md3St_t* st;
	md3XyzNormal_t* xyz;
	md3Tag_t* tag;
#endif

	pinmodel = static_cast<md3Header_t*>(buffer);
	//
	// read some fields from the binary, but only LittleLong() them when we know this wasn't an already-cached model...
	//
	version = pinmodel->version;
	size = pinmodel->ofsEnd;

	if (!b_already_cached)
	{
		version = LittleLong version;
		size = LittleLong size;
	}

	if (version != MD3_VERSION) {
		ri.Printf(PRINT_WARNING, "R_LoadMD3: %s has wrong version (%i should be %i)\n",
			mod_name, version, MD3_VERSION);
		return qfalse;
	}

	mod->type = MOD_MESH;
	mod->dataSize += size;

	qboolean bAlreadyFound = qfalse;
	mod->md3[lod] = static_cast<md3Header_t*>(RE_RegisterModels_Malloc(size, buffer, mod_name, &bAlreadyFound, TAG_MODEL_MD3));

	assert(b_already_cached == bAlreadyFound);

	if (!bAlreadyFound)
	{
		// horrible new hackery, if !bAlreadyFound then we've just done a tag-morph, so we need to set the
		//	bool reference passed into this function to true, to tell the caller NOT to do an FS_Freefile since
		//	we've hijacked that memory block...
		//
		// Aaaargh. Kill me now...
		//
		b_already_cached = qtrue;
		assert(mod->md3[lod] == buffer);
		//		memcpy( mod->md3[lod], buffer, size );	// and don't do this now, since it's the same thing

		LL(mod->md3[lod]->ident);
		LL(mod->md3[lod]->version);
		LL(mod->md3[lod]->num_frames);
		LL(mod->md3[lod]->numTags);
		LL(mod->md3[lod]->numSurfaces);
		LL(mod->md3[lod]->ofsFrames);
		LL(mod->md3[lod]->ofsTags);
		LL(mod->md3[lod]->ofsSurfaces);
		LL(mod->md3[lod]->ofsEnd);
	}

	if (mod->md3[lod]->num_frames < 1) {
		ri.Printf(PRINT_WARNING, "R_LoadMD3: %s has no frames\n", mod_name);
		return qfalse;
	}

	if (bAlreadyFound)
	{
		return qtrue;	// All done. Stop, go no further, do not pass Go...
	}

#ifdef Q3_BIG_ENDIAN
	// swap all the frames
	frame = (md3Frame_t*)((byte*)mod->md3[lod] + mod->md3[lod]->ofsFrames);
	for (i = 0; i < mod->md3[lod]->num_frames; i++, frame++) {
		LF(frame->radius);
		for (j = 0; j < 3; j++) {
			LF(frame->bounds[0][j]);
			LF(frame->bounds[1][j]);
			LF(frame->localOrigin[j]);
		}
	}

	// swap all the tags
	tag = (md3Tag_t*)((byte*)mod->md3[lod] + mod->md3[lod]->ofsTags);
	for (i = 0; i < mod->md3[lod]->numTags * mod->md3[lod]->num_frames; i++, tag++) {
		for (j = 0; j < 3; j++) {
			LF(tag->origin[j]);
			LF(tag->axis[0][j]);
			LF(tag->axis[1][j]);
			LF(tag->axis[2][j]);
		}
	}
#endif

	// swap all the surfaces
	surf = reinterpret_cast<md3Surface_t*>(reinterpret_cast<byte*>(mod->md3[lod]) + mod->md3[lod]->ofsSurfaces);
	for (int i = 0; i < mod->md3[lod]->numSurfaces; i++) {
		LL(surf->flags);
		LL(surf->num_frames);
		LL(surf->numShaders);
		LL(surf->numTriangles);
		LL(surf->ofsTriangles);
		LL(surf->num_verts);
		LL(surf->ofsShaders);
		LL(surf->ofsSt);
		LL(surf->ofsXyzNormals);
		LL(surf->ofsEnd);

		if (surf->num_verts > SHADER_MAX_VERTEXES) {
			Com_Error(ERR_DROP, "R_LoadMD3: %s has more than %i verts on a surface (%i)",
				mod_name, SHADER_MAX_VERTEXES, surf->num_verts);
		}
		if (surf->numTriangles * 3 > SHADER_MAX_INDEXES) {
			Com_Error(ERR_DROP, "R_LoadMD3: %s has more than %i triangles on a surface (%i)",
				mod_name, SHADER_MAX_INDEXES / 3, surf->numTriangles);
		}

		// change to surface identifier
		surf->ident = SF_MD3;

		// lowercase the surface name so skin compares are faster
		Q_strlwr(surf->name);

		// strip off a trailing _1 or _2
		// this is a crutch for q3data being a mess
		j = strlen(surf->name);
		if (j > 2 && surf->name[j - 2] == '_') {
			surf->name[j - 2] = 0;
		}

		// register the shaders
		shader = reinterpret_cast<md3Shader_t*>(reinterpret_cast<byte*>(surf) + surf->ofsShaders);
		for (j = 0; j < surf->numShaders; j++, shader++) {
			const shader_t* sh = R_FindShader(shader->name, lightmapsNone, stylesDefault, qtrue);
			if (sh->defaultShader) {
				shader->shaderIndex = 0;
			}
			else {
				shader->shaderIndex = sh->index;
			}
			RE_RegisterModels_StoreShaderRequest(mod_name, &shader->name[0], &shader->shaderIndex);
		}

#ifdef Q3_BIG_ENDIAN
		// swap all the triangles
		tri = (md3Triangle_t*)((byte*)surf + surf->ofsTriangles);
		for (j = 0; j < surf->numTriangles; j++, tri++) {
			LL(tri->indexes[0]);
			LL(tri->indexes[1]);
			LL(tri->indexes[2]);
		}

		// swap all the ST
		st = (md3St_t*)((byte*)surf + surf->ofsSt);
		for (j = 0; j < surf->num_verts; j++, st++) {
			LF(st->st[0]);
			LF(st->st[1]);
		}

		// swap all the XyzNormals
		xyz = (md3XyzNormal_t*)((byte*)surf + surf->ofsXyzNormals);
		for (j = 0; j < surf->num_verts * surf->num_frames; j++, xyz++)
		{
			LS(xyz->xyz[0]);
			LS(xyz->xyz[1]);
			LS(xyz->xyz[2]);

			LS(xyz->normal);
		}
#endif

		// find the next surface
		surf = reinterpret_cast<md3Surface_t*>(reinterpret_cast<byte*>(surf) + surf->ofsEnd);
	}

	return qtrue;
}

//=============================================================================

/*
** RE_BeginRegistration
*/
void RE_BeginRegistration(glconfig_t* glconfig_out) {
	ri.Hunk_ClearToMark();

	R_Init();

	*glconfig_out = glConfig;

	R_IssuePendingRenderCommands();

	tr.viewCluster = -1;		// force markleafs to regenerate

	RE_ClearScene();

	tr.registered = qtrue;
}

//=============================================================================

/*
===============
R_ModelInit
===============
*/
void R_ModelInit()
{
	static CachedModels_t singleton;	// sorry vv, your dynamic allocation was a (false) memory leak
	CachedModels = &singleton;

	// leave a space for NULL model
	tr.numModels = 0;
	/*
	Ghoul2 Insert Start
	*/

	memset(mhHashTable, 0, sizeof mhHashTable);
	/*
	Ghoul2 Insert End
	*/

	model_t* mod = R_AllocModel();
	mod->type = MOD_BAD;
}

/*
================
R_Modellist_f
================
*/
void R_Modellist_f() {
	int j;
	int		lods;

	int total = 0;
	for (int i = 1; i < tr.numModels; i++) {
		model_t* mod = tr.models[i];
		switch (mod->type)
		{
		default:
			assert(0);
			ri.Printf(PRINT_ALL, "UNKNOWN  :      %s\n", mod->name);
			break;

		case MOD_BAD:
			ri.Printf(PRINT_ALL, "MOD_BAD  :      %s\n", mod->name);
			break;

		case MOD_BRUSH:
			ri.Printf(PRINT_ALL, "%8i : (%i) %s\n", mod->dataSize, mod->numLods, mod->name);
			break;

		case MOD_MDXA:

			ri.Printf(PRINT_ALL, "%8i : (%i) %s\n", mod->dataSize, mod->numLods, mod->name);
			break;

		case MOD_MDXM:

			ri.Printf(PRINT_ALL, "%8i : (%i) %s\n", mod->dataSize, mod->numLods, mod->name);
			break;

		case MOD_MESH:

			lods = 1;
			for (j = 1; j < MD3_MAX_LODS; j++) {
				if (mod->md3[j] && mod->md3[j] != mod->md3[j - 1]) {
					lods++;
				}
			}
			ri.Printf(PRINT_ALL, "%8i : (%i) %s\n", mod->dataSize, lods, mod->name);
			break;
		}
		total += mod->dataSize;
	}
	ri.Printf(PRINT_ALL, "%8i : Total models\n", total);

	/*	this doesn't work with the new hunks
		if ( tr.world ) {
			ri.Printf( PRINT_ALL, "%8i : %s\n", tr.world->dataSize, tr.world->name );
		} */
}

//=============================================================================

/*
================
R_GetTag for MD3s
================
*/
static md3Tag_t* R_GetTag(md3Header_t* mod, int frame, const char* tag_name) {
	if (frame >= mod->num_frames) {
		// it is possible to have a bad frame while changing models, so don't error
		frame = mod->num_frames - 1;
	}

	md3Tag_t* tag = reinterpret_cast<md3Tag_t*>(reinterpret_cast<byte*>(mod) + mod->ofsTags) + frame * mod->numTags;
	for (int i = 0; i < mod->numTags; i++, tag++) {
		if (strcmp(tag->name, tag_name) == 0) {
			return tag;	// found it
		}
	}

	return nullptr;
}

/*
================
R_LerpTag
================
*/
void	R_LerpTag(orientation_t* tag, const qhandle_t handle, const int start_frame, const int end_frame,
	const float frac, const char* tag_name) {
	md3Tag_t* start, * finish;

	const model_t* model = R_GetModelByHandle(handle);
	if (model->md3[0])
	{
		start = R_GetTag(model->md3[0], start_frame, tag_name);
		finish = R_GetTag(model->md3[0], end_frame, tag_name);
	}
	else
	{
		AxisClear(tag->axis);
		VectorClear(tag->origin);
		return;
	}

	if (!start || !finish) {
		AxisClear(tag->axis);
		VectorClear(tag->origin);
		return;
	}

	const float front_lerp = frac;
	const float back_lerp = 1.0 - frac;

	for (int i = 0; i < 3; i++) {
		tag->origin[i] = start->origin[i] * back_lerp + finish->origin[i] * front_lerp;
		tag->axis[0][i] = start->axis[0][i] * back_lerp + finish->axis[0][i] * front_lerp;
		tag->axis[1][i] = start->axis[1][i] * back_lerp + finish->axis[1][i] * front_lerp;
		tag->axis[2][i] = start->axis[2][i] * back_lerp + finish->axis[2][i] * front_lerp;
	}
	VectorNormalize(tag->axis[0]);
	VectorNormalize(tag->axis[1]);
	VectorNormalize(tag->axis[2]);
}

/*
====================
R_ModelBounds
====================
*/
void R_ModelBounds(const qhandle_t handle, vec3_t mins, vec3_t maxs) {
	const model_t* model = R_GetModelByHandle(handle);

	if (model->bmodel) {
		VectorCopy(model->bmodel->bounds[0], mins);
		VectorCopy(model->bmodel->bounds[1], maxs);
		return;
	}

	if (model->md3[0]) {
		md3Header_t* header = model->md3[0];

		const md3Frame_t* frame = reinterpret_cast<md3Frame_t*>(reinterpret_cast<byte*>(header) + header->ofsFrames);

		VectorCopy(frame->bounds[0], mins);
		VectorCopy(frame->bounds[1], maxs);
	}
	else
	{
		VectorClear(mins);
		VectorClear(maxs);
	}
}