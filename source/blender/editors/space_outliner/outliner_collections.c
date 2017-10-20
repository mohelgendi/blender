/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Blender Foundation, Dalai Felinto
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/space_outliner/outliner_collections.c
 *  \ingroup spoutliner
 */

#include "BKE_context.h"
#include "BKE_collection.h"
#include "BKE_layer.h"
#include "BKE_main.h"
#include "BKE_report.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"

#include "DNA_group_types.h"

#include "BLI_listbase.h"

#include "ED_screen.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "UI_resources.h"

#include "outliner_intern.h" /* own include */

/* -------------------------------------------------------------------- */

static LayerCollection *outliner_collection_active(bContext *C)
{
	TODO_LAYER_OPERATORS;
	/* consider that we may have overrides or objects active
	 * leading to no active collections */
	return CTX_data_layer_collection(C);
}

SceneCollection *outliner_scene_collection_from_tree_element(TreeElement *te)
{
	TreeStoreElem *tselem = TREESTORE(te);

	if (tselem->type == TSE_SCENE_COLLECTION) {
		return te->directdata;
	}
	else if (tselem->type == TSE_LAYER_COLLECTION) {
		LayerCollection *lc = te->directdata;
		return lc->scene_collection;
	}

	return NULL;
}

#if 0
static CollectionOverride *outliner_override_active(bContext *UNUSED(C))
{
	TODO_LAYER_OPERATORS;
	TODO_LAYER_OVERRIDE;
	return NULL;
}
#endif

/* -------------------------------------------------------------------- */
/* collection manager operators */

/**
 * Recursively get the collection for a given index
 */
static SceneCollection *scene_collection_from_index(ListBase *lb, const int number, int *i)
{
	for (SceneCollection *sc = lb->first; sc; sc = sc->next) {
		if (*i == number) {
			return sc;
		}

		(*i)++;

		SceneCollection *sc_nested = scene_collection_from_index(&sc->scene_collections, number, i);
		if (sc_nested) {
			return sc_nested;
		}
	}
	return NULL;
}

static int collection_link_exec(bContext *C, wmOperator *op)
{
	Scene *scene = CTX_data_scene(C);
	SceneLayer *sl = CTX_data_scene_layer(C);
	SceneCollection *sc_master = BKE_collection_master(scene);
	SceneCollection *sc;

	int scene_collection_index = RNA_enum_get(op->ptr, "scene_collection");
	if (scene_collection_index == 0) {
		sc = sc_master;
	}
	else {
		int index = 1;
		sc = scene_collection_from_index(&sc_master->scene_collections, scene_collection_index, &index);
		BLI_assert(sc);
	}

	BKE_collection_link(sl, sc);

	DEG_relations_tag_update(CTX_data_main(C));

	/* TODO(sergey): Use proper flag for tagging here. */
	DEG_id_tag_update(&scene->id, 0);

	WM_main_add_notifier(NC_SCENE | ND_LAYER, NULL);
	return OPERATOR_FINISHED;
}

static int collection_link_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	if (BKE_collection_master(CTX_data_scene(C))->scene_collections.first == NULL) {
		RNA_enum_set(op->ptr, "scene_collection", 0);
		return collection_link_exec(C, op);
	}
	else {
		return WM_enum_search_invoke(C, op, event);
	}
}

static void collection_scene_collection_itemf_recursive(
        EnumPropertyItem *tmp, EnumPropertyItem **item, int *totitem, int *value, SceneCollection *sc)
{
	tmp->value = *value;
	tmp->icon = ICON_COLLAPSEMENU;
	tmp->identifier = sc->name;
	tmp->name = sc->name;
	RNA_enum_item_add(item, totitem, tmp);

	(*value)++;

	for (SceneCollection *ncs = sc->scene_collections.first; ncs; ncs = ncs->next) {
		collection_scene_collection_itemf_recursive(tmp, item, totitem, value, ncs);
	}
}

static const EnumPropertyItem *collection_scene_collection_itemf(
        bContext *C, PointerRNA *UNUSED(ptr), PropertyRNA *UNUSED(prop), bool *r_free)
{
	EnumPropertyItem tmp = {0, "", 0, "", ""};
	EnumPropertyItem *item = NULL;
	int value = 0, totitem = 0;

	Scene *scene = CTX_data_scene(C);
	SceneCollection *sc = BKE_collection_master(scene);

	collection_scene_collection_itemf_recursive(&tmp, &item, &totitem, &value, sc);
	RNA_enum_item_end(&item, &totitem);
	*r_free = true;

	return item;
}

