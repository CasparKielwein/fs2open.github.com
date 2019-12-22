//
// Created by ckielwein on 2019-12-22.
//

#include <playerman/player.h>
#include <freespace.h>
#include <network/multiutil.h>
#include "debris/debris.h"
#include "cmeasure/cmeasure.h"
#include "network/multi.h"
#include "weapon/beam.h"
#include "weapon/swarm.h"
#include "debugconsole/console.h"
#include "globalincs/linklist.h"
#include "scripting/scripting.h"
#include "tracing/categories.h"
#include "tracing/Monitor.h"
#include "tracing/tracing.h"
#include "weapon/weapon.h"
#include "ship/ship.h"
#include "asteroid/asteroid.h"
#include "objectstore.h"
#include "object.h"
#include "objcollide.h"
#include "objectdock.h"

object obj_free_list;
object obj_used_list;
object obj_create_list;

object *Player_obj = nullptr;
object *Viewer_obj = nullptr;

//Data for objects
object Objects[MAX_OBJECTS];

#ifdef OBJECT_CHECK
checkobject CheckObjects[MAX_OBJECTS];
#endif

int Num_objects=-1;
int Highest_object_index=-1;
int Highest_ever_object_index=0;


static void on_script_state_destroy(lua_State*) {
    // Since events are mostly used for scripting, we clear the event handlers when the Lua state is destroyed
    for (auto& obj : Objects) {
        obj.pre_move_event.clear();
        obj.post_move_event.clear();
    }
}

/**
 * Sets up the free list & init player & whatever else
 */
void obj_init()
{
    Object_inited = 1;
    for (auto & Object : Objects)
        Object.clear();
    Viewer_obj = nullptr;

    list_init( &obj_free_list );
    list_init( &obj_used_list );
    list_init( &obj_create_list );

    // Link all object slots into the free list
    object* objp = Objects;
    for (int i=0; i<MAX_OBJECTS; i++)	{
        list_append(&obj_free_list, objp);
        objp++;
    }

    Object_next_signature = 1;	//0 is invalid, others start at 1
    Num_objects = 0;
    Highest_object_index = 0;

    obj_reset_colliders();

    Script_system.OnStateDestroy.add(on_script_state_destroy);
}

void obj_shutdown()
{
    for (auto& obj : Objects) {
        obj.clear();
    }
}

void obj_delete_all()
{
    int counter = 0;
    for (int i = 0; i < MAX_OBJECTS; ++i)
    {
        if (Objects[i].type == OBJ_NONE)
            continue;
        ++counter;
        obj_delete(i);
    }

    mprintf(("Cleanup: Deleted %i objects\n", counter));
}

static void obj_move_all_pre(object *objp, float frametime)
{
    TRACE_SCOPE(tracing::PreMove);

    switch( objp->type )	{
        case OBJ_WEAPON:
            if (!physics_paused){
                weapon_process_pre( objp, frametime );
            }
            break;
        case OBJ_SHIP:
            if (!physics_paused || (objp==Player_obj )){
                ship_process_pre( objp, frametime );
            }
            break;
        case OBJ_FIREBALL:
            // all fireballs are moved via fireball_process_post()
            break;
        case OBJ_SHOCKWAVE:
            // all shockwaves are moved via shockwave_move_all()
            break;
        case OBJ_DEBRIS:
            // all debris are moved via debris_process_post()
            break;
        case OBJ_ASTEROID:
            if (!physics_paused){
                asteroid_process_pre(objp);
            }
            break;
/*	case OBJ_CMEASURE:
		if (!physics_paused){
			cmeasure_process_pre(objp, frametime);
		}
		break;*/
        case OBJ_WAYPOINT:
            break;  // waypoints don't move..
        case OBJ_GHOST:
            break;
        case OBJ_OBSERVER:
        case OBJ_JUMP_NODE:
            break;
        case OBJ_BEAM:
            break;
        case OBJ_NONE:
            Int3();
            break;
        default:
            Error(LOCATION, "Unhandled object type %d in obj_move_all_pre\n", objp->type);
    }

    objp->pre_move_event(objp);
}


// Used to tell if a particular group of lasers has cast light yet.
ubyte Obj_weapon_group_id_used[WEAPON_MAX_GROUP_IDS];

/**
 * Called once a frame to mark all weapon groups as not having cast light yet.
 */
static void obj_clear_weapon_group_id_list()
{
    memset( Obj_weapon_group_id_used, 0, sizeof(Obj_weapon_group_id_used) );
}


int Arc_light = 1;		// If set, electrical arcs on debris cast light
DCF_BOOL(arc_light, Arc_light)

