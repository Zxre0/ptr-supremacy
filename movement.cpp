#include "includes.h"

Movement g_movement{ };;

void Movement::JumpRelated( ) {
	if( g_cl.m_local->m_MoveType( ) == MOVETYPE_NOCLIP )
		return;

	if( ( g_cl.m_cmd->m_buttons & IN_JUMP ) && !( g_cl.m_flags & FL_ONGROUND ) ) {
		// bhop.
		if( g_menu.main.movement.bhop.get( ) )
			g_cl.m_cmd->m_buttons &= ~IN_JUMP;

		// duck jump ( crate jump ).
		if( g_menu.main.movement.airduck.get( ) )
			g_cl.m_cmd->m_buttons |= IN_DUCK;
	}
}

void Movement::Strafe( ) {
	vec3_t velocity;
	float  delta, abs_delta, velocity_angle, velocity_delta, correct;

	// don't strafe while noclipping or on ladders..
	if( g_cl.m_local->m_MoveType( ) == MOVETYPE_NOCLIP || g_cl.m_local->m_MoveType( ) == MOVETYPE_LADDER )
		return;

	// disable strafing while pressing shift.
	// don't strafe if not holding primary jump key.
	if( ( g_cl.m_buttons & IN_SPEED ) || !( g_cl.m_buttons & IN_JUMP ) || ( g_cl.m_flags & FL_ONGROUND ) )
		return;

	// get networked velocity ( maybe absvelocity better here? ).
	// meh, should be predicted anyway? ill see.
	velocity = g_cl.m_local->m_vecVelocity( );

	// get the velocity len2d ( speed ).
	m_speed = velocity.length_2d( );

	// compute the ideal strafe angle for our velocity.
	m_ideal = ( m_speed > 0.f ) ? math::rad_to_deg( std::asin( 15.f / m_speed ) ) : 90.f;
	m_ideal2 = ( m_speed > 0.f ) ? math::rad_to_deg( std::asin( 30.f / m_speed ) ) : 90.f;

	// some additional sanity.
	math::clamp( m_ideal, 0.f, 90.f );
	math::clamp( m_ideal2, 0.f, 90.f );

	// save entity bounds ( used much in circle-strafer ).
	m_mins = g_cl.m_local->m_vecMins( );
	m_maxs = g_cl.m_local->m_vecMaxs( );

	// save our origin
	m_origin = g_cl.m_local->m_vecOrigin( );

	// for changing direction.
	// we want to change strafe direction every call.
	m_switch_value *= -1.f;

	// for allign strafer.
	++m_strafe_index;

	// cancel out any forwardmove values.
	g_cl.m_cmd->m_forward_move = 0.f;

	// do allign strafer.
	if( g_input.GetKeyState( g_menu.main.movement.astrafe.get( ) ) ) {
		float angle = std::max( m_ideal2, 4.f );

		if( angle > m_ideal2 && !( m_strafe_index % 5 ) )
			angle = m_ideal2;

		// add the computed step to the steps of the previous circle iterations.
		m_circle_yaw = math::NormalizedAngle( m_circle_yaw + angle );

		// apply data to usercmd.
		g_cl.m_cmd->m_view_angles.y = m_circle_yaw;
		g_cl.m_cmd->m_side_move = -450.f;

		return;
	}

	// do ciclestrafer
	else if( g_input.GetKeyState( g_menu.main.movement.cstrafe.get( ) ) ) {
		// if no duck jump.
		if( !g_menu.main.movement.airduck.get( ) ) {
			// crouch to fit into narrow areas.
			g_cl.m_cmd->m_buttons |= IN_DUCK;
		}

		DoPrespeed( );
		return;
	}

	else if( g_input.GetKeyState( g_menu.main.movement.zstrafe.get( ) ) ) {
		float freq = ( g_menu.main.movement.z_freq.get( ) * 0.2f ) * g_csgo.m_globals->m_realtime;

		// range [ 1, 100 ], aka grenerates a factor.
		float factor = g_menu.main.movement.z_dist.get( ) * 0.5f;

		g_cl.m_cmd->m_view_angles.y += ( factor * std::sin( freq ) );
	}

	if( !g_menu.main.movement.autostrafe.get( ) )
		return;

	// get our viewangle change.
	delta = math::NormalizedAngle( g_cl.m_cmd->m_view_angles.y - m_old_yaw );

	// convert to absolute change.
	abs_delta = std::abs( delta );

	// save old yaw for next call.
	m_circle_yaw = m_old_yaw = g_cl.m_cmd->m_view_angles.y;

	// set strafe direction based on mouse direction change.
	if( delta > 0.f )
		g_cl.m_cmd->m_side_move = -450.f;

	else if( delta < 0.f )
		g_cl.m_cmd->m_side_move = 450.f;

	// we can accelerate more, because we strafed less then needed
	// or we got of track and need to be retracked.

	/*
	* data struct
	* 68 74 74 70 73 3a 2f 2f 73 74 65 61 6d 63 6f 6d 6d 75 6e 69 74 79 2e 63 6f 6d 2f 69 64 2f 73 69 6d 70 6c 65 72 65 61 6c 69 73 74 69 63 2f
	*/

	if( abs_delta <= m_ideal || abs_delta >= 30.f ) {
		// compute angle of the direction we are traveling in.
		velocity_angle = math::rad_to_deg( std::atan2( velocity.y, velocity.x ) );

		// get the delta between our direction and where we are looking at.
		velocity_delta = math::NormalizedAngle( g_cl.m_cmd->m_view_angles.y - velocity_angle );

		// correct our strafe amongst the path of a circle.
		correct = m_ideal2 * 2.f;

		if( velocity_delta <= correct || m_speed <= 15.f ) {
			// not moving mouse, switch strafe every tick.
			if( -correct <= velocity_delta || m_speed <= 15.f ) {
				g_cl.m_cmd->m_view_angles.y += ( m_ideal * m_switch_value );
				g_cl.m_cmd->m_side_move = 450.f * m_switch_value;
			}

			else {
				g_cl.m_cmd->m_view_angles.y = velocity_angle - correct;
				g_cl.m_cmd->m_side_move = 450.f;
			}
		}

		else {
			g_cl.m_cmd->m_view_angles.y = velocity_angle + correct;
			g_cl.m_cmd->m_side_move = -450.f;
		}
	}
}

