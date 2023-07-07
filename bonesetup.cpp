#include "includes.h"

Bones g_bones{};;

bool Bones::setup( Player* player, BoneArray* out, LagRecord* record ) {
	// if the record isnt setup yet.
	if( !record->m_setup ) {
		// run setupbones rebuilt.
		if( !BuildBones( player, 0x7FF00, record->m_bones, record ) )
			return false;

		// we have setup this record bones.
		record->m_setup = true;
	}

	// record is setup.
	if( out && record->m_setup )
		std::memcpy( out, record->m_bones, sizeof( BoneArray ) * 128 );

	return true;
}

bool Bones::BuildBones( Player* target, int mask, BoneArray* out, LagRecord* record ) {
	target->InvalidateBoneCache( );

	// BuildTransformations Fix (credits: polak)
	static auto r_jiggle_bones = g_csgo.m_cvar->FindVar( HASH( "r_jiggle_bones" ) )->GetInt( );
	g_csgo.m_cvar->FindVar( HASH( "r_jiggle_bones" ) )->SetValue( 0 );

	int effects = target->m_fEffects( );
	target->AddEffect( 8 );

	m_running = true;
	bool ret = target->SetupBones( out, 128, mask, record->m_sim_time );
	m_running = false;

	target->m_fEffects( ) = effects;

	g_csgo.m_cvar->FindVar( HASH( "r_jiggle_bones" ) )->SetValue( r_jiggle_bones );

	return ret;
}