extern fireball Fireballs[];

static void obj_move_all_post(object *objp, float frametime)
{
    switch (objp->type)
    {
        case OBJ_WEAPON:
        {
            TRACE_SCOPE(tracing::WeaponPostMove);

            if ( !physics_paused )
                weapon_process_post( objp, frametime );

            // Cast light
            if ( Detail.lighting > 3 ) {
                // Weapons cast light

                int group_id = Weapons[objp->instance].group_id;
                int cast_light = 1;

                if ( (group_id >= 0) && (Obj_weapon_group_id_used[group_id]==0) )	{
                    // Mark this group as done
                    Obj_weapon_group_id_used[group_id]++;
                } else {
                    // This group has already done its light casting
                    cast_light = 0;
                }

                if ( cast_light )	{
                    weapon_info * wi = &Weapon_info[Weapons[objp->instance].weapon_info_index];

                    if ( wi->render_type == WRT_LASER )	{
                        color c;
                        float r,g,b;

                        // get the laser color
                        weapon_get_laser_color(&c, objp);

                        r = i2fl(c.red)/255.0f;
                        g = i2fl(c.green)/255.0f;
                        b = i2fl(c.blue)/255.0f;

                        //light_add_point( &objp->pos, 10.0f, 20.0f, 1.0f, r, g, b, objp->parent );
                        light_add_point( &objp->pos, 10.0f, 100.0f, 1.0f, r, g, b, objp->parent );
                    } else {
                        light_add_point( &objp->pos, 10.0f, 20.0f, 1.0f, 1.0f, 1.0f, 1.0f, objp->parent );
                    }
                }
            }

            break;
        }

        case OBJ_SHIP:
        {
            TRACE_SCOPE(tracing::ShipPostMove);

            if ( !physics_paused || (objp==Player_obj) ) {
                ship_process_post( objp, frametime );
                ship_model_update_instance(objp);
            }

            // Make any electrical arcs on ships cast light
            if (Arc_light)	{
                if ( (Detail.lighting > 3) && (objp != Viewer_obj) ) {
                    ship* shipp = &Ships[objp->instance];

                    for (int i=0; i<MAX_SHIP_ARCS; i++ )	{
                        if ( timestamp_valid( shipp->arc_timestamp[i] ) )	{
                            // Move arc endpoints into world coordinates
                            vec3d tmp1, tmp2;
                            vm_vec_unrotate(&tmp1,&shipp->arc_pts[i][0],&objp->orient);
                            vm_vec_add2(&tmp1,&objp->pos);

                            vm_vec_unrotate(&tmp2,&shipp->arc_pts[i][1],&objp->orient);
                            vm_vec_add2(&tmp2,&objp->pos);

                            light_add_point( &tmp1, 10.0f, 20.0f, frand(), 1.0f, 1.0f, 1.0f, -1 );
                            light_add_point( &tmp2, 10.0f, 20.0f, frand(), 1.0f, 1.0f, 1.0f, -1 );
                        }
                    }
                }
            }

            //Check for changing team colors
            ship* shipp = &Ships[objp->instance];
            if (Ship_info[shipp->ship_info_index].uses_team_colors && stricmp(shipp->secondary_team_name.c_str(), "none") != 0) {
                if (f2fl(Missiontime) * 1000 > f2fl(shipp->team_change_timestamp) * 1000 + shipp->team_change_time) {
                    shipp->team_name = shipp->secondary_team_name;
                    shipp->team_change_timestamp = 0;
                    shipp->team_change_time = 0;
                    shipp->secondary_team_name = "none";
                }
            }

            break;
        }

        case OBJ_FIREBALL:
        {
            TRACE_SCOPE(tracing::FireballPostMove);

            if ( !physics_paused )
                fireball_process_post(objp,frametime);

            if (Detail.lighting > 2) {
                float r = 0.0f, g = 0.0f, b = 0.0f;

                fireball_get_color(Fireballs[objp->instance].fireball_info_index, &r, &g, &b);

                // we don't cast black light, so just bail in that case
                if ( (r == 0.0f) && (g == 0.0f) && (b == 0.0f) )
                    break;

                // Make explosions cast light
                float p = fireball_lifeleft_percent(objp);
                if (p > 0.0f) {
                    if (p > 0.5f)
                        p = 1.0f - p;

                    p *= 2.0f;
                    float rad = p * (1.0f + frand() * 0.05f) * objp->radius;

                    float intensity = 1.0f;
                    if (fireball_is_warp(objp))
                    {
                        intensity = fireball_wormhole_intensity(&Fireballs[objp->instance]); // Valathil: Get wormhole radius for lighting
                        rad = objp->radius;
                    }
                    // P goes from 0 to 1 to 0 over the life of the explosion
                    // Only do this if rad is > 0.0000001f
                    if (rad > 0.0001f)
                        light_add_point( &objp->pos, rad * 2.0f, rad * 5.0f, intensity, r, g, b, -1 );
                }
            }

            break;
        }

        case OBJ_SHOCKWAVE:
            // all shockwaves are moved via shockwave_move_all()
            break;

        case OBJ_DEBRIS:
        {
            TRACE_SCOPE(tracing::DebrisPostMove);

            if ( !physics_paused )
                debris_process_post(objp, frametime);

            // Make any electrical arcs on debris cast light
            if (Arc_light)	{
                if ( Detail.lighting > 3 ) {
                    debris* db = &Debris[objp->instance];

                    if (db->arc_frequency > 0) {
                        for (int i=0; i<MAX_DEBRIS_ARCS; i++ )	{
                            if ( timestamp_valid( db->arc_timestamp[i] ) )	{
                                // Move arc endpoints into world coordinates
                                vec3d tmp1, tmp2;
                                vm_vec_unrotate(&tmp1,&db->arc_pts[i][0],&objp->orient);
                                vm_vec_add2(&tmp1,&objp->pos);

                                vm_vec_unrotate(&tmp2,&db->arc_pts[i][1],&objp->orient);
                                vm_vec_add2(&tmp2,&objp->pos);

                                light_add_point( &tmp1, 10.0f, 20.0f, frand(), 1.0f, 1.0f, 1.0f, -1 );
                                light_add_point( &tmp2, 10.0f, 20.0f, frand(), 1.0f, 1.0f, 1.0f, -1 );
                            }
                        }
                    }
                }
            }

            break;
        }

        case OBJ_ASTEROID:
        {
            TRACE_SCOPE(tracing::AsteroidPostMove);

            if ( !physics_paused )
                asteroid_process_post(objp);

            break;
        }

        case OBJ_WAYPOINT:
            break;  // waypoints don't move..

        case OBJ_GHOST:
            break;

        case OBJ_OBSERVER:
            void observer_process_post(object *objp);
            observer_process_post(objp);
            break;

        case OBJ_JUMP_NODE:
            radar_plot_object(objp);
            break;

        case OBJ_BEAM:
            break;

        case OBJ_NONE:
            Int3();
            break;

        default:
            Error(LOCATION, "Unhandled object type %d in obj_move_all_post\n", objp->type);
    }

    objp->post_move_event(objp);
}

