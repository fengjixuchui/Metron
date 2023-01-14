#include "MtSourceFile.h"
#include <memory.h>
#include "Log.h"
#include "MtModLibrary.h"
#include "MtModule.h"
#include "MtStruct.h"

#pragma warning(disable : 4996)

extern "C" {
extern const TSLanguage* tree_sitter_cpp();
}

//------------------------------------------------------------------------------

CHECK_RETURN Err MtSourceFile::init(MtModLibrary* _lib,
                                    const std::string& _filename,
                                    const std::string& _full_path,
                                    void* _src_blob,
                                    int _src_len) {
  Err err;

  lib = _lib;

  filename = _filename;
  full_path = _full_path;
  src_blob.resize(_src_len);
  memcpy(src_blob.data(), _src_blob, _src_len);
  assert(src_blob.back() != 0);

  auto blob_size = src_blob.size();
  source = (const char*)src_blob.data();
  source_end = source + blob_size;


  // Parse the source file.

  parser = ts_parser_new();
  lang = tree_sitter_cpp();
  ts_parser_set_language(parser, lang);

  tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)blob_size);

  // Pull out all modules from the top level of the source.

  TSNode ts_root = ts_tree_root_node(tree);
  auto root_sym = ts_node_symbol(ts_root);

  root_node = MnNode(MnNode(ts_root, root_sym, 0, this));

  assert(modules.empty());
  err << collect_modules(root_node);

  return err;
}

//------------------------------------------------------------------------------

MtSourceFile::~MtSourceFile() {
  ts_tree_delete(tree);
  ts_parser_delete(parser);

  for (auto m : modules) delete m;
  modules.clear();
  lang = nullptr;
  parser = nullptr;
  tree = nullptr;
  source = nullptr;
}

//------------------------------------------------------------------------------

//------------------------------------------------------------------------------

CHECK_RETURN Err MtSourceFile::collect_modules(MnNode toplevel) {
  Err err;

  for (const auto& c : toplevel) {
    switch (c.sym) {
      case sym_struct_specifier: {
        MtStruct* new_struct = new MtStruct(c, this);
        for (auto f : c.get_field(field_body)) {
          if (f.sym == sym_field_declaration) {
            MtField* new_field = new MtField(new_struct, f);
            new_struct->fields.push_back(new_field);
          }
        }
        structs.push_back(new_struct);
        break;
      }
      case sym_class_specifier:
      case sym_template_declaration: {
        //MnNode mod_root(c.node, c.sym, 0, this);
        MtModule* mod = new MtModule(lib);
        err << mod->init(this, c);
        modules.push_back(mod);
        break;
      }
      case sym_preproc_ifdef: {
        err << collect_modules(c);
        break;
      }
    }
  }

  return err;
}

//------------------------------------------------------------------------------

MtModule* MtSourceFile::get_module(const std::string& name) {
  for (auto m : modules) {
    if (m->name() == name) return m;
  }
  return nullptr;
}

//------------------------------------------------------------------------------

// KCOV_OFF
void MtSourceFile::dump() {
  LOG_G("Source file %s @ %s\n", filename.c_str(), full_path.c_str());
  LOG_INDENT_SCOPE();
  for (auto m : modules) {
    m->dump();
  }
}
// KCOV_ON

//------------------------------------------------------------------------------
