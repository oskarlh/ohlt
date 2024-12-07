#include "qrad.h"
#include "meshtrace.h"
#include "filelib.h"

#define MAX_MODELS		1024

model_t models[MAX_MODELS];
int num_models;

static void LoadStudioModel( const char *modelname, const vec3_array& origin, const vec3_array& angles, const vec3_array& scale, int body, int skin, int trace_mode )
{
	if( num_models >= MAX_MODELS )
	{
		Developer( DEVELOPER_LEVEL_ERROR, "LoadStudioModel: MAX_MODELS exceeded\n" );
		return;
	}
	model_t *m = &models[num_models];
	snprintf(m->name, sizeof(m->name), "%s%s", g_Wadpath, modelname);
	FlipSlashes(m->name);

	if (!std::filesystem::exists(m->name))
	{
		Warning("LoadStudioModel: couldn't load %s\n", m->name);
		return;
	}
	LoadFile(m->name, (char**)&m->extradata);

	studiohdr_t *phdr = (studiohdr_t *)m->extradata;

	// well the textures place in separate file (very stupid case)
	if( phdr->numtextures == 0 )
	{
		char texname[128], texpath[128];
		byte *texdata, *moddata;
		studiohdr_t *thdr, *newhdr;
		safe_strncpy(texname, modelname, 128);
		StripExtension(texname);

		snprintf(texpath, sizeof(texpath), "%s%sT.mdl", g_Wadpath, texname);
		FlipSlashes(texpath);

		LoadFile(texpath, (char**)&texdata);
		moddata = (byte *)m->extradata;
		phdr = (studiohdr_t *)moddata;

		thdr = (studiohdr_t *)texdata;

		// merge textures with main model buffer
		m->extradata = malloc( phdr->length + thdr->length - sizeof( studiohdr_t ));	// we don't need two headers
		memcpy( m->extradata, moddata, phdr->length );
		memcpy( (byte *)m->extradata + phdr->length, texdata + sizeof( studiohdr_t ), thdr->length - sizeof( studiohdr_t ));

		// merge header
		newhdr = (studiohdr_t *)m->extradata;

		newhdr->numskinfamilies = thdr->numskinfamilies;
		newhdr->numtextures = thdr->numtextures;
		newhdr->numskinref = thdr->numskinref;
		newhdr->textureindex = phdr->length;
		newhdr->skinindex = newhdr->textureindex + ( newhdr->numtextures * sizeof( mstudiotexture_t ));
		newhdr->texturedataindex = newhdr->skinindex + (newhdr->numskinfamilies * newhdr->numskinref * sizeof( short ));
		newhdr->length = phdr->length + thdr->length - sizeof( studiohdr_t );

		// and finally merge datapointers for textures
		for( int i = 0; i < newhdr->numtextures; i++ )
		{
			mstudiotexture_t *ptexture = (mstudiotexture_t *)(((byte *)newhdr) + newhdr->textureindex);
			ptexture[i].index += ( phdr->length - sizeof( studiohdr_t ));
//			printf( "Texture %i [%s]\n", i, ptexture[i].name );
			// now we can replace offsets with real pointers
//			ptexture[i].pixels = (byte *)newhdr + ptexture[i].index;
		}

		free( moddata );
		free( texdata );
	}
	else
	{
#if 0
		for( int i = 0; i < phdr->numtextures; i++ )
		{
			mstudiotexture_t *ptexture = (mstudiotexture_t *)(((byte *)phdr) + phdr->textureindex);
			// now we can replace offsets with real pointers
			ptexture[i].pixels = (byte *)phdr + ptexture[i].index;
		}
#endif
	}

	VectorCopy( origin, m->origin );
	VectorCopy( angles, m->angles );
	VectorCopy( scale, m->scale );

	m->trace_mode = trace_mode;

	m->body = body;
	m->skin = skin;

	m->mesh.StudioConstructMesh( m );

	num_models++;
}