void OUTLINER_OT_collection_link(wmOperatorType *ot)
{
	PropertyRNA *prop;

	/* identifiers */
	ot->name = "Add Collection";
	ot->idname = "OUTLINER_OT_collection_link";
	ot->description = "Link a new collection to the active layer";

	/* api callbacks */
	ot->exec = collection_link_exec;
	ot->invoke = collection_link_invoke;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	prop = RNA_def_enum(ot->srna, "scene_collection", DummyRNA_NULL_items, 0, "Scene Collection", "");
	RNA_def_enum_funcs(prop, collection_scene_collection_itemf);
	RNA_def_property_flag(prop, PROP_ENUM_NO_TRANSLATE);
	ot->prop = prop;
}

/**
 * Returns true if selected element is a collection directly
 * linked to the active SceneLayer (not a nested collection)
 */
static int collection_unlink_poll(bContext *C)
{
	LayerCollection *lc = outliner_collection_active(C);

	if (lc == NULL) {
		return 0;
	}

	SceneLayer *sl = CTX_data_scene_layer(C);
	return BLI_findindex(&sl->layer_collections, lc) != -1 ? 1 : 0;
}

static int collection_unlink_exec(bContext *C, wmOperator *op)
{
	LayerCollection *lc = outliner_collection_active(C);
	SpaceOops *soops = CTX_wm_space_outliner(C);

	if (lc == NULL) {
		BKE_report(op->reports, RPT_ERROR, "Active element is not a collection");
		return OPERATOR_CANCELLED;
	}

	SceneLayer *sl = CTX_data_scene_layer(C);
	BKE_collection_unlink(sl, lc);

	if (soops) {
		outliner_cleanup_tree(soops);
	}

	DEG_relations_tag_update(CTX_data_main(C));

	/* TODO(sergey): Use proper flag for tagging here. */
	DEG_id_tag_update(&CTX_data_scene(C)->id, 0);

	WM_main_add_notifier(NC_SCENE | ND_LAYER, NULL);
	return OPERATOR_FINISHED;
}

void OUTLINER_OT_collection_unlink(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Collection";
	ot->idname = "OUTLINER_OT_collection_unlink";
	ot->description = "Unlink collection from the active layer";

	/* api callbacks */
	ot->exec = collection_unlink_exec;
	ot->poll = collection_unlink_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/**********************************************************************************/
/* Add new collection. */

static int collection_new_exec(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	SceneLayer *scene_layer = CTX_data_scene_layer(C);
	SceneCollection *scene_collection;
	Group *group;
	const char *name = NULL;

	const int collection_type = RNA_enum_get(op->ptr, "type");
	if (collection_type == COLLECTION_TYPE_GROUP) {
		group = BLI_findlink(&bmain->group, RNA_enum_get(op->ptr, "group"));
		name = group->id.name + 2;
	}

	scene_collection = BKE_collection_add(&scene->id, NULL, collection_type, name);

	if (collection_type == COLLECTION_TYPE_GROUP) {
		BKE_collection_group_set(scene, scene_collection, group);
		/* TODO(sergey): Use proper flag for tagging here. */
		DEG_id_tag_update(&scene->id, 0);
	}

	BKE_collection_link(scene_layer, scene_collection);

	DEG_relations_tag_update(bmain);
	WM_main_add_notifier(NC_SCENE | ND_LAYER, NULL);
	return OPERATOR_FINISHED;
}

static int collection_new_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
	/* A simple hidden functionality to help debugging
	 * before we have a UI for that. */
	PropertyRNA *prop = RNA_struct_find_property(op->ptr, "type");
	if (event->shift && !RNA_property_is_set(op->ptr, prop)) {
		RNA_property_enum_set(op->ptr, prop,  COLLECTION_TYPE_GROUP);
	}

	const int collection_type = RNA_enum_get(op->ptr, "type");
	switch (collection_type) {
		case COLLECTION_TYPE_GROUP:
			return WM_enum_search_invoke(C, op, event);
		case COLLECTION_TYPE_NONE:
		default:
			return collection_new_exec(C, op);
	}
}