static void obj_delete_all_that_should_be_dead()
{
    if (!Object_inited) {
        mprintf(("Why hasn't obj_init() been called yet?\n"));
        obj_init();
    }

    // Move all objects
    object* objp = GET_FIRST(&obj_used_list);
    while( objp !=END_OF_LIST(&obj_used_list) )	{
        // Goober5000 - HACK HACK HACK - see obj_move_all
        objp->flags.remove(Object::Object_Flags::Docked_already_handled);

        object* temp = GET_NEXT(objp);
        if ( objp->flags[Object::Object_Flags::Should_be_dead] )
            obj_delete( OBJ_INDEX(objp) );			// MWA says that john says that let obj_delete handle everything because of the editor
        objp = temp;
    }
}


#ifdef OBJECT_CHECK

static void obj_check_object( object *obj )
{
    int objnum = OBJ_INDEX(obj);

    // PROGRAMMERS: If one of these Int3() gets hit, then someone
    // is changing a value in the object structure that might cause
    // collision detection to not work.  See John for more info if
    // you are hitting one of these.

    if ( CheckObjects[objnum].type != obj->type )	{
        if ( (obj->type==OBJ_WAYPOINT) && (CheckObjects[objnum].type==OBJ_SHIP) )	{
            // We know about ships changing into waypoints and that is
            // ok.
            CheckObjects[objnum].type = OBJ_WAYPOINT;
        } else if ( (obj->type==OBJ_SHIP) && (CheckObjects[objnum].type==OBJ_GHOST) )	{
            // We know about player changing into a ghost after dying and that is
            // ok.
            CheckObjects[objnum].type = OBJ_GHOST;
        } else if ( (obj->type==OBJ_GHOST) && (CheckObjects[objnum].type==OBJ_SHIP) )	{
            // We know about player changing into a ghost after dying and that is
            // ok.
            CheckObjects[objnum].type = OBJ_SHIP;
        } else {
            mprintf(( "Object type changed! Old: %i, Current: %i\n", CheckObjects[objnum].type, obj->type ));
            Int3();
        }
    }
    if ( CheckObjects[objnum].signature != obj->signature ) {
        mprintf(( "Object signature changed!\n" ));
        Int3();
    }
    if ( (CheckObjects[objnum].flags[Object::Object_Flags::Collides]) != (obj->flags[Object::Object_Flags::Collides]) ) {
        mprintf(( "Object flags changed!\n" ));
        Int3();
    }
    if ( CheckObjects[objnum].parent_sig != obj->parent_sig ) {
        mprintf(( "Object parent sig changed!\n" ));
        Int3();
    }
    if ( CheckObjects[objnum].parent_type != obj->parent_type ) {
        mprintf(( "Object's parent type changed!\n" ));
        Int3();
    }
}
#endif


