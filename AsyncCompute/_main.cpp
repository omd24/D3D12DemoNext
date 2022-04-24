#include "Timer.hpp"
#include "Camera.hpp"
#include "AppMgr.hpp"

int main() {

  Timer timer = {};
  timerInit(&timer);

  Camera camera = {};
  cameraInit(&camera, {0, 0, 0});

  return (0);
}
