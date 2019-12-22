/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 




#include "asteroid/asteroid.h"
#include "cmeasure/cmeasure.h"
#include "debris/debris.h"
#include "debugconsole/console.h"
#include "fireball/fireballs.h"
#include "freespace.h"
#include "globalincs/linklist.h"
#include "iff_defs/iff_defs.h"
#include "io/timer.h"
#include "jumpnode/jumpnode.h"
#include "lighting/lighting.h"
#include "mission/missionparse.h" //For 2D Mode
#include "network/multi.h"
#include "network/multiutil.h"
#include "object/deadobjectdock.h"
#include "object/objcollide.h"
#include "object/object.h"
#include "object/objectdock.h"
#include "object/objectshield.h"
#include "object/objectsnd.h"
#include "observer/observer.h"
#include "scripting/scripting.h"
#include "playerman/player.h"
#include "radar/radarsetup.h"
#include "render/3d.h"
#include "ship/afterburner.h"
#include "ship/ship.h"
#include "tracing/tracing.h"
#include "weapon/beam.h"
#include "weapon/shockwave.h"
#include "weapon/swarm.h"
#include "weapon/weapon.h"
#include "tracing/Monitor.h"
#include "graphics/light.h"


/*
 *  Global variables
 */


int Object_next_signature = 1;	//0 is bogus, start at 1
int Object_inited = 0;
int Show_waypoints = 0;

//WMC - Made these prettier
const char *Object_type_names[MAX_OBJECT_TYPES] = {
//XSTR:OFF
	"None",
	"Ship",
	"Weapon",
	"Fireball",
	"Start",
	"Waypoint",
	"Debris",
	"Countermeasure",
	"Ghost",
	"Point",
	"Shockwave",
	"Wing",
	"Observer",
	"Asteroid",
	"Jump Node",
	"Beam",
//XSTR:ON
};

obj_flag_name Object_flag_names[] = {
    { Object::Object_Flags::Invulnerable,			"invulnerable",				1,	},
	{ Object::Object_Flags::Protected,				"protect-ship",				1,	},
	{ Object::Object_Flags::Beam_protected,			"beam-protect-ship",		1,	},
	{ Object::Object_Flags::No_shields,				"no-shields",				1,	},
	{ Object::Object_Flags::Targetable_as_bomb,		"targetable-as-bomb",		1,	},
	{ Object::Object_Flags::Flak_protected,			"flak-protect-ship",		1,	},
	{ Object::Object_Flags::Laser_protected,		"laser-protect-ship",		1,	},
	{ Object::Object_Flags::Missile_protected,		"missile-protect-ship",		1,	},
	{ Object::Object_Flags::Immobile,				"immobile",					1,	},
	{ Object::Object_Flags::Collides,				"collides",					1,  },
};

#ifdef OBJECT_CHECK
checkobject::checkobject() 
    : type(0), signature(0), parent_sig(0), parent_type(0) 
{
    flags.reset();
}
#endif

// all we need to set are the pointers, but type, parent, and instance are useful to set as well
object::object()
	: next(nullptr), prev(nullptr), type(OBJ_NONE), parent(-1), instance(-1), n_quadrants(0), hull_strength(0.0),
	  sim_hull_strength(0.0), net_signature(0), num_pairs(0), dock_list(nullptr), dead_dock_list(nullptr), collision_group_id(0)
{
	memset(&(this->phys_info), 0, sizeof(physics_info));
}

object::~object()
{
	objsnd_num.clear();

	dock_free_dock_list(this);
	dock_free_dead_dock_list(this);
}

// DO NOT set next and prev to nullptr because they keep the object on the free and used lists
void object::clear()
{
	signature = num_pairs = collision_group_id = 0;
	parent = parent_sig = instance = -1;
	type = parent_type = OBJ_NONE;
    flags.reset();
	pos = last_pos = vmd_zero_vector;
	orient = last_orient = vmd_identity_matrix;
	radius = hull_strength = sim_hull_strength = 0.0f;
	physics_init( &phys_info );
	shield_quadrant.clear();
	objsnd_num.clear();
	net_signature = 0;

	pre_move_event.clear();
	post_move_event.clear();

	// just in case nobody called obj_delete last mission
	dock_free_dock_list(this);
	dock_free_dead_dock_list(this);
}