static void obj_move_call_physics(object *objp, float frametime)
{
    TRACE_SCOPE(tracing::Physics);

    int has_fired = -1;	//stop fireing stuff-Bobboau

    //	Do physics for objects with OF_PHYSICS flag set and with some engine strength remaining.
    if ( objp->flags[Object::Object_Flags::Physics] ) {
        // only set phys info if ship is not dead
        if ((objp->type == OBJ_SHIP) && !(Ships[objp->instance].flags[Ship::Ship_Flags::Dying])) {
            ship *shipp = &Ships[objp->instance];

            float engine_strength = ship_get_subsystem_strength(shipp, SUBSYSTEM_ENGINE);
            if ( ship_subsys_disrupted(shipp, SUBSYSTEM_ENGINE) ) {
                engine_strength=0.0f;
            }

            if (engine_strength == 0.0f) {	//	All this is necessary to make ship gradually come to a stop after engines are blown.
                vm_vec_zero(&objp->phys_info.desired_vel);
                vm_vec_zero(&objp->phys_info.desired_rotvel);
                objp->phys_info.flags |= (PF_REDUCED_DAMP | PF_DEAD_DAMP);
                objp->phys_info.side_slip_time_const = Ship_info[shipp->ship_info_index].damp * 4.0f;
            }

            if (shipp->weapons.num_secondary_banks > 0) {
                polymodel *pm = model_get(Ship_info[shipp->ship_info_index].model_num);
                Assertion( pm != nullptr, "No polymodel found for ship %s", Ship_info[shipp->ship_info_index].name );
                Assertion( pm->missile_banks != nullptr, "Ship %s has %d secondary banks, but no missile banks could be found.\n", Ship_info[shipp->ship_info_index].name, shipp->weapons.num_secondary_banks );

                for (int i = 0; i < shipp->weapons.num_secondary_banks; i++) {
                    //if there are no missles left don't bother
                    if (shipp->weapons.secondary_bank_ammo[i] == 0)
                        continue;

                    int points = pm->missile_banks[i].num_slots;
                    int missles_left = shipp->weapons.secondary_bank_ammo[i];
                    int next_point = shipp->weapons.secondary_next_slot[i];
                    float fire_wait = Weapon_info[shipp->weapons.secondary_bank_weapons[i]].fire_wait;
                    float reload_time = (fire_wait == 0.0f) ? 1.0f : 1.0f / fire_wait;

                    //ok so...we want to move up missles but only if there is a missle there to be moved up
                    //there is a missle behind next_point, and how ever many missles there are left after that

                    if (points > missles_left) {
                        //there are more slots than missles left, so not all of the slots will have missles drawn on them
                        for (int k = next_point; k < next_point+missles_left; k ++) {
                            float &s_pct = shipp->secondary_point_reload_pct[i][k % points];
                            if (s_pct < 1.0)
                                s_pct += reload_time * frametime;
                            if (s_pct > 1.0)
                                s_pct = 1.0f;
                        }
                    } else {
                        //we don't have to worry about such things
                        for (int k = 0; k < points; k++) {
                            float &s_pct = shipp->secondary_point_reload_pct[i][k];
                            if (s_pct < 1.0)
                                s_pct += reload_time * frametime;
                            if (s_pct > 1.0)
                                s_pct = 1.0f;
                        }
                    }
                }
            }
        }

        // if a weapon is flagged as dead, kill its engines just like a ship
        if((objp->type == OBJ_WEAPON) && (Weapons[objp->instance].weapon_flags[Weapon::Weapon_Flags::Dead_in_water])){
            vm_vec_zero(&objp->phys_info.desired_vel);
            vm_vec_zero(&objp->phys_info.desired_rotvel);
            objp->phys_info.flags |= (PF_REDUCED_DAMP | PF_DEAD_DAMP);
            objp->phys_info.side_slip_time_const = 1.0f;	// FIXME?  originally indexed into Ship_info[], which was a bug...
        }

        if (physics_paused)	{
            if (objp==Player_obj){
                physics_sim(&objp->pos, &objp->orient, &objp->phys_info, frametime );		// simulate the physics
            }
        } else {
            //	Hack for dock mode.
            //	If docking with a ship, we don't obey the normal ship physics, we can slew about.
            if (objp->type == OBJ_SHIP) {
                ai_info	*aip = &Ai_info[Ships[objp->instance].ai_index];

                //	Note: This conditional for using PF_USE_VEL (instantaneous acceleration) is probably too loose.
                //	A ships awaiting support will fly towards the support ship with instantaneous acceleration.
                //	But we want to have ships in the process of docking have quick acceleration, or they overshoot their goals.
                //	Probably can not key off objnum_I_am_docked_or_docking_with, but then need to add some other condition.  Live with it for now. -- MK, 2/19/98

                // Goober5000 - no need to key off objnum; other conditions get it just fine

                if (/* (objnum_I_am_docked_or_docking_with != -1) || */
                        ((aip->mode == AIM_DOCK) && ((aip->submode == AIS_DOCK_2) || (aip->submode == AIS_DOCK_3) || (aip->submode == AIS_UNDOCK_0))) ||
                        ((aip->mode == AIM_WARP_OUT) && (aip->submode >= AIS_WARP_3))) {
                    if (ship_get_subsystem_strength(&Ships[objp->instance], SUBSYSTEM_ENGINE) > 0.0f){
                        objp->phys_info.flags |= PF_USE_VEL;
                    } else {
                        objp->phys_info.flags &= ~PF_USE_VEL;	//	If engine blown, don't PF_USE_VEL, or ships stop immediately
                    }
                } else {
                    objp->phys_info.flags &= ~PF_USE_VEL;
                }
            }

            // in multiplayer, if this object was just updatd (i.e. clients send their own positions),
            // then reset the flag and don't move the object.
            if (MULTIPLAYER_MASTER && (objp->flags[Object::Object_Flags::Just_updated])) {
                objp->flags.remove(Object::Object_Flags::Just_updated);
                goto obj_maybe_fire;
            }

            physics_sim(&objp->pos, &objp->orient, &objp->phys_info, frametime );		// simulate the physics

            // if the object is the player object, do things that need to be done after the ship
            // is moved (like firing weapons, etc).  This routine will get called either single
            // or multiplayer.  We must find the player object to get to the control info field
            obj_maybe_fire:
            if ( (objp->flags[Object::Object_Flags::Player_ship]) && (objp->type != OBJ_OBSERVER) && (objp == Player_obj)) {
                player *pp;
                if(Player != nullptr){
                    pp = Player;
                    obj_player_fire_stuff( objp, pp->ci );
                }
            }

            // fire streaming weapons for ships in here - ALL PLAYERS, regardless of client, single player, server, whatever.
            // do stream weapon firing for all ships themselves.
            if(objp->type == OBJ_SHIP){
                ship_fire_primary(objp, 1, 0);
                has_fired = 1;
            }
        }
    }

    if(has_fired == -1){
        ship_stop_fire_primary(objp);	//if it hasn't fired do the "has just stoped fireing" stuff
    }

    //2D MODE
    //THIS IS A FREAKIN' HACK
    //Do not let ship change position on Y axis
    if(The_mission.flags[Mission::Mission_Flags::Mission_2d])
    {
        angles old_angles, new_angles;
        objp->pos.xyz.y = objp->last_pos.xyz.y;
        vm_extract_angles_matrix(&old_angles, &objp->last_orient);
        vm_extract_angles_matrix(&new_angles, &objp->orient);
        new_angles.p = old_angles.p;
        new_angles.b = old_angles.b;
        vm_angles_2_matrix(&objp->orient, &new_angles);

        //Phys stuff hack
        new_angles.h = old_angles.h;
        vm_angles_2_matrix(&objp->phys_info.last_rotmat, &new_angles);
        objp->phys_info.vel.xyz.y = 0.0f;
        objp->phys_info.desired_rotvel.xyz.x = 0;
        objp->phys_info.desired_rotvel.xyz.z = 0;
        objp->phys_info.desired_vel.xyz.y = 0.0f;
    }
}

