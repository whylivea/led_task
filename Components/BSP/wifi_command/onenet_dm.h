#ifndef __onenet_dm_h_
#define __onenet_dm_h_
#include "cJSON.h"

void onenet_dm_Init(void);

void onenet_property_handle(cJSON* property);

cJSON *onenet_property_upload(void);




#endif