/**
 * Scan the object list, freeing down to num_used objects
 *
 * @param  num_used Number of used objects to free down to
 * @return Returns number of slots freed
 */
int free_object_slots(int num_used)
{
	int	olind = 0;
	int	obj_list[MAX_OBJECTS];

	// calc num_already_free by walking the obj_free_list
	int num_already_free = 0;
	for ( object* objp = GET_FIRST(&obj_free_list); objp != END_OF_LIST(&obj_free_list); objp = GET_NEXT(objp) )
		num_already_free++;

	if (MAX_OBJECTS - num_already_free < num_used)
		return 0;

	for ( object* objp = GET_FIRST(&obj_used_list); objp != END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp) ) {
		if (objp->flags[Object::Object_Flags::Should_be_dead]) {
			num_already_free++;
			if (MAX_OBJECTS - num_already_free < num_used)
				return num_already_free;
		} else
			switch (objp->type) {
				case OBJ_NONE:
					num_already_free++;
					if (MAX_OBJECTS - num_already_free < num_used)
						return 0;
					break;
				case OBJ_FIREBALL:
				case OBJ_WEAPON:
				case OBJ_DEBRIS:
					obj_list[olind++] = OBJ_INDEX(objp);
					break;

				case OBJ_GHOST:
				case OBJ_SHIP:
				case OBJ_START:
				case OBJ_WAYPOINT:
				case OBJ_POINT:
				case OBJ_SHOCKWAVE:
				case OBJ_WING:
				case OBJ_OBSERVER:
				case OBJ_ASTEROID:
				case OBJ_JUMP_NODE:
				case OBJ_BEAM:
					break;
				default:
					Int3();	//	Hey, what kind of object is this?  Unknown!
					break;
			}
	}

	int num_to_free = MAX_OBJECTS - num_used - num_already_free;
	int original_num_to_free = num_to_free;

	if (num_to_free > olind) {
		nprintf(("allender", "Warning: Asked to free %i objects, but can only free %i.\n", num_to_free, olind));
		num_to_free = olind;
	}

	for (int i=0; i<num_to_free; i++)
		if ( (Objects[obj_list[i]].type == OBJ_DEBRIS) && (Debris[Objects[obj_list[i]].instance].flags & DEBRIS_EXPIRE) ) {
			num_to_free--;
			nprintf(("allender", "Freeing   DEBRIS object %3i\n", obj_list[i]));
			Objects[obj_list[i]].flags.set(Object::Object_Flags::Should_be_dead);
		}

	if (!num_to_free)
		return original_num_to_free;

	for (int i=0; i<num_to_free; i++)	{
		object *tmp_obj = &Objects[obj_list[i]];
		if ( (tmp_obj->type == OBJ_FIREBALL) && (fireball_is_perishable(tmp_obj)) ) {
			num_to_free--;
			nprintf(("allender", "Freeing FIREBALL object %3i\n", obj_list[i]));
			tmp_obj->flags.set(Object::Object_Flags::Should_be_dead);
		}
	}

	if (!num_to_free){
		return original_num_to_free;
	}

	int deleted_weapons = collide_remove_weapons();

	num_to_free -= deleted_weapons;
	if ( !num_to_free ){
		return original_num_to_free;
	}

	for (int i=0; i<num_to_free; i++){
		if ( Objects[obj_list[i]].type == OBJ_WEAPON ) {
			num_to_free--;
			Objects[obj_list[i]].flags.set(Object::Object_Flags::Should_be_dead);
		}
	}

	if (!num_to_free){
		return original_num_to_free;
	}

	return original_num_to_free - num_to_free;
}

// Goober5000
float get_hull_pct(object *objp)
{
	Assert(objp);
	Assert(objp->type == OBJ_SHIP);

	float total_strength = Ships[objp->instance].ship_max_hull_strength;

	Assert(total_strength > 0.0f);	// unlike shield, no ship can have 0 hull

	if (total_strength == 0.0f)
		return 0.0f;

	if (objp->hull_strength < 0.0f)	// this sometimes happens when a ship is being destroyed
		return 0.0f;

	return objp->hull_strength / total_strength;
}