// =====================================================================================
//  LoadStudioModels
// =====================================================================================
void LoadStudioModels( void )
{
	memset( models, 0, sizeof( models ));
	num_models = 0;

	if( !g_studioshadow ) return;

	for( int i = 0; i < g_numentities; i++ )
	{
		const char *name, *model;
		vec3_array origin, angles;

		entity_t* e = &g_entities[i];
		name = (const char*) ValueForKey( e, u8"classname" );

		if( !strcasecmp( name, "env_static" ))
		{
			int spawnflags = IntForKey( e, u8"spawnflags" );
			if( spawnflags & 4 ) continue; // shadow disabled
		
			model = (const char*) ValueForKey( e, u8"model" );

			if( !model || !*model )
			{
				Developer( DEVELOPER_LEVEL_WARNING, "env_static has empty model field\n" );
				continue;
			}
		}
		else if( IntForKey( e, u8"zhlt_studioshadow" ))
		{
			model = (const char*) ValueForKey( e, u8"model" );

			if( !model || !*model )
				continue;
		}
		else
		{
			continue;
		}

		GetVectorForKey( e, u8"origin", origin );
		GetVectorForKey( e, u8"angles", angles );

		angles[0] = -angles[0]; // Stupid quake bug workaround
		int trace_mode = SHADOW_NORMAL;	// default mode

		// make sure what field is present
		if( strcmp( (const char*) ValueForKey( e, u8"zhlt_shadowmode" ), "" ))
			trace_mode = IntForKey( e, u8"zhlt_shadowmode" );

		int body = IntForKey( e, u8"body" );
		int skin = IntForKey( e, u8"skin" );

		float scale = FloatForKey( e, u8"scale" );
		vec3_array xform;

		GetVectorForKey( e, u8"xform", xform );

		if( VectorCompare( xform, vec3_origin ))
			VectorFill( xform, scale );

		// check xform values
		if( xform[0] < 0.01f ) xform[0] = 1.0f;
		if( xform[1] < 0.01f ) xform[1] = 1.0f;
		if( xform[2] < 0.01f ) xform[2] = 1.0f;
		if( xform[0] > 16.0f ) xform[0] = 16.0f;
		if( xform[1] > 16.0f ) xform[1] = 16.0f;
		if( xform[2] > 16.0f ) xform[2] = 16.0f;

		LoadStudioModel( model, origin, angles, xform, body, skin, trace_mode );
	}

	Log( "%i opaque studio models\n", num_models );
}

void FreeStudioModels( void )
{
	for( int i = 0; i < num_models; i++ )
	{
		model_t *m = &models[i];

		// first, delete the mesh
		m->mesh.FreeMesh();

		// unload the model
		Free( m->extradata );
	}

	memset( models, 0, sizeof( models ));
	num_models = 0;
}

static void MoveBounds( const vec3_array& start, const vec3_array& mins, const vec3_array& maxs, const vec3_array& end, vec3_array& outmins, vec3_array& outmaxs )
{
	for( int i = 0; i < 3; i++ )
	{
		if( end[i] > start[i] )
		{
			outmins[i] = start[i] + mins[i] - 1.0f;
			outmaxs[i] = end[i] + maxs[i] + 1.0f;
		}
		else
		{
			outmins[i] = end[i] + mins[i] - 1.0f;
			outmaxs[i] = start[i] + maxs[i] + 1.0f;
		}
	}
}

bool TestSegmentAgainstStudioList( const vec3_array& p1, const vec3_array& p2 )
{
	if( !num_models ) return false; // easy out

	vec3_array	trace_mins, trace_maxs;

	MoveBounds( p1, vec3_origin, vec3_origin, p2, trace_mins, trace_maxs );

	for( int i = 0; i < num_models; i++ )
	{
		model_t *m = &models[i];

		mmesh_t *pMesh = m->mesh.GetMesh();
		areanode_t *pHeadNode = m->mesh.GetHeadNode();

		if( !pMesh || !m->mesh.Intersect( trace_mins.data(), trace_maxs.data() ))
			continue; // bad model or not intersect with trace

		TraceMesh	trm;	// a name like Doom3 :-)

		trm.SetTraceModExtradata( m->extradata );
		trm.SetTraceMesh( pMesh, pHeadNode );
		trm.SetupTrace( p1.data(), vec3_origin, vec3_origin, p2.data() );

		if( trm.DoTrace())
			return true; // we hit studio model
	}

	return false;
}