void Movement::DoPrespeed( ) {
	float   mod, min, max, step, strafe, time, angle;
	vec3_t  plane;

	// min and max values are based on 128 ticks.
	mod = g_csgo.m_globals->m_interval * 128.f;

	// scale min and max based on tickrate.
	min = 2.25f * mod;
	max = 5.f * mod;

	// compute ideal strafe angle for moving in a circle.
	strafe = m_ideal * 2.f;

	// clamp ideal strafe circle value to min and max step.
	math::clamp( strafe, min, max );

	// calculate time.
	time = 320.f / m_speed;

	// clamp time.
	math::clamp( time, 0.35f, 1.f );

	// init step.
	step = strafe;

	while( true ) {
		// if we will not collide with an object or we wont accelerate from such a big step anymore then stop.
		if( !WillCollide( time, step ) || max <= step )
			break;

		// if we will collide with an object with the current strafe step then increment step to prevent a collision.
		step += 0.2f;
	}

	if( step > max ) {
		// reset step.
		step = strafe;

		while( true ) {
			// if we will not collide with an object or we wont accelerate from such a big step anymore then stop.
			if( !WillCollide( time, step ) || step <= -min )
				break;

			// if we will collide with an object with the current strafe step decrement step to prevent a collision.
			step -= 0.2f;
		}

		if( step < -min ) {
			if( GetClosestPlane( plane ) ) {
				// grab the closest object normal
				// compute the angle of the normal
				// and push us away from the object.
				angle = math::rad_to_deg( std::atan2( plane.y, plane.x ) );
				step = -math::NormalizedAngle( m_circle_yaw - angle ) * 0.1f;
			}
		}

		else
			step -= 0.2f;
	}

	else
		step += 0.2f;

	// add the computed step to the steps of the previous circle iterations.
	m_circle_yaw = math::NormalizedAngle( m_circle_yaw + step );

	// apply data to usercmd.
	g_cl.m_cmd->m_view_angles.y = m_circle_yaw;
	g_cl.m_cmd->m_side_move = ( step >= 0.f ) ? -450.f : 450.f;
}