float get_sim_hull_pct(object *objp)
{
	Assert(objp);
	Assert(objp->type == OBJ_SHIP);

	float total_strength = Ships[objp->instance].ship_max_hull_strength;

	Assert(total_strength > 0.0f);	// unlike shield, no ship can have 0 hull

	if (total_strength == 0.0f)
		return 0.0f;

	if (objp->sim_hull_strength < 0.0f)	// this sometimes happens when a ship is being destroyed
		return 0.0f;

	return objp->sim_hull_strength / total_strength;
}

// Goober5000
float get_shield_pct(object *objp)
{
	Assert(objp);

	// bah - we might have asteroids
	if (objp->type != OBJ_SHIP)
		return 0.0f;

	float total_strength = shield_get_max_strength(objp);

	if (total_strength == 0.0f)
		return 0.0f;

	return shield_get_strength(objp) / total_strength;
}

static int num_objects_hwm = 0;

/** 
 * Allocates an object
 *
 * Generally, obj_create() should be called to get an object, since it
 * fills in important fields and does the linking.
 *
 * @return the number of a free object, updating Highest_object_index
 * @return -1 if no free objects
 */
int obj_allocate()
{
	if (!Object_inited) {
		mprintf(("Why hasn't obj_init() been called yet?\n"));
		obj_init();
	}

	if ( Num_objects >= MAX_OBJECTS-10 ) {
		int	num_freed = free_object_slots(MAX_OBJECTS-10);
		nprintf(("warning", " *** Freed %i objects\n", num_freed));
	}

	if (Num_objects >= MAX_OBJECTS) {
		#ifndef NDEBUG
		mprintf(("Object creation failed - too many objects!\n" ));
		#endif
		return -1;
	}

	// Find next available object
	object* objp = GET_FIRST(&obj_free_list);
	Assert ( objp != &obj_free_list );		// shouldn't have the dummy element

	// remove objp from the free list
	list_remove( &obj_free_list, objp );
	
	// insert objp onto the end of create list
	list_append( &obj_create_list, objp );

	// increment counter
	Num_objects++;

	if (Num_objects > num_objects_hwm) {
		num_objects_hwm = Num_objects;
	}

	// get objnum
    int objnum = OBJ_INDEX(objp);

	if (objnum > Highest_object_index) {
		Highest_object_index = objnum;
		if (Highest_object_index > Highest_ever_object_index)
			Highest_ever_object_index = Highest_object_index;
	}

	return objnum;
}

/**
 * Frees up an object  
 *
 * Generally, obj_delete() should be called to get rid of an object.
 * This function deallocates the object entry after the object has been unlinked
 */
void obj_free(int objnum)
{
	if (!Object_inited) {
		mprintf(("Why hasn't obj_init() been called yet?\n"));
		obj_init();
	}

	Assert( objnum >= 0 );	// Trying to free bogus object!!!

	// get object pointer
    object* objp = &Objects[objnum];

	// remove objp from the used list
	list_remove( &obj_used_list, objp );

	// add objp to the end of the free
	list_append( &obj_free_list, objp );

	// decrement counter
	Num_objects--;

	Objects[objnum].type = OBJ_NONE;

	Assert(Num_objects >= 0);

	if (objnum == Highest_object_index) {
		while (Highest_object_index >= 0 && Objects[Highest_object_index].type == OBJ_NONE) {
			--Highest_object_index;
		}
	}
}

/**
 * Initialize a new object. Adds to the list for the given segment.
 *
 * The object will be a non-rendering, non-physics object.   Pass -1 if no parent.
 * @return the object number 
 */
