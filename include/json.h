/*
 * json.h - A JSON parser
 *
 * Copyright (c) 2014   A. Dilly
 *
 * AirCat is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * AirCat is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with AirCat.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _JSON_H
#define _JSON_H

#include <json/json.h>

struct json;

/* JSON file parser */
#define json_from_file(f) (struct json *) json_object_from_file(f)
#define json_to_file(f, j) json_object_to_file(f, (struct json_object *) j)
#define json_to_file_ex(f, j, fl) json_object_to_file_ext(f, \
						   (struct json_object *) j, fl)

/* JSON object */
#define json_new() (struct json *) json_object_new_object()
#define json_copy(j) (struct json *) (j != NULL ? \
			       json_object_get((struct json_object *) j) : NULL)
#define json_free(j) if(j != NULL) json_object_put((struct json_object *) j)

/* JSON object to string */
#define json_export(j) json_object_to_json_string((struct json_object *) j)
#define json_export_ex(j, f) json_object_to_json_string_ext( \
						    (struct json_object *) j, f)

/* JSON object */
#define json_add(j, k, v) json_object_object_add((struct json_object *) j, k, \
						 (struct json_object *) v)
#define json_get(j, k) (struct json *) (j != NULL ? \
		     json_object_object_get((struct json_object *) j, k) : NULL)
#define json_get_ex(j, k, v) (int) json_object_object_get_ex( \
						  (struct json_object *) j, k, \
						  (struct json_object **) v)
#define json_has_key(j, k) (int) json_get_ex(j, k, NULL)
#define json_del(j, k) json_object_object_del((struct json_object *) j, k)

/* JSON Array */
#define json_new_array() (struct json *) json_object_new_array()
#define json_array_length(j) json_object_array_length((struct json_object *) j)
#define json_array_add(j, v) json_object_array_add((struct json_object *) j, \
						   (struct json_object *) v)
#define json_array_set(j, i, v) json_object_array_put_idx( \
						  (struct json_object *) j, i, \
						  (struct json_object *) v)
#define json_array_get(j, i) (struct json *) json_object_array_get_idx( \
						    (struct json_object *) j, i)

/* JSON boolean */
#define json_new_bool(v) (struct json *) json_object_new_boolean((json_bool) v)
#define json_to_bool(j) (int) json_object_get_boolean((struct json_object *) j)
#define json_get_bool(j, k) json_to_bool(json_get(j, k))
#define json_set_bool(j, k, v) json_add(j, k, json_new_bool((json_bool) v))

/* JSON integer */
#define json_new_int(v) (struct json *) json_object_new_int(v)
#define json_new_int64(v)(struct json *) json_object_new_int64(v)
#define json_to_int(j) json_object_get_int((struct json_object *) j)
#define json_to_int64(j) json_object_get_int64((struct json_object *) j)
#define json_get_int(j, k) json_to_int(json_get(j, k))
#define json_get_int64(j, k) json_to_int64(json_get(j, k))
#define json_set_int(j, k, v) json_add(j, k, json_new_int(v))
#define json_set_int64(j, k, v) json_add(j, k, json_new_int64(v))

/* JSON double */
#define json_new_double(v) (struct json *) json_object_new_double(v)
#define json_to_double(j) json_object_get_double((struct json_object *) j)
#define json_get_double(j, k) json_to_double(json_get(j, k))
#define json_set_double(j, k, v) json_add(j, k, json_new_double(v))

/* JSON string */
#define json_new_string(v) (struct json *) (v != NULL ? \
					       json_object_new_string(v) : NULL)
#define json_new_string_len(v, l) (struct json *) (v != NULL ? \
					json_object_new_string_len(v, l) : NULL)
#define json_to_string(j) json_object_get_string((struct json_object *) j)
#define json_to_string_len(j) json_object_get_string_len( \
						       (struct json_object *) j)
#define json_get_string(j, k) json_to_string(json_get(j, k))
#define json_set_string(j, k, v) json_add(j, k, json_new_string(v))

/* JSON foreach paerser
 * j: (struct json *) to parse
 * key: (const char *) to contain current key
 * val: (struct json *) to contain current value associated to key
 * e: (struct lh_table *) used to parse the JSON object
 */
#define json_foreach(j, key, val, e) \
	     for(e = json_object_get_object((struct json_object *) j)->head; \
	         (e ? (key = (char*)e->k, val = (struct json *) e->v, e) : 0); \
	         e = e->next)

#endif