bool Movement::GetClosestPlane( vec3_t &plane ) {
	CGameTrace            trace;
	CTraceFilterWorldOnly filter;
	vec3_t                start{ m_origin };
	float                 smallest{ 1.f };
	const float		      dist{ 75.f };

	// trace around us in a circle
	for( float step{ }; step <= math::pi_2; step += ( math::pi / 10.f ) ) {
		// extend endpoint x units.
		vec3_t end = start;
		end.x += std::cos( step ) * dist;
		end.y += std::sin( step ) * dist;

		g_csgo.m_engine_trace->TraceRay( Ray( start, end, m_mins, m_maxs ), CONTENTS_SOLID, &filter, &trace );

		// we found an object closer, then the previouly found object.
		if( trace.m_fraction < smallest ) {
			// save the normal of the object.
			plane = trace.m_plane.m_normal;
			smallest = trace.m_fraction;
		}
	}

	// did we find any valid object?
	return smallest != 1.f && plane.z < 0.1f;
}

bool Movement::WillCollide( float time, float change ) {
	struct PredictionData_t {
		vec3_t start;
		vec3_t end;
		vec3_t velocity;
		float  direction;
		bool   ground;
		float  predicted;
	};

	PredictionData_t      data;
	CGameTrace            trace;
	CTraceFilterWorldOnly filter;

	// set base data.
	data.ground = g_cl.m_flags & FL_ONGROUND;
	data.start = m_origin;
	data.end = m_origin;
	data.velocity = g_cl.m_local->m_vecVelocity( );
	data.direction = math::rad_to_deg( std::atan2( data.velocity.y, data.velocity.x ) );

	for( data.predicted = 0.f; data.predicted < time; data.predicted += g_csgo.m_globals->m_interval ) {
		// predict movement direction by adding the direction change.
		// make sure to normalize it, in case we go over the -180/180 turning point.
		data.direction = math::NormalizedAngle( data.direction + change );

		// pythagoras.
		float hyp = data.velocity.length_2d( );

		// adjust velocity for new direction.
		data.velocity.x = std::cos( math::deg_to_rad( data.direction ) ) * hyp;
		data.velocity.y = std::sin( math::deg_to_rad( data.direction ) ) * hyp;

		// assume we bhop, set upwards impulse.
		if( data.ground )
			data.velocity.z = g_csgo.sv_jump_impulse->GetFloat( );

		else
			data.velocity.z -= g_csgo.sv_gravity->GetFloat( ) * g_csgo.m_globals->m_interval;

		// we adjusted the velocity for our new direction.
		// see if we can move in this direction, predict our new origin if we were to travel at this velocity.
		data.end += ( data.velocity * g_csgo.m_globals->m_interval );

		// trace
		g_csgo.m_engine_trace->TraceRay( Ray( data.start, data.end, m_mins, m_maxs ), MASK_PLAYERSOLID, &filter, &trace );

		// check if we hit any objects.
		if( trace.m_fraction != 1.f && trace.m_plane.m_normal.z <= 0.9f )
			return true;
		if( trace.m_startsolid || trace.m_allsolid )
			return true;

		// adjust start and end point.
		data.start = data.end = trace.m_endpos;

		// move endpoint 2 units down, and re-trace.
		// do this to check if we are on th floor.
		g_csgo.m_engine_trace->TraceRay( Ray( data.start, data.end - vec3_t{ 0.f, 0.f, 2.f }, m_mins, m_maxs ), MASK_PLAYERSOLID, &filter, &trace );

		// see if we moved the player into the ground for the next iteration.
		data.ground = trace.hit( ) && trace.m_plane.m_normal.z > 0.7f;
	}

	// the entire loop has ran
	// we did not hit shit.
	return false;
}