int obj_create(ubyte type,int parent_obj,int instance, matrix * orient, 
               vec3d * pos, float radius, const flagset<Object::Object_Flags> &flags )
{
	// Find next free object
	int objnum = obj_allocate();

	if (objnum == -1)		//no free objects
		return -1;

    object *obj = &Objects[objnum];
	Assert(obj->type == OBJ_NONE);		//make sure unused 

	// clear object in preparation for setting of custom values
	obj->clear();

	Assert(Object_next_signature > 0);	// 0 is bogus!
	obj->signature = Object_next_signature++;

	obj->type 					= type;
	obj->instance				= instance;
	obj->parent					= parent_obj;
	if (obj->parent != -1)	{
		obj->parent_sig		= Objects[parent_obj].signature;
		obj->parent_type		= Objects[parent_obj].type;
	} else {
		obj->parent_sig = obj->signature;
		obj->parent_type = obj->type;
	}

	obj->flags 					= flags;
    obj->flags.set(Object::Object_Flags::Not_in_coll);
	if (pos)	{
		obj->pos 				= *pos;
		obj->last_pos			= *pos;
	}

	if (orient)	{
		obj->orient 			= *orient;
		obj->last_orient		= *orient;
	}
	obj->radius 				= radius;

	obj->n_quadrants = DEFAULT_SHIELD_SECTIONS; // Might be changed by the ship creation code
	obj->shield_quadrant.resize(obj->n_quadrants);

	return objnum;
}

/**
 * Remove object from the world
 * If Player_obj, don't remove it!
 * 
 * @param objnum Object number to remove
 */
void obj_delete(int objnum)
{
	Assert(objnum >= 0 && objnum < MAX_OBJECTS);
	object* objp = &Objects[objnum];
	if (objp->type == OBJ_NONE) {
		mprintf(("obj_delete() called for already deleted object %d.\n", objnum));
		return;
	};	

	// Remove all object pairs
	obj_remove_collider(objnum);

	switch( objp->type )	{
	case OBJ_WEAPON:
		weapon_delete( objp );
		break;
	case OBJ_SHIP:
		if ((objp == Player_obj) && !Fred_running) {
			objp->type = OBJ_GHOST;
            objp->flags.remove(Object::Object_Flags::Should_be_dead);
			
			// we have to traverse the ship_obj list and remove this guy from it as well
			ship_obj *moveup = GET_FIRST(&Ship_obj_list);
			while(moveup != END_OF_LIST(&Ship_obj_list)){
				if(OBJ_INDEX(objp) == moveup->objnum){
					list_remove(&Ship_obj_list,moveup);
					break;
				}
				moveup = GET_NEXT(moveup);
			}

			physics_init(&objp->phys_info);
			obj_snd_delete_type(OBJ_INDEX(objp));
			return;
		} else
			ship_delete( objp );
		break;
	case OBJ_FIREBALL:
		fireball_delete( objp );
		break;
	case OBJ_SHOCKWAVE:
		shockwave_delete( objp );
		break;
	case OBJ_START:
	case OBJ_WAYPOINT:
	case OBJ_POINT:
		break;  // requires no action, handled by the Fred code.
	case OBJ_JUMP_NODE:
		break;  // requires no further action, handled by jumpnode deconstructor.
	case OBJ_DEBRIS:
		debris_delete( objp );
		break;
	case OBJ_ASTEROID:
		asteroid_delete(objp);
		break;
/*	case OBJ_CMEASURE:
		cmeasure_delete( objp );
		break;*/
	case OBJ_GHOST:
		if(!(Game_mode & GM_MULTIPLAYER)){
			mprintf(("Warning: Tried to delete a ghost!\n"));
			objp->flags.remove(Object::Object_Flags::Should_be_dead);
			return;
		} else {
			// we need to be able to delete GHOST objects in multiplayer to allow for player respawns.
			nprintf(("Network","Deleting GHOST object\n"));
		}		
		break;
	case OBJ_OBSERVER:
		observer_delete(objp);
		break;	
	case OBJ_BEAM:
		break;
	case OBJ_NONE:
		Int3();
		break;
	default:
		Error( LOCATION, "Unhandled object type %d in obj_delete_all_that_should_be_dead", objp->type );
	}

	// delete any dock information we still have
	dock_free_dock_list(objp);
	dock_free_dead_dock_list(objp);

	// if a persistant sound has been created, delete it
	obj_snd_delete_type(OBJ_INDEX(objp));		

	objp->type = OBJ_NONE;		//unused!
	objp->signature = 0;

	obj_free(objnum);
}

