#pragma once
#include <stdbool.h>
int xiaotai_eyes_ui_start(void);
void xiaotai_eyes_ui_tag(const char *emotion,bool explicit_tag,int phase);
void xiaotai_eyes_ui_text(const char *left,const char *right);
void xiaotai_eyes_ui_idle(void);
