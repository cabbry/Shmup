#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <SLES/OpenSLES_AndroidConfiguration.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "android/native_activity.h"
extern ANativeWindow* window;

void engine_draw_frame(void);
int engine_init_display(void);
void engine_term_display(void);