void OUTLINER_OT_collection_new(wmOperatorType *ot)
{
	PropertyRNA *prop;

	/* identifiers */
	ot->name = "New Collection";
	ot->idname = "OUTLINER_OT_collection_new";
	ot->description = "Add a new collection to the scene, and link it to the active layer (Shift + Click for group)";

	/* api callbacks */
	ot->exec = collection_new_exec;
	ot->invoke = collection_new_invoke;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	prop = RNA_def_enum(ot->srna, "type", rna_enum_collection_type_items,
	                    COLLECTION_TYPE_NONE, "Type", "Type of collection to add");
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
	prop = RNA_def_enum(ot->srna, "group", DummyRNA_NULL_items, 0, "Group", "The group to use for the group collections");
	RNA_def_enum_funcs(prop, RNA_group_itemf);
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
	ot->prop = prop;
}

/**********************************************************************************/

/**
 * Returns true is selected element is a collection
 */
static int collection_override_new_poll(bContext *(C))
{
#ifdef TODO_LAYER_OVERRIDE
	/* disable for now, since it's not implemented */
	(void) C;
	return 0;
#else
	return outliner_collection_active(C) ? 1 : 0;
#endif
}

static int collection_override_new_invoke(bContext *UNUSED(C), wmOperator *op, const wmEvent *UNUSED(event))
{
	TODO_LAYER_OPERATORS;
	TODO_LAYER_OVERRIDE;
	BKE_report(op->reports, RPT_ERROR, "OUTLINER_OT_collections_override_new not implemented yet");
	return OPERATOR_CANCELLED;
}