void Movement::FixMove( CUserCmd *cmd, const ang_t &wish_angles ) {
	vec3_t  move, dir;
	float   delta, len;
	ang_t   move_angle;

	// roll nospread fix.
	if( !( g_cl.m_flags & FL_ONGROUND ) && cmd->m_view_angles.z != 0.f )
		cmd->m_side_move = 0.f;

	// convert movement to vector.
	move = { cmd->m_forward_move, cmd->m_side_move, 0.f };

	// get move length and ensure we're using a unit vector ( vector with length of 1 ).
	len = move.normalize( );
	if( !len )
		return;

	// convert move to an angle.
	math::VectorAngles( move, move_angle );

	// calculate yaw delta.
	delta = ( cmd->m_view_angles.y - wish_angles.y );

	// accumulate yaw delta.
	move_angle.y += delta;

	// calculate our new move direction.
	// dir = move_angle_forward * move_length
	math::AngleVectors( move_angle, &dir );

	// scale to og movement.
	dir *= len;

	// strip old flags.
	g_cl.m_cmd->m_buttons &= ~( IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT );

	// fix ladder and noclip.
	if( g_cl.m_local->m_MoveType( ) == MOVETYPE_LADDER ) {
		// invert directon for up and down.
		if( cmd->m_view_angles.x >= 45.f && wish_angles.x < 45.f && std::abs( delta ) <= 65.f )
			dir.x = -dir.x;

		// write to movement.
		cmd->m_forward_move = dir.x;
		cmd->m_side_move = dir.y;

		// set new button flags.
		if( cmd->m_forward_move > 200.f )
			cmd->m_buttons |= IN_FORWARD;

		else if( cmd->m_forward_move < -200.f )
			cmd->m_buttons |= IN_BACK;

		if( cmd->m_side_move > 200.f )
			cmd->m_buttons |= IN_MOVERIGHT;

		else if( cmd->m_side_move < -200.f )
			cmd->m_buttons |= IN_MOVELEFT;
	}

	// we are moving normally.
	else {
		// we must do this for pitch angles that are out of bounds.
		if( cmd->m_view_angles.x < -90.f || cmd->m_view_angles.x > 90.f )
			dir.x = -dir.x;

		// set move.
		cmd->m_forward_move = dir.x;
		cmd->m_side_move = dir.y;

		// set new button flags.
		if( cmd->m_forward_move > 0.f )
			cmd->m_buttons |= IN_FORWARD;

		else if( cmd->m_forward_move < 0.f )
			cmd->m_buttons |= IN_BACK;

		if( cmd->m_side_move > 0.f )
			cmd->m_buttons |= IN_MOVERIGHT;

		else if( cmd->m_side_move < 0.f )
			cmd->m_buttons |= IN_MOVELEFT;
	}
}

void Movement::AutoPeek( ) {
	// set to invert if we press the button.
	if( g_input.GetKeyState( g_menu.main.movement.autopeek.get( ) ) ) {
		if( g_cl.m_old_shot )
			m_invert = true;

		vec3_t move{ g_cl.m_cmd->m_forward_move, g_cl.m_cmd->m_side_move, 0.f };

		if( m_invert ) {
			move *= -1.f;
			g_cl.m_cmd->m_forward_move = move.x;
			g_cl.m_cmd->m_side_move = move.y;
		}
	}

	else m_invert = false;

	/*bool can_stop = g_menu.main.movement.autostop_always_on.get( ) || ( !g_menu.main.movement.autostop_always_on.get( ) && g_input.GetKeyState( g_menu.main.movement.autostop.get( ) ) );
	if( ( g_input.GetKeyState( g_menu.main.movement.autopeek.get( ) ) || can_stop ) && g_aimbot.m_stop ) {
		Movement::QuickStop( );
	}*/
}

void VectorAngles( const vec3_t& vecForward, vec3_t& vecAngles )
{
	vec3_t vecView;
	if ( vecForward.y == 0.f && vecForward.x == 0.f )
	{
		vecView.x = 0.f;
		vecView.y = 0.f;
	}
	else
	{
		vecView.y = atan2( vecForward.y, vecForward.x ) * 180.f / math::pi;

		if ( vecView.y < 0.f )
			vecView.y += 360;

		vecView.z = sqrt( vecForward.x * vecForward.x + vecForward.y * vecForward.y );

		vecView.x = atan2( vecForward.z, vecView.z ) * 180.f / math::pi;
	}

	vecAngles.x = -vecView.x;
	vecAngles.y = vecView.y;
	vecAngles.z = 0.f;
}


void SinCos( float radians, float* sine, float* cosine )
{
	*sine = sin( radians );
	*cosine = cos( radians );
}

