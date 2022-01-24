#include "MtModLibrary.h"

#pragma once
#include <vector>
#include "MtModule.h"

//------------------------------------------------------------------------------

void MtModLibrary::reset() {
  for (auto module : modules) delete module;
  modules.clear();
}

void MtModLibrary::load(const std::string& input_filename, const std::string& output_filename) {
  auto mod = new MtModule();
  mod->load(input_filename, output_filename);
  modules.push_back(mod);
}

MtModule* MtModLibrary::find_module(const std::string& module_name) {
  for (auto mod : modules) {
    if (mod->module_name == module_name) return mod;
  }
  return nullptr;
}

//------------------------------------------------------------------------------
