//
// Created by Leon on 16/09/2025.
//

#pragma once


extern bool (* projectMainPrologue)();

extern bool (* projectMainOrDoOneLoop)();

extern int Main();

#define SET_APP_ENTRY_POINTS(pro, loop) \
static struct app_entry_point_setter_t \
{ \
app_entry_point_setter_t() \
{ \
projectMainPrologue = pro; \
projectMainOrDoOneLoop = loop; \
} \
} app_entry_point_setter_instance;