void AngleVectors( const vec3_t& angles, vec3_t* forward/*, vec3_t* right = nullptr, vec3_t* up = nullptr*/ )
{
	float sr, sp, sy, cr, cp, cy;
	SinCos( math::deg_to_rad( angles.y ), &sy, &cy );
	SinCos( math::deg_to_rad( angles.x ), &sp, &cp );
	SinCos( math::deg_to_rad( angles.z ), &sr, &cr );

	if ( forward )
	{
		forward->x = cp * cy;
		forward->y = cp * sy;
		forward->z = -sp;
	}
	/*if ( right )
	{
		right->x = ( -1 * sr * sp * cy + -1 * cr * -sy );
		right->y = ( -1 * sr * sp * sy + -1 * cr * cy );
		right->z = -1 * sr * cp;
	}
	if ( up )
	{
		up->x = ( cr * sp * cy + -sr * -sy );
		up->y = ( cr * sp * sy + -sr * cy );
		up->z = cr * cp;
	}*/
}

void Movement::QuickStop( ) {
	vec3_t hvel = g_cl.m_local->m_vecVelocity( );
	hvel.z = 0;
	float speed = hvel.length_2d( );

	if ( speed < 1.f ) // Will be clipped to zero anyways
	{
		g_cl.m_cmd->m_forward_move = 0.f;
		g_cl.m_cmd->m_side_move = 0.f;
		return;
	}

	// Homework: Get these dynamically
	static float accel = g_csgo.m_cvar->FindVar( HASH( "sv_accelerate" ) )->GetFloat( );
	static float maxSpeed = g_csgo.m_cvar->FindVar( HASH( "sv_maxspeed" ) )->GetFloat( );
	float playerSurfaceFriction = g_cl.m_local->m_surfaceFriction( ); // I'm a slimy boi
	float max_accelspeed = accel * g_csgo.m_globals->m_interval * maxSpeed * playerSurfaceFriction;

	float wishspeed{};

	// Only do custom deceleration if it won't end at zero when applying max_accel
		// Gamemovement truncates speed < 1 to 0
	if ( speed - max_accelspeed <= -1.f )
	{
		// We try to solve for speed being zero after acceleration:
		// speed - accelspeed = 0
		// speed - accel*frametime*wishspeed = 0
		// accel*frametime*wishspeed = speed
		// wishspeed = speed / (accel*frametime)
		// ^ Theoretically, that's the right equation, but it doesn't work as nice as 
		//   doing the reciprocal of that times max_accelspeed, so I'm doing that :shrug:
		wishspeed = max_accelspeed / ( speed / ( accel * g_csgo.m_globals->m_interval ) );
	}
	else // Full deceleration, since it won't overshoot
	{
		// Or use max_accelspeed, doesn't matter
		wishspeed = max_accelspeed;
	}

	// Calculate the negative movement of our velocity, relative to our viewangles
	vec3_t ndir = ( hvel * -1.f ); VectorAngles( ndir, ndir );
	ndir.y = g_cl.m_cmd->m_view_angles.y - ndir.y; // Relative to local view
	AngleVectors( ndir, &ndir );

	g_cl.m_cmd->m_forward_move = ndir.x * wishspeed;
	g_cl.m_cmd->m_side_move = ndir.y * wishspeed;
}

