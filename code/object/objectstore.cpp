//
// Created by ckielwein on 2019-12-22.
//

#include <globalincs/linklist.h>
#include <scripting/scripting.h>
#include "objectstore.h"
#include "object.h"
#include "objcollide.h"

object obj_free_list;
object obj_used_list;
object obj_create_list;

object *Player_obj = nullptr;
object *Viewer_obj = nullptr;

//Data for objects
object Objects[MAX_OBJECTS];

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
