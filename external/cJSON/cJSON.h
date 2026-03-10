/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors (MIT License)
  https://github.com/DaveGamble/cJSON
  Header-only reference for build; full source downloaded by CI.
*/
#ifndef cJSON__h
#define cJSON__h

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct cJSON {
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;
    int type;
    char *valuestring;
    int valueint;
    double valuedouble;
    char *string;
} cJSON;

#define cJSON_Invalid (0)
#define cJSON_False  (1 << 0)
#define cJSON_True   (1 << 1)
#define cJSON_NULL   (1 << 2)
#define cJSON_Number (1 << 3)
#define cJSON_String (1 << 4)
#define cJSON_Array  (1 << 5)
#define cJSON_Object (1 << 6)
#define cJSON_Raw    (1 << 7)
#define cJSON_IsReference (256)
#define cJSON_StringIsConst (512)

cJSON *cJSON_Parse(const char *value);
void cJSON_Delete(cJSON *item);
cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string);
cJSON *cJSON_GetArrayItem(const cJSON *array, int index);
int cJSON_GetArraySize(const cJSON *array);

#define cJSON_IsString(item) ((item)->type & cJSON_String)
#define cJSON_IsArray(item)  ((item)->type & cJSON_Array)
#define cJSON_IsObject(item) ((item)->type & cJSON_Object)
#define cJSON_IsTrue(item)   ((item)->type & cJSON_True)
#define cJSON_IsFalse(item)  ((item)->type & cJSON_False)
#define cJSON_IsBool(item)   (((item)->type & (cJSON_True | cJSON_False)) != 0)

#ifdef __cplusplus
}
#endif

#endif