MONITOR( NumObjects )

int Collisions_enabled = 1;

DCF_BOOL( collisions, Collisions_enabled )

/**
 * Move all objects for the current frame
 */
void obj_move_all(float frametime)
{
    TRACE_SCOPE(tracing::MoveObjects);

    SCP_vector<object*> cmeasure_list;
    const bool global_cmeasure_timer = (Cmeasures_homing_check > 0);

    Assertion(Cmeasures_homing_check >= 0, "Cmeasures_homing_check is %d in obj_move_all(); it should never be negative. Get a coder!\n", Cmeasures_homing_check);

    if (global_cmeasure_timer)
        Cmeasures_homing_check--;

    // Goober5000 - HACK HACK HACK
    // this function also resets the OF_DOCKED_ALREADY_HANDLED flag, to save trips
    // through the used object list
    obj_delete_all_that_should_be_dead();

    obj_merge_created_list();

    // Clear the table that tells which groups of weapons have cast light so far.
    if(!(Game_mode & GM_MULTIPLAYER) || (MULTIPLAYER_MASTER)) {
        obj_clear_weapon_group_id_list();
    }

    MONITOR_INC( NumObjects, Num_objects );

    for (object* objp = GET_FIRST(&obj_used_list); objp != END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp)) {
        // skip objects which should be dead
        if (objp->flags[Object::Object_Flags::Should_be_dead]) {
            continue;
        }

        // if this is an observer object, skip it
        if (objp->type == OBJ_OBSERVER) {
            continue;
        }

        // Compile a list of active countermeasures during an existing traversal of obj_used_list
        if (objp->type == OBJ_WEAPON) {
            weapon *wp = &Weapons[objp->instance];
            weapon_info *wip = &Weapon_info[wp->weapon_info_index];

            if (wip->wi_flags[Weapon::Info_Flags::Cmeasure]) {
                if ((wip->cmeasure_timer_interval > 0 && timestamp_elapsed(wp->cmeasure_timer))	// If it's timer-based and ready to pulse...
                    || (wip->cmeasure_timer_interval <= 0 && global_cmeasure_timer)) {	// ...or it's not and the global counter is active...
                    // ...then it's actively pulsing and we need to add objp to cmeasure_list.
                    cmeasure_list.push_back(objp);
                    if (wip->cmeasure_timer_interval > 0) {
                        // Reset the timer
                        wp->cmeasure_timer = timestamp(wip->cmeasure_timer_interval);
                    }
                }
            }
        }

        vec3d cur_pos = objp->pos;			// Save the current position

#ifdef OBJECT_CHECK
        obj_check_object( objp );
#endif

        // pre-move
        obj_move_all_pre(objp, frametime);

        // store last pos and orient
        objp->last_pos = cur_pos;
        objp->last_orient = objp->orient;

        // Goober5000 - skip objects which don't move, but only until they're destroyed
        if (!(objp->flags[Object::Object_Flags::Immobile] && objp->hull_strength > 0.0f)) {
            // if this is an object which should be interpolated in multiplayer, do so
            if (multi_oo_is_interp_object(objp)) {
                multi_oo_interp(objp);
            } else {
                // physics
                obj_move_call_physics(objp, frametime);
            }
        }

        // move post
        obj_move_all_post(objp, frametime);

        // Equipment script processing
        if (objp->type == OBJ_SHIP) {
            ship* shipp = &Ships[objp->instance];
            object* target;

            if (Ai_info[shipp->ai_index].target_objnum != -1)
                target = &Objects[Ai_info[shipp->ai_index].target_objnum];
            else
                target = nullptr;
            if (objp == Player_obj && Player_ai->target_objnum != -1)
                target = &Objects[Player_ai->target_objnum];

            Script_system.SetHookObjects(2, "User", objp, "Target", target);
            Script_system.RunCondition(CHA_ONWPEQUIPPED, objp);
            Script_system.RemHookVars(2, "User", "Target");
        }
    }

    // Now that we've moved all the objects, move all the models that use intrinsic rotations.  We do that here because we already handled the
    // ship models in obj_move_all_post, and this is more or less conceptually close enough to move the rest.  (Originally all models
    // were intrinsic-rotated here, but for sequencing reasons, intrinsic ship rotations must happen along with regular ship rotations.)
    model_do_intrinsic_rotations();

    //	After all objects have been moved, move all docked objects.
    object* objp = GET_FIRST(&obj_used_list);
    while( objp !=END_OF_LIST(&obj_used_list) )	{
        dock_move_docked_objects(objp);

        //Valathil - Move the screen rotation calculation for billboards here to get the updated orientation matrices caused by docking interpolation
        vec3d tangles;

        tangles.xyz.x = -objp->phys_info.rotvel.xyz.x*frametime;
        tangles.xyz.y = -objp->phys_info.rotvel.xyz.y*frametime;
        tangles.xyz.z = objp->phys_info.rotvel.xyz.z*frametime;

        // If this is the viewer_object, keep track of the
        // changes in banking so that rotated bitmaps look correct.
        // This is used by the g3_draw_rotated_bitmap function.
        extern physics_info *Viewer_physics_info;
        if ( &objp->phys_info == Viewer_physics_info )	{
            vec3d tangles_r;
            vm_vec_unrotate(&tangles_r, &tangles, &Eye_matrix);
            vm_vec_rotate(&tangles, &tangles_r, &objp->orient);

            if(objp->dock_list && objp->dock_list->docked_objp->type == OBJ_SHIP && Ai_info[Ships[objp->dock_list->docked_objp->instance].ai_index].submode == AIS_DOCK_4) {
                Physics_viewer_bank -= tangles.xyz.z*0.65f;
            } else {
                Physics_viewer_bank -= tangles.xyz.z;
            }

            if ( Physics_viewer_bank < 0.0f ){
                Physics_viewer_bank += 2.0f * PI;
            }

            if ( Physics_viewer_bank > 2.0f * PI ){
                Physics_viewer_bank -= 2.0f * PI;
            }
        }

        // unflag all objects as being updates
        objp->flags.remove(Object::Object_Flags::Just_updated);

        objp = GET_NEXT(objp);
    }

    if (!cmeasure_list.empty())
        find_homing_object_cmeasures(cmeasure_list);	//	If any cmeasures are active, maybe steer away homing missiles

    // do pre-collision stuff for beam weapons
    beam_move_all_pre();

    if ( Collisions_enabled ) {
        TRACE_SCOPE(tracing::CollisionDetection);
        obj_sort_and_collide();
    }

    turret_swarm_check_validity();

    // do post-collision stuff for beam weapons
    beam_move_all_post();

    // update artillery locking info now
    ship_update_artillery_lock();
}