/**
 * Add all newly created objects to the end of the used list and create their
 * object pairs for collision detection
 */
void obj_merge_created_list(void)
{
	// The old way just merged the two.   This code takes one out of the create list,
	// creates object pairs for it, and then adds it to the used list.
	//	OLD WAY: list_merge( &obj_used_list, &obj_create_list );
	object *objp = GET_FIRST(&obj_create_list);
	while( objp !=END_OF_LIST(&obj_create_list) )	{
		list_remove( obj_create_list, objp );

		// Add it to the object pairs array
		obj_add_collider(OBJ_INDEX(objp));

		// Then add it to the object used list
		list_append( &obj_used_list, objp );

		objp = GET_FIRST(&obj_create_list);
	}

	// Make sure the create list is empty.
	list_init(&obj_create_list);
}

int physics_paused = 0, ai_paused = 0;


// Goober5000
extern void call_doa(object *child, object *parent);
void obj_move_one_docked_object(object *objp, object *parent_objp)
{
	// in FRED, just move and return
	if (Fred_running)
	{
		call_doa(objp, parent_objp);
		return;
	}

	// support ships (and anyone else, for that matter) don't keep up if they're undocking
	ai_info *aip = &Ai_info[Ships[objp->instance].ai_index];
	if ( (aip->mode == AIM_DOCK) && (aip->submode >= AIS_UNDOCK_1) )
	{
		if (aip->goal_objnum == OBJ_INDEX(parent_objp))
		{
			return;
		}
	}

	// check the guy that I'm docked with and don't move if he's undocking from me
	ai_info *other_aip = &Ai_info[Ships[parent_objp->instance].ai_index];
	if ( (other_aip->mode == AIM_DOCK) && (other_aip->submode >= AIS_UNDOCK_1) )
	{
		if (other_aip->goal_objnum == OBJ_INDEX(objp))
		{
			return;
		}
	}

	// we're here, so we move with our parent object
	call_doa(objp, parent_objp);
}

/**
 * Deals with firing player things like lasers, missiles, etc.
 *
 * Separated out because of multiplayer issues.
*/
void obj_player_fire_stuff( object *objp, control_info ci )
{
	Assert( objp->flags[Object::Object_Flags::Player_ship]);

	// try and get the ship pointer
	ship* shipp = nullptr;
	if((objp->type == OBJ_SHIP) && (objp->instance >= 0) && (objp->instance < MAX_SHIPS)){
		shipp = &Ships[objp->instance];
	} else {
		return;
	}

	// single player pilots, and all players in multiplayer take care of firing their own primaries
	if(!(Game_mode & GM_MULTIPLAYER) || (objp == Player_obj))
	{
		if ( ci.fire_primary_count ) {
			// flag the ship as having the trigger down
			if(shipp != nullptr){
				shipp->flags.set(Ship::Ship_Flags::Trigger_down);
			}

			// fire non-streaming primaries here
			ship_fire_primary( objp, 0 );
		} else {
			// unflag the ship as having the trigger down
			if(shipp != nullptr){
                shipp->flags.remove(Ship::Ship_Flags::Trigger_down);
				ship_stop_fire_primary(objp);	//if it hasn't fired do the "has just stoped fireing" stuff
			}
		}

		if ( ci.fire_countermeasure_count ) {
			ship_launch_countermeasure( objp );
		}
	}

	// single player and multiplayer masters do all of the following
	if ( !MULTIPLAYER_CLIENT ) {		
		if ( ci.fire_secondary_count ) {
			ship_fire_secondary( objp );

			// kill the secondary count
			ci.fire_secondary_count = 0;
		}
	}

	// everyone does the following for their own ships.
	if ( ci.afterburner_start ){
		if (ship_get_subsystem_strength(&Ships[objp->instance], SUBSYSTEM_ENGINE)){
			afterburners_start( objp );
		}
	}
	
	if ( ci.afterburner_stop ){
		afterburners_stop( objp, 1 );
	}
	
}

