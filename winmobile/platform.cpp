// The calls the core makes into the platform that the Android glue
// (render.c, keyboard.c, motion.c) used to answer.

#include <GLES/gl.h>

#include "gles_d3d11.h"
#include "platform.h"

extern "C" {

float Display_scale = 1.0f;

// Never defined anywhere; the NDK linker let the call from multi.c through
void network_dump_appletalk_player(unsigned char node, unsigned short net, unsigned char socket,
								   int why) {
}

void getRenderBufferSize(GLint *width, GLint *height) {
	gles_get_size(width, height);
}

void showRenderBuffer() {
	draw_buttons();
	gles_present();
}

// TODO: gyroscope through Windows.Devices.Sensors
void startMotion() {
}

void stopMotion() {
}

void getRotationRate(double *x, double *y, double *z) {
	*x = *y = *z = 0;
}

int haveGyroscope() {
	return 0;
}

}
