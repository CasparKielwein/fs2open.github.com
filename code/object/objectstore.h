#ifndef FS2_OPEN_OBJECTSTORE_H
#define FS2_OPEN_OBJECTSTORE_H

class object;

extern object Objects[];
extern int Highest_object_index;		//highest objnum
extern int Highest_ever_object_index;
extern object obj_free_list;
extern object obj_used_list;
extern object obj_create_list;

extern object *Viewer_obj;	// Which object is the viewer. Can be NULL.
extern object *Player_obj;	// Which object is the player. Has to be valid.

// The next signature for the next newly created object. Zero is bogus
extern int Object_next_signature;
extern int Num_objects;

// Use this instead of "objp - Objects" to get an object number
// given it's pointer.  This way, we can replace it with a macro
// to check that the pointer is valid for debugging.
// This code will break in 64 bit builds when we have more than 2^31 objects but that will probably never happen
#define OBJ_INDEX(objp) (int)(objp-Objects)


//do whatever setup needs to be done
void obj_init();

void obj_shutdown();

#endif //FS2_OPEN_OBJECTSTORE_H