MONITOR( NumObjectsRend )

/**
 * Render an object.  Calls one of several routines based on type
 */
extern int Cmdline_dis_weapons;

void obj_render(object *obj)
{
	model_draw_list render_list;

	obj_queue_render(obj, &render_list);

	render_list.init_render();
	render_list.render_all();

	gr_zbias(0);
	gr_set_cull(0);
	gr_zbuffer_set(ZBUFFER_TYPE_READ);
	gr_set_fill_mode(GR_FILL_MODE_SOLID);

	gr_clear_states();

	gr_reset_lighting();
	gr_set_lighting(false, false);
}

void obj_queue_render(object* obj, model_draw_list* scene)
{
	TRACE_SCOPE(tracing::QueueRender);

	if ( obj->flags[Object::Object_Flags::Should_be_dead] ) return;

	Script_system.SetHookObject("Self", obj);
	
	auto skip_render = Script_system.IsConditionOverride(CHA_OBJECTRENDER, obj);
	
	// Always execute the hook content
	Script_system.RunCondition(CHA_OBJECTRENDER, obj);

	Script_system.RemHookVar("Self");

	if (skip_render) {
		// Script said that it want's to skip rendering
		return;
	}

	switch ( obj->type ) {
	case OBJ_NONE:
#ifndef NDEBUG
		mprintf(( "ERROR!!!! Bogus obj " PTRDIFF_T_ARG " is rendering!\n", obj-Objects ));
		Int3();
#endif
		break;
	case OBJ_WEAPON:
		if ( Cmdline_dis_weapons ) return;
		weapon_render(obj, scene);
		break;
	case OBJ_SHIP:
		ship_render(obj, scene);
		break;
	case OBJ_FIREBALL:
		fireball_render(obj, scene);
		break;
	case OBJ_SHOCKWAVE:
		shockwave_render(obj, scene);
		break;
	case OBJ_DEBRIS:
		debris_render(obj, scene);
		break;
	case OBJ_ASTEROID:
		asteroid_render(obj, scene);
		break;
	case OBJ_JUMP_NODE:
		for (auto & Jump_node : Jump_nodes) {
			if ( Jump_node.GetSCPObject() != obj ) {
				continue;
			}

			Jump_node.Render(scene, &obj->pos, &Eye_position);
		}
		break;
	case OBJ_WAYPOINT:
		// 		if (Show_waypoints)	{
		// 			gr_set_color( 128, 128, 128 );
		// 			g3_draw_sphere_ez( &obj->pos, 5.0f );
		// 		}
		break;
	case OBJ_GHOST:
		break;
	case OBJ_BEAM:
		break;
	default:
		Error( LOCATION, "Unhandled obj type %d in obj_render", obj->type );
	}
}

void obj_init_all_ships_physics()
{
	for (object* objp = GET_FIRST(&obj_used_list); objp !=END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp) ) {
		if (objp->type == OBJ_SHIP)
			physics_ship_init(objp);
	}
}

/**
 * Returns a vector of the average position of all ships in the mission.
 */
void obj_get_average_ship_pos( vec3d *pos )
{
	vm_vec_zero( pos );

   // average up all ship positions
	int count = 0;
	for ( object* objp = GET_FIRST(&obj_used_list); objp != END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp) ) {
		if ( objp->type != OBJ_SHIP )
			continue;
		vm_vec_add2( pos, &objp->pos );
		count++;
	}

	if ( count )
		vm_vec_scale( pos, 1.0f/(float)count );
}

/**
 * Return the team for the object passed as a parameter
 *
 * @param objp Pointer to object that you want team for
 *
 * @return enumerated team on success
 * @return -1 on failure (for objects that don't have teams)
 */