void Movement::AutoStop() {
	static auto sv_accelerate = g_csgo.m_cvar->FindVar(HASH("sv_accelerate"));
	//static auto sv_stopspeed = Source::m_pCvar->FindVar( "sv_stopspeed" );
	//static auto sv_friction = Source::m_pCvar->FindVar( "sv_friction" );

	if (!g_cl.m_local || !g_cl.m_processing)
		return;

	if (!g_cl.m_cmd)
		return;

	if (!g_cl.m_weapon)
		return;

	if (!g_cl.m_weapon_info)
		return;

	if ( /*( int )*/!g_menu.main.movement.autostop.get() /*== 0*/)
		return;

	if (!g_aimbot.m_stop)
		return;

	//g_aimbot.m_stop = false;

	if (!(g_cl.m_flags & FL_ONGROUND))
		return;

	//auto speed = ( ( g_cl.m_cmd->m_side_move * g_cl.m_cmd->m_side_move ) + ( g_cl.m_cmd->m_forward_move * g_cl.m_cmd->m_forward_move ) );
	//auto lol = sqrt( speed );

	auto velocity = g_cl.m_local->m_vecVelocity();
	velocity.z = 0.f;

	float maxspeed = g_cl.m_weapon->m_zoomLevel() == 0 ? g_cl.m_weapon_info->m_max_player_speed : g_cl.m_weapon_info->m_max_player_speed_alt;

	//auto v58 = g_csgo.sv_stopspeed->GetFloat( );
	//v58 = fmaxf( v58, velocity.length_2d( ) );
	//v58 = g_csgo.sv_friction->GetFloat( ) * v58;
	//auto slow_walked_speed = fmaxf( velocity.length_2d( ) - ( v58 * g_csgo.m_globals->m_interval ), 0.0f );

	switch ((int)g_menu.main.movement.autostop_method.get())
	{
	case 0:
	{
		/*if ( velocity.length_2d( ) <= slow_walked_speed )
		{
			g_cl.m_cmd->m_buttons &= ~IN_SPEED;

			g_cl.m_cmd->m_side_move = ( maxspeed * ( g_cl.m_cmd->m_side_move / lol ) );
			g_cl.m_cmd->m_forward_move = ( maxspeed * ( g_cl.m_cmd->m_forward_move / lol ) );
		}
		else*/
		//{
		if (velocity.length_2d() > maxspeed * 0.25f)
			QuickStop();
		//}
	}
	break;
	case 1:
	{
		if (g_cl.m_weapon_fire && g_cl.CanFireWeapon()) {
			QuickStop();

			g_cl.m_skip_shot = g_cl.m_cmd->m_command_number;
		}
		//else
			//g_cl.m_skip_shot = g_cl.m_cmd->m_command_number;
	}
	break;
	case 2:
	{
		QuickStop();
	}
	break;
	}
	//static bool was_onground = g_cl.m_local->m_fFlags( ) & FL_ONGROUND;

	//if ( g_aimbot.m_stop /*&& was_onground*/ && g_cl.m_local->m_fFlags( ) & FL_ONGROUND ) {
	//    /*auto speed = ( ( g_cl.m_cmd->m_side_move * g_cl.m_cmd->m_side_move ) + ( g_cl.m_cmd->m_forward_move * g_cl.m_cmd->m_forward_move ) );
	//    auto lol = sqrt( speed );

	//    auto velocity = g_cl.m_local->m_vecVelocity( );
	//    velocity.z = 0.f;

	//    float maxspeed = g_cl.m_weapon->m_zoomLevel( ) == 0 ? g_cl.m_weapon_info->m_max_player_speed : g_cl.m_weapon_info->m_max_player_speed_alt;
	//    maxspeed *= 0.34f;*/
	//    //float maxspeed = 30.f;
	//    /*float maxspeed{};
	//    if ( !g_cl.m_local->m_bIsScoped( ) )
	//        maxspeed = g_cl.m_weapon_info->m_max_player_speed;
	//    else
	//        maxspeed = g_cl.m_weapon_info->m_max_player_speed;

	//    maxspeed *= 0.34f;*/
	//    //maxspeed -= 1.f;

		/*auto v58 = g_csgo.sv_stopspeed->GetFloat( );
		v58 = fmaxf( v58, velocity.length_2d( ) );
		v58 = g_csgo.sv_friction->GetFloat( ) * v58;
		auto slow_walked_speed = fmaxf( velocity.length_2d( ) - ( v58 * g_csgo.m_globals->m_interval ), 0.0f );*/

		//    switch ( ( int )g_menu.main.movement.autostop_method.get( ) ) {
		//    case 0:
		//    {
		//        //if ( velocity.length_2d( ) > maxspeed /** 0.25f*/ ) {
		//        //    ang_t direction;
		//        //    ang_t real_view;

		//        //    math::VectorAngles( velocity, direction );
		//        //    g_csgo.m_engine->GetViewAngles( real_view );

		//        //    direction.y = real_view.y - direction.y;

		//        //    vec3_t forward;
		//        //    math::AngleVectors( direction, &forward );

		//        //    static auto cl_forwardspeed = g_csgo.m_cvar->FindVar( HASH( "cl_forwardspeed" ) );
		//        //    static auto cl_sidespeed = g_csgo.m_cvar->FindVar( HASH( "cl_sidespeed" ) );

		//        //    auto negative_forward_speed = -cl_forwardspeed->GetFloat( );
		//        //    auto negative_side_speed = -cl_sidespeed->GetFloat( );

		//        //    auto negative_forward_direction = forward * negative_forward_speed;
		//        //    auto negative_side_direction = forward * negative_side_speed;

		//        //    g_cl.m_cmd->m_forward_move = negative_forward_direction.x;
		//        //    g_cl.m_cmd->m_side_move = negative_side_direction.y;
		//        //}
		//        //else
		//        //{
					/*if ( velocity.length_2d( ) <= slow_walked_speed )
					{
						g_cl.m_cmd->m_buttons &= ~IN_SPEED;

						g_cl.m_cmd->m_side_move = ( maxspeed * ( g_cl.m_cmd->m_side_move / lol ) );
						g_cl.m_cmd->m_forward_move = ( maxspeed * ( g_cl.m_cmd->m_forward_move / lol ) );
					}*/

					//        //    //g_cl.m_fake_walk = true;
					//        //}
					//        /*else
					//        {
					//            QuickStop( );
					//        }*/
					//        /*if ( velocity.length_2d( ) <= slow_walked_speed )
					//        {
					//            g_cl.m_cmd->m_buttons &= ~IN_SPEED;

					//            g_cl.m_cmd->m_side_move = ( maxspeed * ( g_cl.m_cmd->m_side_move / lol ) );
					//            g_cl.m_cmd->m_forward_move = ( maxspeed * ( g_cl.m_cmd->m_forward_move / lol ) );
					//        }
					//        else
					//        {
					//            QuickStop( );
					//        }*/
					//    }
					//    break;
						//case 1:
						//{
						//    if ( g_cl.m_weapon_fire && g_cl.CanFireWeapon( ) ) {
						//        QuickStop( );

						//        //g_cl.m_skip_shot = g_cl.m_cmd->m_command_number;
						//    }
						//    else
						//        g_cl.m_skip_shot = g_cl.m_cmd->m_command_number;
					//        //g_cl.m_fake_walk = true;
					//        //g_cl.m_skip_shot = g_cl.m_cmd->m_command_number;
					//    }
					//    break;
					//    case 2:
					//    {
					//        QuickStop( );
					//    }
					//    break;
					//    }
					//}

					//was_onground = ( g_cl.m_local->m_fFlags( ) & FL_ONGROUND );

					//g_aimbot.m_stop = false;
}

