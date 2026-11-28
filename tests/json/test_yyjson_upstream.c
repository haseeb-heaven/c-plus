/* yyjson upstream API smoke/navigation test (vendored ibireme/yyjson, MIT).
 * Uses yyjson's own public API (no custom wrapper). */
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "yyjson.h"

int main(void){
    const char *json = "{\"id\":42,\"name\":\"cplus\",\"ok\":true,\"vals\":[1,2,3]}";
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    assert(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    assert(root && yyjson_is_obj(root));

    yyjson_val *id = yyjson_obj_get(root, "id");
    assert(id && yyjson_is_int(id) && yyjson_get_sint(id) == 42);
    for (int i = 0; i < 10; ++i) assert(yyjson_get_sint(yyjson_obj_get(root,"id")) == 42);

    yyjson_val *name = yyjson_obj_get(root, "name");
    assert(name && yyjson_is_str(name) && strcmp(yyjson_get_str(name), "cplus") == 0);

    yyjson_val *vals = yyjson_obj_get(root, "vals");
    assert(vals && yyjson_is_arr(vals) && yyjson_arr_size(vals) == 3);
    assert(yyjson_get_sint(yyjson_arr_get(vals, 2)) == 3);

    assert(yyjson_obj_get(root, "absent") == NULL);
    yyjson_doc_free(doc);

    /* malformed input -> NULL doc */
    assert(yyjson_read("{bad", 4, 0) == NULL);

    printf("yyjson_upstream: ok\n");
    return 0;
}