int obj_team(object *objp)
{
	Assert( objp != nullptr );
	int team = -1;

	switch ( objp->type ) {
		case OBJ_SHIP:
			Assert( objp->instance >= 0 && objp->instance < MAX_SHIPS );
			team = Ships[objp->instance].team;
			break;

		case OBJ_DEBRIS:
			team = debris_get_team(objp);
			Assertion(team != -1, "Obj_team called for a debris object with no team.");
			break;

/*		case OBJ_CMEASURE:
			Assert( objp->instance >= 0 && objp->instance < MAX_CMEASURES);
			team = Cmeasures[objp->instance].team;
			break;
*/
		case OBJ_WEAPON:
			Assert( objp->instance >= 0 && objp->instance < MAX_WEAPONS );
			team = Weapons[objp->instance].team;
			break;

		case OBJ_JUMP_NODE:
			team = Player_ship->team;
			break;
					
		case OBJ_FIREBALL:
		case OBJ_WAYPOINT:
		case OBJ_START:
		case OBJ_NONE:
		case OBJ_GHOST:
		case OBJ_SHOCKWAVE:		
		case OBJ_BEAM:
			team = -1;
			break;

		case OBJ_ASTEROID:
			team = Iff_traitor;
			break;

		default:
			Int3();	// can't happen
			break;
	} // end switch

	Assertion(team != -1, "Obj_team called for a object of type %s with no team.",  Object_type_names[objp->type]);
	return team;
}

// Goober5000
int object_is_docked(object *objp)
{
	return (objp->dock_list != nullptr);
}

// Goober5000
int object_is_dead_docked(object *objp)
{
	return (objp->dead_dock_list != nullptr);
}

/**
 * Makes an object start 'gliding'
 *
 * It will continue on the same velocity that it was going,
 * regardless of orientation -WMC
 */
void object_set_gliding(object *objp, bool enable, bool force)
{
	Assert(objp != nullptr);

	if(enable) {
		if (!force) {
			objp->phys_info.flags |= PF_GLIDING;
		} else {
			objp->phys_info.flags |= PF_FORCE_GLIDE;
		}
	} else {
		if (!force) {
			objp->phys_info.flags &= ~PF_GLIDING;
		} else {
			objp->phys_info.flags &= ~PF_FORCE_GLIDE;
		}
		vm_vec_rotate(&objp->phys_info.prev_ramp_vel, &objp->phys_info.vel, &objp->orient);	//Backslash
	}
}

/**
 * @return whether an object is gliding -WMC
 */
bool object_get_gliding(object *objp)
{
	Assert(objp != nullptr);

	return ( ((objp->phys_info.flags & PF_GLIDING) != 0) || ((objp->phys_info.flags & PF_FORCE_GLIDE) != 0));
}

bool object_glide_forced(object *objp)
{
	return (objp->phys_info.flags & PF_FORCE_GLIDE) != 0;
}

/**
 * Quickly finds an object by its signature
 */
int obj_get_by_signature(int sig)
{
	Assert(sig > 0);

	object *objp = GET_FIRST(&obj_used_list);
	while( objp !=END_OF_LIST(&obj_used_list) )
	{
		if(objp->signature == sig)
			return OBJ_INDEX(objp);

		objp = GET_NEXT(objp);
	}
	return -1;
}

/**
 * Gets object model
 */
int object_get_model(object *objp)
{
	switch(objp->type)
	{
		case OBJ_ASTEROID:
		{
			asteroid *asp = &Asteroids[objp->instance];
			return Asteroid_info[asp->asteroid_type].model_num[asp->asteroid_subtype];
		}
		case OBJ_DEBRIS:
		{
			debris *debrisp = &Debris[objp->instance];
			return debrisp->model_num;
		}
		case OBJ_SHIP:
		{
			ship *shipp = &Ships[objp->instance];
			return Ship_info[shipp->ship_info_index].model_num;
		}
		case OBJ_WEAPON:
		{
			weapon *wp = &Weapons[objp->instance];
			return Weapon_info[wp->weapon_info_index].model_num;
		}
		default:
			break;
	}

	return -1;
}
bool obj_compare(object* left, object* right) {
	if (left == right) {
		// Same pointer
		return true;
	}
	if (left == nullptr || right == nullptr) {
		// Only one is nullptr and the other is not (since they are not equal)
		return false;
	}

	return OBJ_INDEX(left) == OBJ_INDEX(right);
}