/**
 * Do client-side pre-interpolation object movement
 */
void obj_client_pre_interpolate()
{
    object *objp;

    // duh
    obj_delete_all_that_should_be_dead();

    // client side processing of warping in effect stages
    multi_do_client_warp(flFrametime);

    // client side movement of an observer
    if((Net_player->flags & NETINFO_FLAG_OBSERVER) || (Player_obj->type == OBJ_OBSERVER)){
        obj_observer_move(flFrametime);
    }

    // run everything except ships through physics (and ourselves of course)
    obj_merge_created_list();						// must merge any objects created by the host!

    objp = GET_FIRST(&obj_used_list);
    for ( objp = GET_FIRST(&obj_used_list); objp !=END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp) )	{
        if((objp != Player_obj) && (objp->type == OBJ_SHIP)){
            continue;
        }

        // for all non-dead object which are _not_ ships
        if ( !(objp->flags[Object::Object_Flags::Should_be_dead]) )	{
            // pre-move step
            obj_move_all_pre(objp, flFrametime);

            // store position and orientation
            objp->last_pos = objp->pos;
            objp->last_orient = objp->orient;

            // call physics
            obj_move_call_physics(objp, flFrametime);

            // post-move step
            obj_move_all_post(objp, flFrametime);
        }
    }
}

