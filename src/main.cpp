#include "tracer/tracer.h"

#include <ext/json.hpp>
#include <nvh/inputparser.h>

int main(int argc, char** argv) {
  // setup some basic things for the sample, logging file for example
  NVPSystem system(PROJECT_NAME);

  LOG_INFO(">>> {} by f1shel <<<", PROJECT_NAME);

  InputParser parser(argc, argv);
  if (parser.exist("--help")) {
    // TODO: help function
    exit(0);
  }

  TracerInitSettings tis;
  if (parser.exist("--offline")) tis.offline = true;
  tis.outputname = parser.getString("--out", "asuna_out.hdr");
  tis.scenefile = parser.getString("--scene", "PLEASE_SET_SCENE_PATH");

  Tracer asuna;
  asuna.init(tis);
  asuna.run();
  asuna.deinit();
  return 0;
}