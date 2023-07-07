#include "includes.h"
#include "pred.h"

InputPrediction g_inputpred{};;

void InputPrediction::update( ) {
	bool        valid{ g_csgo.m_cl->m_delta_tick > 0 };

	// render start was not called.
	if( g_cl.m_stage == FRAME_NET_UPDATE_END ) {
		// EDIT; from what ive seen RunCommand is called when u call Prediction::Update
		// so the above code is not fucking needed.

		int start = g_csgo.m_cl->m_last_command_ack;
		int stop  = g_csgo.m_cl->m_last_outgoing_command + g_csgo.m_cl->m_choked_commands;

		// call CPrediction::Update.
		g_csgo.m_prediction->Update( g_csgo.m_cl->m_delta_tick, valid, start, stop );
	}
}

void InputPrediction::run( ) {
	static CMoveData data{};

	g_csgo.m_prediction->m_in_prediction = true;

	// CPrediction::StartCommand
	g_cl.m_local->m_pCurrentCommand( ) = g_cl.m_cmd;
	g_cl.m_local->m_PlayerCommand( )   = *g_cl.m_cmd;

	*g_csgo.m_nPredictionRandomSeed = g_cl.m_cmd->m_random_seed;
	g_csgo.m_pPredictionPlayer      = g_cl.m_local;

	// backup globals.
	m_curtime   = g_csgo.m_globals->m_curtime;
	m_frametime = g_csgo.m_globals->m_frametime;

	// CPrediction::RunCommand

	// set globals appropriately.
	g_csgo.m_globals->m_curtime   = game::TICKS_TO_TIME( g_cl.m_local->m_nTickBase( ) );
	g_csgo.m_globals->m_frametime = g_csgo.m_prediction->m_engine_paused ? 0.f : g_csgo.m_globals->m_interval;

	// set target player ( host ).
	g_csgo.m_move_helper->SetHost( g_cl.m_local );
	g_csgo.m_game_movement->StartTrackPredictionErrors( g_cl.m_local );

	// setup input.
	g_csgo.m_prediction->SetupMove( g_cl.m_local, g_cl.m_cmd, g_csgo.m_move_helper, &data );

	// run movement.
	g_csgo.m_game_movement->ProcessMovement( g_cl.m_local, &data );
	g_csgo.m_prediction->FinishMove( g_cl.m_local, g_cl.m_cmd, &data );
	g_csgo.m_game_movement->FinishTrackPredictionErrors( g_cl.m_local );

	// reset target player ( host ).
	g_csgo.m_move_helper->SetHost( nullptr );

	if ( g_cl.m_local->m_MoveType( ) == MOVETYPE_WALK && g_cl.m_local->GetSequenceActivity( g_cl.m_local->m_AnimOverlay( )[ 3 ].m_sequence ) == 979 ) {
		g_cl.m_local->m_AnimOverlay( )[ 3 ].m_weight = 0.f;
		g_cl.m_local->m_AnimOverlay( )[ 3 ].m_cycle = 0.f;
	}

	// call shoot pos.
	//g_cl.m_local->GetShootPosition( );
}

void InputPrediction::restore( ) {
	g_csgo.m_prediction->m_in_prediction = false;

	*g_csgo.m_nPredictionRandomSeed = -1;
	g_csgo.m_pPredictionPlayer      = nullptr;

	// restore globals.
	g_csgo.m_globals->m_curtime   = m_curtime;
	g_csgo.m_globals->m_frametime = m_frametime;
}

void InputPrediction::detect_prediction_error( ) {
	if ( !g_cl.m_local || !g_cl.m_processing )
		return;

	g_netdata.apply( );

	auto data = &g_netdata.m_data[ g_cl.m_local->m_nTickBase( ) % MULTIPLAYER_BACKUP ];
	auto viewPunch_delta = std::fabsf( g_cl.m_local->m_aimPunchAngle( ).x - data->m_punch.x );

	if ( viewPunch_delta <= 0.03125f )
		g_cl.m_local->m_aimPunchAngle( ).x = data->m_punch.x;

	int v34 = 1;
	int v35 = 1;
	int repredict = 0;
	int v37 = 1;

	if ( std::abs( g_cl.m_local->m_aimPunchAngle( ).x - data->m_punch.x ) > 0.03125f
		 || std::abs( g_cl.m_local->m_aimPunchAngle( ).y - data->m_punch.y ) > 0.03125f
		 || std::abs( g_cl.m_local->m_aimPunchAngle( ).z - data->m_punch.z ) > 0.03125f )
	{
		v34 = 0;
	}

	if ( v34 )
		g_cl.m_local->m_aimPunchAngle( ) = data->m_punch;
	else {
		data->m_punch = g_cl.m_local->m_aimPunchAngle( );
		repredict = 1;
	}

	if ( std::abs( g_cl.m_local->m_aimPunchAngleVel( ).x - data->m_punch_vel.x ) > 0.03125f
		 || std::abs( g_cl.m_local->m_aimPunchAngleVel( ).y - data->m_punch_vel.y ) > 0.03125f
		 || std::abs( g_cl.m_local->m_aimPunchAngleVel( ).z - data->m_punch_vel.z ) > 0.03125f )
	{
		v35 = 0;
	}

	if ( v35 )
		g_cl.m_local->m_aimPunchAngleVel( ) = data->m_punch_vel;
	else {
		data->m_punch_vel = g_cl.m_local->m_aimPunchAngleVel( );
		repredict = 1;
	}

	auto v28 = std::abs( g_cl.m_local->m_vecViewOffset( ).z - data->m_view_offset.z );
	if ( v28 > 0.125f )
	{
		data->m_view_offset.z = g_cl.m_local->m_vecViewOffset( ).z;
		repredict = 1;
	}
	else
		g_cl.m_local->m_vecViewOffset( ).z = data->m_view_offset.z;

	/*if ( ( m_vecOrigin - m_data->m_vecOrigin ).LengthSquared( ) >= 1.0f )
	{
		m_data->m_vecOrigin = m_vecOrigin;
		repredict = 1;
	}*/

	if ( repredict )
	{
		g_csgo.m_prediction->m_previous_startframe = -1;
		g_csgo.m_prediction->m_commands_predicted = 0;
		//prediction->m_bPreviousAckHadErrors = 1;
	}
}