/**
 * Do client-side post-interpolation object movement
 */
void obj_client_post_interpolate()
{
    object *objp;

    //	After all objects have been moved, move all docked objects.
    objp = GET_FIRST(&obj_used_list);
    while( objp !=END_OF_LIST(&obj_used_list) )	{
        if ( objp != Player_obj ) {
            dock_move_docked_objects(objp);
        }
        objp = GET_NEXT(objp);
    }

    // check collisions
    obj_sort_and_collide();

    // do post-collision stuff for beam weapons
    beam_move_all_post();
}


void obj_observer_move(float frame_time)
{
    object *objp;
    float ft;

    // if i'm not in multiplayer, or not an observer, bail
    if(!(Game_mode & GM_MULTIPLAYER) || (Net_player == nullptr) || !(Net_player->flags & NETINFO_FLAG_OBSERVER) || (Player_obj->type != OBJ_OBSERVER)){
        return;
    }

    objp = Player_obj;

    objp->last_pos = objp->pos;
    objp->last_orient = objp->orient;		// save the orientation -- useful in multiplayer.

    ft = flFrametime;
    obj_move_call_physics( objp, ft );
    obj_move_all_post(objp, frame_time);
    objp->flags.remove(Object::Object_Flags::Just_updated);
}

/**
 * Reset all collisions
 */