/* in the middle of renames remove s */
void OUTLINER_OT_collection_override_new(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "New Override";
	ot->idname = "OUTLINER_OT_collection_override_new";
	ot->description = "Add a new override to the active collection";

	/* api callbacks */
	ot->invoke = collection_override_new_invoke;
	ot->poll = collection_override_new_poll;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

struct CollectionDeleteData {
	Scene *scene;
	SpaceOops *soops;
};

static TreeTraversalAction collection_delete_cb(TreeElement *te, void *customdata)
{
	struct CollectionDeleteData *data = customdata;
	SceneCollection *scene_collection = outliner_scene_collection_from_tree_element(te);

	if (!scene_collection) {
		return TRAVERSE_SKIP_CHILDS;
	}

	if (scene_collection == BKE_collection_master(data->scene)) {
		/* skip - showing warning/error message might be missleading
		 * when deleting multiple collections, so just do nothing */
	}
	else {
		BKE_collection_remove(data->scene, scene_collection);
	}

	return TRAVERSE_CONTINUE;
}

static int collection_delete_exec(bContext *C, wmOperator *UNUSED(op))
{
	Scene *scene = CTX_data_scene(C);
	SpaceOops *soops = CTX_wm_space_outliner(C);
	struct CollectionDeleteData data = {.scene = scene, .soops = soops};

	TODO_LAYER_OVERRIDE; /* handle overrides */
	outliner_tree_traverse(soops, &soops->tree, 0, TSE_SELECTED, collection_delete_cb, &data);

	DEG_relations_tag_update(CTX_data_main(C));

	/* TODO(sergey): Use proper flag for tagging here. */
	DEG_id_tag_update(&scene->id, 0);

	WM_main_add_notifier(NC_SCENE | ND_LAYER, NULL);

	return OPERATOR_FINISHED;
}

void OUTLINER_OT_collections_delete(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Delete";
	ot->idname = "OUTLINER_OT_collections_delete";
	ot->description = "Delete selected overrides or collections";

	/* api callbacks */
	ot->exec = collection_delete_exec;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int collection_select_exec(bContext *C, wmOperator *op)
{
	SceneLayer *sl = CTX_data_scene_layer(C);
	const int collection_index = RNA_int_get(op->ptr, "collection_index");
	sl->active_collection = collection_index;
	WM_main_add_notifier(NC_SCENE | ND_LAYER, NULL);
	return OPERATOR_FINISHED;
}

void OUTLINER_OT_collection_select(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Select";
	ot->idname = "OUTLINER_OT_collection_select";
	ot->description = "Change active collection or override";

	/* api callbacks */
	ot->exec = collection_select_exec;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	RNA_def_int(ot->srna, "collection_index", 0, 0, INT_MAX, "Index",
	            "Index of collection to select", 0, INT_MAX);
}

#define ACTION_DISABLE 0
#define ACTION_ENABLE 1
#define ACTION_TOGGLE 2

static int collection_toggle_exec(bContext *C, wmOperator *op)
{
	Main *bmain = CTX_data_main(C);
	Scene *scene = CTX_data_scene(C);
	SceneLayer *scene_layer = CTX_data_scene_layer(C);
	int action = RNA_enum_get(op->ptr, "action");
	LayerCollection *layer_collection = CTX_data_layer_collection(C);

	if (layer_collection->flag & COLLECTION_DISABLED) {
		if (ELEM(action, ACTION_TOGGLE, ACTION_ENABLE)) {
			BKE_collection_enable(scene_layer, layer_collection);
		}
		else { /* ACTION_DISABLE */
			BKE_reportf(op->reports, RPT_ERROR, "Layer collection %s already disabled",
			            layer_collection->scene_collection->name);
			return OPERATOR_CANCELLED;
		}
	}
	else {
		if (ELEM(action, ACTION_TOGGLE, ACTION_DISABLE)) {
			BKE_collection_disable(scene_layer, layer_collection);
		}
		else { /* ACTION_ENABLE */
			BKE_reportf(op->reports, RPT_ERROR, "Layer collection %s already enabled",
			            layer_collection->scene_collection->name);
			return OPERATOR_CANCELLED;
		}
	}

	DEG_relations_tag_update(bmain);
	/* TODO(sergey): Use proper flag for tagging here. */
	DEG_id_tag_update(&scene->id, 0);

	WM_event_add_notifier(C, NC_SCENE | ND_OB_SELECT, scene);
	WM_event_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);

	return OPERATOR_FINISHED;
}

void OUTLINER_OT_collection_toggle(wmOperatorType *ot)
{
	PropertyRNA *prop;

	static EnumPropertyItem actions_items[] = {
		{ACTION_DISABLE, "DISABLE", 0, "Disable", "Disable selected markers"},
		{ACTION_ENABLE, "ENABLE", 0, "Enable", "Enable selected markers"},
		{ACTION_TOGGLE, "TOGGLE", 0, "Toggle", "Toggle disabled flag for selected markers"},
		{0, NULL, 0, NULL, NULL}
	};

	/* identifiers */
	ot->name = "Toggle Collection";
	ot->idname = "OUTLINER_OT_collection_toggle";
	ot->description = "Deselect collection objects";

	/* api callbacks */
	ot->exec = collection_toggle_exec;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

	/* properties */
	prop = RNA_def_int(ot->srna, "collection_index", -1, -1, INT_MAX, "Collection Index", "Index of collection to toggle", 0, INT_MAX);
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
	prop = RNA_def_enum(ot->srna, "action", actions_items, ACTION_TOGGLE, "Action", "Selection action to execute");
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

#undef ACTION_TOGGLE
#undef ACTION_ENABLE
#undef ACTION_DISABLE

/* -------------------------------------------------------------------- */

static int stubs_invoke(bContext *UNUSED(C), wmOperator *op, const wmEvent *UNUSED(event))
{
	TODO_LAYER_OPERATORS;
	BKE_report(op->reports, RPT_ERROR, "Operator not implemented yet");
	return OPERATOR_CANCELLED;
}

void OUTLINER_OT_collection_objects_add(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Add Objects";
	ot->idname = "OUTLINER_OT_collection_objects_add";
	ot->description = "Add selected objects to collection";

	/* api callbacks */
	ot->invoke = stubs_invoke;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

void OUTLINER_OT_collection_objects_remove(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Remove Object";
	ot->idname = "OUTLINER_OT_collection_objects_remove";
	ot->description = "Remove objects from collection";

	/* api callbacks */
	ot->invoke = stubs_invoke;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

void OUTLINER_OT_collection_objects_select(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Select Objects";
	ot->idname = "OUTLINER_OT_collection_objects_select";
	ot->description = "Select collection objects";

	/* api callbacks */
	ot->invoke = stubs_invoke;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

void OUTLINER_OT_collection_objects_deselect(wmOperatorType *ot)
{
	/* identifiers */
	ot->name = "Deselect Objects";
	ot->idname = "OUTLINER_OT_collection_objects_deselect";
	ot->description = "Deselect collection objects";

	/* api callbacks */
	ot->invoke = stubs_invoke;

	/* flags */
	ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