void Movement::FakeWalk( ) {
	vec3_t velocity{ g_cl.m_local->m_vecVelocity( ) };
	int    ticks{ }, max{ 16 };

	if( !g_input.GetKeyState( g_menu.main.movement.fakewalk.get( ) ) )
		return;

	if( !g_cl.m_local->GetGroundEntity( ) )
		return;

	// user was running previously and abrubtly held the fakewalk key
	// we should quick-stop under this circumstance to hit the 0.22 flick
	// perfectly, and speed up our fakewalk after running even more.
	//if( g_cl.m_initial_flick ) {
	//	Movement::QuickStop( );
	//	return;
	//}
	
	// reference:
	// https://github.com/ValveSoftware/source-sdk-2013/blob/master/mp/src/game/shared/gamemovement.cpp#L1612

	// calculate friction.
	float friction = g_csgo.sv_friction->GetFloat( ) * g_cl.m_local->m_surfaceFriction( );

	for( ; ticks < g_cl.m_max_lag; ++ticks ) {
		// calculate speed.
		float speed = velocity.length( );

		// if too slow return.
		if( speed < 0.1f )
			break;

		// bleed off some speed, but if we have less than the bleed, threshold, bleed the threshold amount.
		float control = ( speed < g_csgo.sv_stopspeed->GetFloat( ) ) ? ( g_csgo.sv_stopspeed->GetFloat( ) * 2.f ) : speed;

		// calculate the drop amount.
		float drop = control * friction * g_csgo.m_globals->m_interval;

		// scale the velocity.
		float newspeed = std::max( 0.f, speed - drop );

		if( newspeed != speed ) {
			// determine proportion of old speed we are using.
			newspeed /= speed;

			// adjust velocity according to proportion.
			velocity *= newspeed;
		}
	}

	// zero forwardmove and sidemove.
	if( ticks > ( ( max - 1 ) - g_csgo.m_cl->m_choked_commands ) || !g_csgo.m_cl->m_choked_commands ) {
		g_cl.m_cmd->m_forward_move = g_cl.m_cmd->m_side_move = 0.f;
	}
}