void obj_reset_all_collisions()
{
    // clear checkobjects
#ifndef NDEBUG
    for (auto & CheckObject : CheckObjects) {
        CheckObject = checkobject();
    }
#endif

    // clear object pairs
    obj_reset_colliders();

    // now add every object back into the object collision pairs
    object* moveup = GET_FIRST(&obj_used_list);
    while(moveup != END_OF_LIST(&obj_used_list)){
        // he's not in the collision list
        moveup->flags.set(Object::Object_Flags::Not_in_coll);

        // recalc pairs for this guy
        obj_add_collider(OBJ_INDEX(moveup));

        // next
        moveup = GET_NEXT(moveup);
    }
}


/**
 * Call this if you want to change an object flag so that the
 * object code knows what's going on.  For instance if you turn
 * off OF_COLLIDES, the object code needs to know this in order to
 * actually turn the object collision detection off.  By calling
 * this you shouldn't get Int3's in the checkobject code.  If you
 * do, then put code in here to correctly handle the case.
 */
void obj_set_flags( object *obj, const flagset<Object::Object_Flags>& new_flags )
{
    int objnum = OBJ_INDEX(obj);

    // turning collision detection off
    if ( (obj->flags[Object::Object_Flags::Collides]) && (!(new_flags[Object::Object_Flags::Collides])))	{
        // Remove all object pairs
        obj_remove_collider(objnum);

        // update object flags properly
        obj->flags = new_flags;
        obj->flags.set(Object::Object_Flags::Not_in_coll);
#ifdef OBJECT_CHECK
        CheckObjects[objnum].flags = new_flags;
        CheckObjects[objnum].flags.set(Object::Object_Flags::Not_in_coll);
#endif
        return;
    }


    // turning collision detection on
    if ( (!(obj->flags[Object::Object_Flags::Collides])) && (new_flags[Object::Object_Flags::Collides]) )	{

        // observers can't collide or be hit, and they therefore have no hit or collide functions
        // So, don't allow this bit to be set
        if(obj->type == OBJ_OBSERVER){
            mprintf(("Illegal to set collision bit for OBJ_OBSERVER!!\n"));
            Int3();
        }

        obj->flags.set(Object::Object_Flags::Collides);

        // Turn on collision detection
        obj_add_collider(objnum);

        obj->flags = new_flags;
        obj->flags.remove(Object::Object_Flags::Not_in_coll);
#ifdef OBJECT_CHECK
        CheckObjects[objnum].flags = new_flags;
        CheckObjects[objnum].flags.remove(Object::Object_Flags::Not_in_coll);
#endif
        return;
    }

    // for a multiplayer host -- use this debug code to help trap when non-player ships are getting
    // marked as OF_COULD_BE_PLAYER
    // this code is pretty much debug code and shouldn't be relied on to always do the right thing
    // for flags other than
    if ( MULTIPLAYER_MASTER && !(obj->flags[Object::Object_Flags::Could_be_player]) && (new_flags[Object::Object_Flags::Could_be_player]) ) {
        int team, slot;

        // this flag sometimes gets set for observers.
        if ( obj->type == OBJ_OBSERVER ) {
            return;
        }

        // sanity checks
        if ( (obj->type != OBJ_SHIP) || (obj->instance < 0) ) {
            return;				// return because we really don't want to set the flag
        }

        // see if this ship is really a player ship (or should be)
        ship* shipp = &Ships[obj->instance];
        extern void multi_ts_get_team_and_slot(char *, int *, int *, bool);
        multi_ts_get_team_and_slot(shipp->ship_name,&team,&slot, false);
        if ( (shipp->wingnum == -1) || (team == -1) || (slot==-1) ) {
            Int3();
            return;
        }

        // set the flag
        obj->flags = new_flags;
#ifdef OBJECT_CHECK
        CheckObjects[objnum].flags = new_flags;
#endif

        return;
    }

    // Check for unhandled flag changing
    if ( (new_flags[Object::Object_Flags::Collides]) != (obj->flags[Object::Object_Flags::Collides]) ) {
        mprintf(( "Unhandled flag changing in obj_set_flags!!\n" ));
        mprintf(( "Add code to support it, see John for questions!!\n" ));
        Int3();
    } else {
        // Since it wasn't an important flag, just bash it.
        obj->flags = new_flags;
#ifdef OBJECT_CHECK
        CheckObjects[objnum].flags = new_flags;
#endif
    }
}
